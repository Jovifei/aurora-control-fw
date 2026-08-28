#include "app.h"
#include "app_config.h"
#include "board.h"
#include "mock_driver.h"
#include "service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned g_assertions;

#define CHECK(x) do { g_assertions++; if (!(x)) { \
    fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)


/*---------------------------------------------------------------------------*
 * Name        : static void run_service_ticks(aurora_service_t *service, uint32_t count)
 * Input       : service - Service上下文；count - 推进节拍数量
 * Output      : 无
 * Description : 推进指定数量的模拟1 ms节拍并调用Service主循环，用于Host回归。
 *---------------------------------------------------------------------------*/
static void run_service_ticks(aurora_service_t *service, uint32_t count)
{
    uint32_t i;
    for (i = 0U; i < count; ++i)
    {
        aurora_service_isr_tick(service);
        aurora_service_poll(service);
    }
}

/*---------------------------------------------------------------------------*
 * Name        : static aurora_measurement_calibration_t unit_calibration(void)
 * Input       : 无
 * Output      : 六通道单位增益标定结构
 * Description : 构造单位增益的六通道测试标定参数。
 *---------------------------------------------------------------------------*/
static aurora_measurement_calibration_t unit_calibration(void)
{
    aurora_measurement_calibration_t c;
    size_t i;
    memset(&c, 0, sizeof(c));
    for (i = 0U; i < AURORA_ADC_CHANNEL_COUNT; ++i)
    {
        c.channel[i].gain_num = 1;
        c.channel[i].gain_den = 1;
        c.channel[i].polarity = 1;
        c.channel[i].valid = true;
    }
    return c;
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_measurement_block(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证ADC完整块去极值平均、单位换算和PV功率有效位发布。
 *---------------------------------------------------------------------------*/
static void test_measurement_block(void)
{
    aurora_measurement_ctx_t ctx;
    aurora_measurement_t out;
    aurora_measurement_calibration_t c = unit_calibration();
    uint16_t raw[AURORA_ADC_BLOCK_WORDS];
    size_t scan;
    size_t ch;

    for (scan = 0U; scan < AURORA_ADC_SCANS_PER_BLOCK; ++scan)
    {
        for (ch = 0U; ch < AURORA_ADC_CHANNEL_COUNT; ++ch)
        {
            raw[scan * AURORA_ADC_CHANNEL_COUNT + ch] = (uint16_t)(100U + ch * 10U);
        }
    }
    raw[0] = 0U;
    raw[(AURORA_ADC_SCANS_PER_BLOCK - 1U) * AURORA_ADC_CHANNEL_COUNT] = 4095U;
    aurora_measurement_init(&ctx, &c);
    CHECK(aurora_measurement_process_block(&ctx, raw, AURORA_ADC_BLOCK_WORDS, 10U) == AURORA_STATUS_OK);
    CHECK(aurora_measurement_read(&ctx, &out));
    CHECK(out.pv_current_ma == 100);
    CHECK(out.pv_voltage_mv == 110);
    CHECK((out.valid_mask & AURORA_MEAS_VALID_PV_POWER) != 0U);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_protocol_roundtrip(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证设置帧编码后逐字节解析可无损还原动作、资源、消息ID和载荷。
 *---------------------------------------------------------------------------*/
static void test_protocol_roundtrip(void)
{
    aurora_protocol_frame_t tx;
    aurora_protocol_frame_t rx;
    aurora_protocol_ctx_t parser;
    uint8_t wire[AURORA_PROTOCOL_MAX_WIRE];
    size_t length;
    size_t i;

    memset(&tx, 0, sizeof(tx));
    tx.action = AURORA_PROTOCOL_ACTION_WRITE;
    tx.resource = AURORA_PROTOCOL_RESOURCE_SETTING;
    tx.message_id = 0x12345678UL;
    tx.data_length = AURORA_PROTOCOL_SETTING_DATA_LENGTH;
    tx.data[0] = AURORA_CHEM_LFP;
    tx.data[1] = AURORA_PACK_60V;
    length = aurora_protocol_encode(&tx, wire, sizeof(wire));
    CHECK(length == 16U);
    aurora_protocol_init(&parser);
    for (i = 0U; i < length; ++i)
    {
        aurora_protocol_feed_byte(&parser, wire[i], (uint32_t)i);
    }
    CHECK(aurora_protocol_take_frame(&parser, &rx));
    CHECK(rx.action == tx.action);
    CHECK(rx.resource == tx.resource);
    CHECK(rx.message_id == tx.message_id);
    CHECK(rx.data_length == AURORA_PROTOCOL_SETTING_DATA_LENGTH);
    CHECK(rx.data[0] == tx.data[0]);
    CHECK(rx.data[1] == tx.data[1]);
}


/*---------------------------------------------------------------------------*
 * Name        : static void test_protocol_back_to_back_frames(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证连续两帧在及时领取后均可解析，后一帧不会覆盖前一帧。
 *---------------------------------------------------------------------------*/
static void test_protocol_back_to_back_frames(void)
{
    aurora_protocol_frame_t first;
    aurora_protocol_frame_t second;
    aurora_protocol_frame_t out;
    aurora_protocol_ctx_t parser;
    uint8_t wire1[32];
    uint8_t wire2[32];
    size_t len1;
    size_t len2;
    size_t i;

    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));
    first.action = AURORA_PROTOCOL_ACTION_WRITE;
    first.resource = AURORA_PROTOCOL_RESOURCE_RESET;
    first.message_id = 1U;
    second.action = AURORA_PROTOCOL_ACTION_WRITE;
    second.resource = AURORA_PROTOCOL_RESOURCE_SETTING;
    second.message_id = 2U;
    second.data_length = AURORA_PROTOCOL_SETTING_DATA_LENGTH;
    second.data[0] = AURORA_CHEM_LEAD;
    second.data[1] = AURORA_PACK_72V;
    len1 = aurora_protocol_encode(&first, wire1, sizeof(wire1));
    len2 = aurora_protocol_encode(&second, wire2, sizeof(wire2));
    aurora_protocol_init(&parser);

    for (i = 0U; i < len1; ++i)
    {
        aurora_protocol_feed_byte(&parser, wire1[i], (uint32_t)i);
    }
    CHECK(aurora_protocol_take_frame(&parser, &out));
    CHECK(out.message_id == 1U);
    for (i = 0U; i < len2; ++i)
    {
        aurora_protocol_feed_byte(&parser, wire2[i], (uint32_t)(len1 + i));
    }
    CHECK(aurora_protocol_take_frame(&parser, &out));
    CHECK(out.message_id == 2U);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_telemetry_legacy_identity(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证旧产品遥测帧的固定长度、版本字段和型号字符保持兼容。
 *---------------------------------------------------------------------------*/
static void test_telemetry_legacy_identity(void)
{
    aurora_protocol_frame_t frame;
    aurora_measurement_t sample;
    aurora_persistent_settings_t settings;

    memset(&sample, 0, sizeof(sample));
    memset(&settings, 0, sizeof(settings));
    sample.pv_voltage_mv = 17000;
    sample.pv_current_ma = 1000;
    sample.battery_voltage_mv = 72000;
    sample.battery_current_est_ma = 2000;
    aurora_protocol_fill_telemetry(&frame, 7U, &sample, AURORA_CHARGE_CC,
                                   AURORA_FAULT_FAST_PV_OCP, &settings);
    CHECK(frame.data_length == AURORA_PROTOCOL_TELEMETRY_DATA_LENGTH);
    CHECK(frame.data[22] == 2U);
    CHECK(frame.data[23] == AURORA_FW_VERSION_MAJOR);
    CHECK(frame.data[26] == (uint8_t)AURORA_PRODUCT_MODEL[0]);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_watchdog_window_and_adc_overrun(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证看门狗仅在健康票据齐全时喂狗，并验证DMA同半块覆盖会锁存故障。
 *---------------------------------------------------------------------------*/
static void test_watchdog_window_and_adc_overrun(void)
{
    aurora_service_t service;

    /* 主循环仍在跑但没有1ms控制心跳时，健康监督不得盲目喂狗。 */
    mock_reset();
    CHECK(aurora_service_init(&service));
    mock_advance_ms(AURORA_WATCHDOG_STARTUP_GRACE_MS +
                    AURORA_WATCHDOG_WINDOW_MS);
    aurora_service_poll(&service);
    CHECK(mock_watchdog_feeds() == 0U);

    /* 恢复SysTick/控制任务后，一个完整健康窗口才允许喂狗。 */
    run_service_ticks(&service, AURORA_WATCHDOG_WINDOW_MS);
    CHECK(mock_watchdog_feeds() == 1U);

    /* 同一DMA半块在尚未消费前再次完成，必须锁存overrun。 */
    mock_reset();
    CHECK(aurora_service_init(&service));
    aurora_service_isr_adc_block(&service, 0U);
    aurora_service_isr_adc_block(&service, 0U);
    CHECK(service.adc_overrun_count == 1U);
    aurora_service_poll(&service);
    CHECK((service.app.protection.latched_mask & AURORA_FAULT_ADC_OVERRUN) != 0U);

    /* 主循环正在读取半块期间，DMA绕回同一半块也必须判定覆盖风险。 */
    mock_reset();
    CHECK(aurora_service_init(&service));
    service.adc_processing_mask = 1U;
    aurora_service_isr_adc_block(&service, 0U);
    CHECK(service.adc_overrun_count == 1U);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_storage_atomic_format(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证未提交页不可恢复、完整页可恢复，且载荷位翻转会被CRC拒绝。
 *---------------------------------------------------------------------------*/
static void test_storage_atomic_format(void)
{
    aurora_storage_ctx_t ctx;
    aurora_persistent_settings_t restored;
    uint32_t sequence;
    uint8_t page[AURORA_STORAGE_PAGE_SIZE];

    aurora_storage_init_defaults(&ctx);
    ctx.sequence = 42U;
    ctx.settings.daily_energy_wh = 123U;
    CHECK(aurora_storage_encode_page(&ctx, page, sizeof(page), false) != 0U);
    CHECK(!aurora_storage_decode_page(page, sizeof(page), &restored, &sequence));
    CHECK(aurora_storage_encode_page(&ctx, page, sizeof(page), true) != 0U);
    CHECK(aurora_storage_decode_page(page, sizeof(page), &restored, &sequence));
    CHECK(sequence == 42U);
    CHECK(restored.daily_energy_wh == 123U);
    page[AURORA_STORAGE_HEADER_SIZE] ^= 1U;
    CHECK(!aurora_storage_decode_page(page, sizeof(page), &restored, &sequence));
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_mppt_reference_search(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证参考电压型MPPT从Voc附近启动并按P-V趋势调整目标且服从功率上限。
 *---------------------------------------------------------------------------*/
static void test_mppt_reference_search(void)
{
    aurora_mppt_ctx_t ctx;
    aurora_measurement_t sample;
    aurora_mppt_output_t out;

    memset(&sample, 0, sizeof(sample));
    sample.valid_mask = AURORA_MEAS_VALID_PV_V | AURORA_MEAS_VALID_PV_I |
                        AURORA_MEAS_VALID_PV_POWER;
    sample.pv_voltage_mv = 30000;
    sample.pv_current_ma = 1000;
    sample.pv_power_mw = 30000;
    aurora_mppt_init(&ctx);
    aurora_mppt_set_open_circuit_voltage(&ctx, 40000U);
    out = aurora_mppt_step(&ctx, &sample, 200000U, false, 10U);
    CHECK(out.valid);
    sample.pv_voltage_mv = 29000;
    sample.pv_power_mw = 35000;
    out = aurora_mppt_step(&ctx, &sample, 200000U, false, 90U);
    CHECK(out.target_voltage_mv < 39500U);
    CHECK(out.theoretical_power_mw <= 200000U);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_charger_profiles(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证铅酸72V档案关键阈值以及有效电池电压下的CC阶段输出。
 *---------------------------------------------------------------------------*/
static void test_charger_profiles(void)
{
    aurora_charge_profile_t p;
    aurora_charger_ctx_t c;
    aurora_measurement_t s;
    aurora_charge_output_t o;

    CHECK(aurora_charge_profile_get(AURORA_CHEM_LEAD, AURORA_PACK_72V, &p));
    CHECK(p.cv_target_mv == 87200U);
    CHECK(p.float_target_mv == 82200U);
    memset(&s, 0, sizeof(s));
    s.valid_mask = AURORA_MEAS_VALID_BAT_V;
    s.battery_voltage_mv = 75000;
    aurora_charger_init(&c, AURORA_CHEM_LEAD, AURORA_PACK_72V, 0U);
    o = aurora_charger_step(&c, &s, false, false, 10U);
    CHECK(o.state == AURORA_CHARGE_CC);
    CHECK(o.allow_charge);

    /* 非法枚举状态必须fail-safe到FAULT，不能因为default被移除而静默悬空。 */
    c.state = (aurora_charge_state_t)0x7FU;
    o = aurora_charger_step(&c, &s, false, false, 20U);
    CHECK(o.state == AURORA_CHARGE_FAULT);
    CHECK(!o.allow_charge);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_fault_startup_and_no_battery(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证上电测量宽限期结束后锁存ADC超时，同时无电池不会误报欠压。
 *---------------------------------------------------------------------------*/
static void test_fault_startup_and_no_battery(void)
{
    aurora_protection_ctx_t p;
    aurora_charge_profile_t profile;
    aurora_measurement_t s;

    CHECK(aurora_charge_profile_get(AURORA_CHEM_LEAD, AURORA_PACK_72V, &profile));
    memset(&s, 0, sizeof(s));
    aurora_protection_init(&p, 0U);
    aurora_protection_step(&p, &s, &profile, 100U);
    CHECK(p.latched_mask == 0U);
    aurora_protection_step(&p, &s, &profile, 300U);
    CHECK((p.latched_mask & AURORA_FAULT_ADC_STALE) != 0U);

    aurora_protection_init(&p, 0U);
    s.sequence = 1U;
    s.timestamp_ms = 1U;
    s.valid_mask = AURORA_MEAS_VALID_PV_V | AURORA_MEAS_VALID_PV_I |
                   AURORA_MEAS_VALID_BAT_V | AURORA_MEAS_VALID_BUS_V;
    s.pv_voltage_mv = 0;
    s.battery_voltage_mv = 0;
    aurora_protection_step(&p, &s, &profile, 2U);
    CHECK(p.latched_mask == 0U);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_precharge_bootstrap_duty(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证预充初期母线低于PV时仍从零占空比按单步限制启动。
 *---------------------------------------------------------------------------*/
static void test_precharge_bootstrap_duty(void)
{
    aurora_power_stage_ctx_t ctx;
    aurora_measurement_t sample;
    aurora_mppt_output_t mppt;
    aurora_charge_output_t charger;
    aurora_power_command_t command;

    memset(&sample, 0, sizeof(sample));
    memset(&mppt, 0, sizeof(mppt));
    memset(&charger, 0, sizeof(charger));
    sample.valid_mask = AURORA_MEAS_VALID_PV_V | AURORA_MEAS_VALID_PV_POWER |
                        AURORA_MEAS_VALID_BAT_V | AURORA_MEAS_VALID_BUS_V;
    sample.pv_voltage_mv = 30000;
    sample.pv_power_mw = 0;
    sample.battery_voltage_mv = 72000;
    sample.bus_voltage_mv = 0;
    charger.allow_charge = true;
    charger.power_limit_mw = AURORA_RATED_POWER_MW;
    mppt.valid = true;
    mppt.theoretical_power_mw = AURORA_PRECHARGE_POWER_MW;

    aurora_power_stage_init(&ctx, 0U);
    command = aurora_power_stage_step(&ctx, &sample, &mppt, &charger, true, 1U);
    CHECK(command.state == AURORA_POWER_PRECHARGE);
    CHECK(!command.pwm_enable);

    /* BST_U尚低于PV_U时仍需从最小Duty受限爬升，否则预充永远无法建立母线。 */
    command = aurora_power_stage_step(&ctx, &sample, &mppt, &charger, true, 2U);
    CHECK(command.pwm_enable);
    CHECK(command.duty_q15 > 0U);
    CHECK(command.duty_q15 <= AURORA_DUTY_STEP_Q15);
    CHECK(!command.relay_enable);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_temperature_faults_are_independent(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证环境过温和MOS过温拥有独立计数器与故障位，互不串扰。
 *---------------------------------------------------------------------------*/
static void test_temperature_faults_are_independent(void)
{
    aurora_protection_ctx_t protection;
    aurora_charge_profile_t profile;
    aurora_measurement_t sample;
    uint32_t now_ms;

    CHECK(aurora_charge_profile_get(AURORA_CHEM_LEAD, AURORA_PACK_72V, &profile));
    memset(&sample, 0, sizeof(sample));
    sample.sequence = 1U;
    sample.valid_mask = AURORA_MEAS_VALID_PV_V | AURORA_MEAS_VALID_PV_I |
                        AURORA_MEAS_VALID_BAT_V | AURORA_MEAS_VALID_BUS_V |
                        AURORA_MEAS_VALID_MOS_TEMP | AURORA_MEAS_VALID_AMB_TEMP;
    sample.pv_voltage_mv = 30000;
    sample.battery_voltage_mv = 72000;
    sample.bus_voltage_mv = 71000;
    sample.mos_temp_dC = 500;
    sample.ambient_temp_dC = 900;

    aurora_protection_init(&protection, 0U);
    for (now_ms = 1U; now_ms <= 10U; ++now_ms)
    {
        sample.timestamp_ms = now_ms;
        aurora_protection_step(&protection, &sample, &profile, now_ms);
    }
    CHECK((protection.latched_mask & AURORA_FAULT_AMB_TEMP) != 0U);
    CHECK((protection.latched_mask & AURORA_FAULT_MOS_OVERTEMP) == 0U);
    CHECK(protection.amb_temp_count == 10U);
    CHECK(protection.mos_temp_count == 0U);
}


/*---------------------------------------------------------------------------*
 * Name        : static void test_fault_rearm_resets_duty_origin(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证故障和重新授权会清零Duty/积分，重新预充的首个命令只能从零按单步斜率起步。
 *---------------------------------------------------------------------------*/
static void test_fault_rearm_resets_duty_origin(void)
{
    aurora_power_stage_ctx_t ctx;
    aurora_measurement_t sample;
    aurora_mppt_output_t mppt;
    aurora_charge_output_t charger;
    aurora_power_command_t command;

    memset(&ctx, 0, sizeof(ctx));
    memset(&sample, 0, sizeof(sample));
    memset(&mppt, 0, sizeof(mppt));
    memset(&charger, 0, sizeof(charger));

    sample.valid_mask = AURORA_MEAS_VALID_PV_V | AURORA_MEAS_VALID_PV_POWER |
                        AURORA_MEAS_VALID_BAT_V | AURORA_MEAS_VALID_BUS_V;
    sample.pv_voltage_mv = 30000;
    sample.pv_power_mw = 50000;
    sample.battery_voltage_mv = 72000;
    sample.bus_voltage_mv = 70000;
    mppt.valid = true;
    mppt.theoretical_power_mw = 100000U;
    charger.allow_charge = true;
    charger.power_limit_mw = AURORA_RATED_POWER_MW;

    /* 模拟故障前已处于RUN且控制器保存了非零Duty和积分。 */
    ctx.state = AURORA_POWER_RUN;
    ctx.relay_closed = true;
    ctx.duty_q15 = 12000U;
    ctx.power_integral = 2048LL;

    command = aurora_power_stage_step(&ctx, &sample, &mppt, &charger, false, 100U);
    CHECK(command.state == AURORA_POWER_FAULT);
    CHECK(command.duty_q15 == 0U);
    CHECK(ctx.duty_q15 == 0U);
    CHECK(ctx.power_integral == 0LL);

    /* 故障即使很快消失，也必须先满足继电器最短放能时间。 */
    command = aurora_power_stage_step(&ctx, &sample, &mppt, &charger, true, 110U);
    CHECK(command.state == AURORA_POWER_FAULT);
    CHECK(!command.pwm_enable);
    CHECK(command.relay_enable);

    /* 满足放能时间后先回WAIT_BATTERY，下一拍再重新进入预充。 */
    command = aurora_power_stage_step(&ctx, &sample, &mppt, &charger, true, 121U);
    CHECK(command.state == AURORA_POWER_WAIT_BATTERY);
    CHECK(!command.pwm_enable);
    CHECK(!command.relay_enable);
    CHECK(ctx.duty_q15 == 0U);

    command = aurora_power_stage_step(&ctx, &sample, &mppt, &charger, true, 122U);
    CHECK(command.state == AURORA_POWER_PRECHARGE);
    CHECK(!command.pwm_enable);

    command = aurora_power_stage_step(&ctx, &sample, &mppt, &charger, true, 123U);
    CHECK(command.pwm_enable);
    CHECK(command.duty_q15 > 0U);
    CHECK(command.duty_q15 <= AURORA_DUTY_STEP_Q15);

    /* 状态值损坏时必须立刻关继电器、清Duty并进入FAULT。 */
    ctx.state = (aurora_power_state_t)0x7FU;
    ctx.relay_closed = true;
    ctx.duty_q15 = 12000U;
    ctx.power_integral = 2048LL;
    command = aurora_power_stage_step(&ctx, &sample, &mppt, &charger, true, 200U);
    CHECK(command.state == AURORA_POWER_FAULT);
    CHECK(!command.pwm_enable);
    CHECK(!command.relay_enable);
    CHECK(command.duty_q15 == 0U);
    CHECK(ctx.power_integral == 0LL);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_pwm_arm_race(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证首次放行必须先等待零CCR自然装载，且快速故障会立即撤销PWM。
 *---------------------------------------------------------------------------*/
static void test_pwm_arm_race(void)
{
    aurora_service_t service;

    mock_reset();
    CHECK(aurora_service_init(&service));
    service.app.protection.active_mask = 0U;
    service.app.protection.latched_mask = 0U;
    service.app.power_command.pwm_enable = true;
    service.app.power_command.duty_q15 = 1000U;
    service.app.power_command.relay_enable = false;

    /* 第一轮只写0到shadow，不能立即开MOE。 */
    aurora_service_poll(&service);
    CHECK(!mock_pwm_active());
    mock_apply_uev();
    aurora_service_isr_pwm_update(&service);
    aurora_service_poll(&service);
    CHECK(!mock_pwm_active()); /* 板级功率门禁默认关闭。 */

    aurora_service_isr_fast_fault(&service, AURORA_FAULT_FAST_MOS_OCP);
    CHECK(!mock_pwm_active());
    aurora_service_poll(&service);
    CHECK((service.app.protection.latched_mask & AURORA_FAULT_FAST_MOS_OCP) != 0U);
}

/*---------------------------------------------------------------------------*
 * Name        : int main(void)
 * Input       : 无
 * Output      : 0表示全部Host回归通过；断言失败时进程提前退出
 * Description : Host回归入口：依次执行测量、协议、存储、MPPT、充电、保护、功率级、PWM竞态和看门狗测试；全部通过后返回0。
 *---------------------------------------------------------------------------*/
int main(void)
{
    test_measurement_block();
    test_protocol_roundtrip();
    test_protocol_back_to_back_frames();
    test_telemetry_legacy_identity();
    test_storage_atomic_format();
    test_mppt_reference_search();
    test_charger_profiles();
    test_fault_startup_and_no_battery();
    test_precharge_bootstrap_duty();
    test_temperature_faults_are_independent();
    test_fault_rearm_resets_duty_origin();
    test_pwm_arm_race();
    test_watchdog_window_and_adc_overrun();
    printf("Aurora host tests: %u assertions passed.\n", g_assertions);
    return 0;
}
