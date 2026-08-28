#include "app.h"
#include "app_config.h"
#include "board.h"
#include "driver.h"
#include "mock_driver.h"
#include "service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned g_assertions;

#define CHECK(x) do { g_assertions++; if (!(x)) { \
    fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

/*---------------------------------------------------------------------------*
 * Name        : static aurora_measurement_calibration_t unit_calibration(void)
 * Input       : 无
 * Output      : 六通道单位增益标定
 * Description : 构造Host测量模块使用的单位增益测试标定，NTC通道仍由Measurement专用查表处理。
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
 * Name        : static aurora_measurement_t valid_sample(int32_t pv_mv,
 *               int32_t pv_ma, int32_t bat_mv, int32_t bus_mv, uint32_t now_ms)
 * Input       : pv_mv/pv_ma/bat_mv/bus_mv - 物理量；now_ms - 样本时间
 * Output      : 保护/功率状态机可直接使用的完整Host测量快照
 * Description : 统一构造V/I/Vbat/Vbus/PV功率和温度有效的测试样本。
 *---------------------------------------------------------------------------*/
static aurora_measurement_t valid_sample(int32_t pv_mv,
                                         int32_t pv_ma,
                                         int32_t bat_mv,
                                         int32_t bus_mv,
                                         uint32_t now_ms)
{
    aurora_measurement_t s;
    memset(&s, 0, sizeof(s));
    s.sequence = 1U;
    s.timestamp_ms = now_ms;
    s.valid_mask = AURORA_MEAS_VALID_PV_V |
                   AURORA_MEAS_VALID_PV_I |
                   AURORA_MEAS_VALID_BAT_V |
                   AURORA_MEAS_VALID_BUS_V |
                   AURORA_MEAS_VALID_PV_POWER |
                   AURORA_MEAS_VALID_MOS_TEMP |
                   AURORA_MEAS_VALID_AMB_TEMP;
    s.pv_voltage_mv = pv_mv;
    s.pv_current_ma = pv_ma;
    s.pv_power_mw = (int32_t)(((int64_t)pv_mv * (int64_t)pv_ma) /
                                  (int64_t)AURORA_MV_MA_PER_MW);
    s.battery_voltage_mv = bat_mv;
    s.bus_voltage_mv = bus_mv;
    s.mos_temp_dC = 250;
    s.ambient_temp_dC = 250;
    return s;
}

/*---------------------------------------------------------------------------*
 * Name        : static void protection_step_at(aurora_protection_ctx_t *ctx,
 *               aurora_measurement_t *sample,
 *               const aurora_charge_profile_t *profile, uint32_t now_ms)
 * Input       : ctx/sample/profile - 保护输入；now_ms - 当前时间
 * Output      : 无
 * Description : 更新时间戳后运行一次保护，避免保护测试被ADC_STALE误干扰。
 *---------------------------------------------------------------------------*/
static void protection_step_at(aurora_protection_ctx_t *ctx,
                               aurora_measurement_t *sample,
                               const aurora_charge_profile_t *profile,
                               uint32_t now_ms)
{
    sample->timestamp_ms = now_ms;
    aurora_protection_step(ctx, sample, profile, true, true, now_ms);
}

/*---------------------------------------------------------------------------*
 * Name        : static void run_service_ticks(aurora_service_t *service,
 *               uint32_t count)
 * Input       : service - Service上下文；count - 1ms节拍数
 * Output      : 无
 * Description : 推进Host SysTick并运行Service主循环，用于看门狗和事件回归。
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
 * Name        : static void fill_adc_block(uint16_t *raw, uint16_t pv_i_code)
 * Input       : raw - DMA块；pv_i_code - PV_I码值
 * Output      : 无
 * Description : 构造稳定的六通道完整DMA块，供测量和零点校准回归。
 *---------------------------------------------------------------------------*/
static void fill_adc_block(uint16_t *raw, uint16_t pv_i_code)
{
    size_t scan;
    for (scan = 0U; scan < AURORA_ADC_SCANS_PER_BLOCK; ++scan)
    {
        raw[scan * AURORA_ADC_CHANNEL_COUNT + 0U] = pv_i_code;
        raw[scan * AURORA_ADC_CHANNEL_COUNT + 1U] = 110U;
        raw[scan * AURORA_ADC_CHANNEL_COUNT + 2U] = 120U;
        raw[scan * AURORA_ADC_CHANNEL_COUNT + 3U] = 130U;
        raw[scan * AURORA_ADC_CHANNEL_COUNT + 4U] = 1500U;
        raw[scan * AURORA_ADC_CHANNEL_COUNT + 5U] = 1500U;
    }
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_measurement_and_zero_calibration(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证完整DMA块发布、NTC有效位和32块PV_I运行时零点校准。
 *---------------------------------------------------------------------------*/
static void test_measurement_and_zero_calibration(void)
{
    aurora_measurement_ctx_t ctx;
    aurora_measurement_t out;
    aurora_measurement_calibration_t c = unit_calibration();
    uint16_t raw[AURORA_ADC_BLOCK_WORDS];
    uint32_t i;

    fill_adc_block(raw, 100U);
    raw[0] = 0U;
    raw[(AURORA_ADC_SCANS_PER_BLOCK - 1U) * AURORA_ADC_CHANNEL_COUNT] = 4095U;
    aurora_measurement_init(&ctx, &c);
    CHECK(aurora_measurement_process_block(&ctx, raw, AURORA_ADC_BLOCK_WORDS, 10U) ==
          AURORA_STATUS_OK);
    CHECK(aurora_measurement_read(&ctx, &out));
    CHECK(out.pv_current_ma == 100);
    CHECK(out.pv_voltage_mv == 110);
    CHECK((out.valid_mask & AURORA_MEAS_VALID_PV_POWER) != 0U);
    CHECK((out.valid_mask & AURORA_MEAS_VALID_MOS_TEMP) != 0U);
    CHECK((out.valid_mask & AURORA_MEAS_VALID_AMB_TEMP) != 0U);

    aurora_measurement_zero_cal_reset(&ctx);
    fill_adc_block(raw, 100U);
    for (i = 0U; i < (AURORA_ZERO_CAL_BLOCKS - 1U); ++i)
    {
        CHECK(aurora_measurement_zero_cal_accumulate(&ctx, raw,
                                                     AURORA_ADC_BLOCK_WORDS) ==
              AURORA_STATUS_BUSY);
    }
    CHECK(aurora_measurement_zero_cal_accumulate(&ctx, raw,
                                                 AURORA_ADC_BLOCK_WORDS) ==
          AURORA_STATUS_OK);
    CHECK(aurora_measurement_zero_cal_ready(&ctx));
    CHECK(!aurora_measurement_zero_cal_failed(&ctx));
    CHECK(ctx.calibration.channel[0].zero_code == 100);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_protocol_roundtrip(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证旧协议设置帧编码/解析保持无损，并验证连续两帧不会互相覆盖。
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
    CHECK(rx.data[0] == AURORA_CHEM_LFP);
    CHECK(rx.data[1] == AURORA_PACK_60V);

    tx.message_id = 9U;
    length = aurora_protocol_encode(&tx, wire, sizeof(wire));
    for (i = 0U; i < length; ++i)
    {
        aurora_protocol_feed_byte(&parser, wire[i], (uint32_t)(100U + i));
    }
    CHECK(aurora_protocol_take_frame(&parser, &rx));
    CHECK(rx.message_id == 9U);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_telemetry_legacy_identity(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证旧遥测固定长度、故障码、版本和型号字段保持兼容。
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
 * Name        : static void test_all_v27_battery_profiles(void)
 * Input       : 无
 * Output      : 无
 * Description : 逐体系/档位核对V2.7关键充电目标、保护点和铅酸Float参数，防止表格迁移错位。
 *---------------------------------------------------------------------------*/
static void test_all_v27_battery_profiles(void)
{
    static const uint32_t cv[AURORA_CHEM_COUNT][AURORA_PACK_COUNT] =
    {
        {58000U, 72500U, 87000U},
        {54600U, 71400U, 84000U},
        {57600U, 72000U, 86400U},
        {56100U, 72600U, 85800U}
    };
    static const uint32_t uv[AURORA_CHEM_COUNT][AURORA_PACK_COUNT] =
    {
        {41500U, 52000U, 62500U},
        {35100U, 45900U, 54000U},
        {40000U, 50000U, 60000U},
        {30600U, 39600U, 46800U}
    };
    static const uint32_t ov[AURORA_CHEM_COUNT][AURORA_PACK_COUNT] =
    {
        {58800U, 73400U, 88000U},
        {56875U, 74375U, 87500U},
        {60000U, 75000U, 90000U},
        {58650U, 75900U, 89700U}
    };
    aurora_charge_profile_t p;
    size_t chem;
    size_t pack;

    for (chem = 0U; chem < AURORA_CHEM_COUNT; ++chem)
    {
        for (pack = 0U; pack < AURORA_PACK_COUNT; ++pack)
        {
            CHECK(aurora_charge_profile_get((aurora_battery_chem_t)chem,
                                            (aurora_battery_pack_t)pack,
                                            &p));
            CHECK(p.cv_target_mv == cv[chem][pack]);
            CHECK(p.battery_uv_mv == uv[chem][pack]);
            CHECK(p.ov_slow_mv == ov[chem][pack]);
            CHECK(p.cc_current_ma == 3000U);
            CHECK(p.trickle_current_ma == 1000U);
            CHECK(p.tail_current_ma == 300U);
            CHECK(p.ov_absolute_mv == 93000U);
        }
    }

    CHECK(aurora_charge_profile_get(AURORA_CHEM_LEAD, AURORA_PACK_48V, &p));
    CHECK(p.float_target_mv == 54600U);
    CHECK(p.float_min_mv == 54400U);
    CHECK(p.float_max_mv == 54800U);
    CHECK(p.float_end_current_ma == 80U);
    CHECK(aurora_charge_profile_get(AURORA_CHEM_LEAD, AURORA_PACK_60V, &p));
    CHECK(p.float_target_mv == 68300U);
    CHECK(aurora_charge_profile_get(AURORA_CHEM_LEAD, AURORA_PACK_72V, &p));
    CHECK(p.float_target_mv == 82000U);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_charger_estimated_current_and_tail_gating(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证CC输出电池侧目标，并验证弱光/温度/输入限幅时不能用估算尾流误判充满。
 *---------------------------------------------------------------------------*/
static void test_charger_estimated_current_and_tail_gating(void)
{
    aurora_charger_ctx_t c;
    aurora_measurement_t s;
    aurora_charge_output_t o;

    memset(&s, 0, sizeof(s));
    s.valid_mask = AURORA_MEAS_VALID_BAT_V | AURORA_MEAS_VALID_BAT_I_EST;
    s.battery_voltage_mv = 75000;
    s.battery_current_est_ma = 2500;
    s.battery_current_quality = AURORA_MEAS_QUALITY_ESTIMATED;
    aurora_charger_init(&c, AURORA_CHEM_LEAD, AURORA_PACK_72V, 0U);
    o = aurora_charger_step(&c, &s, false, false, false, 10U);
    CHECK(o.state == AURORA_CHARGE_CC);
    CHECK(o.allow_charge);
    CHECK(o.current_target_ma == 3000U);
    CHECK(o.battery_power_target_mw > 0U);

    /* 直接构造CV低尾流；输入限幅时tail timer必须保持0。 */
    c.state = AURORA_CHARGE_CV;
    c.state_since_ms = 20U;
    s.battery_voltage_mv = 87000;
    s.battery_current_est_ma = 200;
    o = aurora_charger_step(&c, &s, false, false, true, 100U);
    CHECK(o.state == AURORA_CHARGE_CV);
    CHECK(c.tail_since_ms == 0U);

    o = aurora_charger_step(&c, &s, false, false, false, 101U);
    CHECK(c.tail_since_ms == 101U);
    o = aurora_charger_step(&c, &s, false, false, false,
                            101U + AURORA_TAIL_HOLD_MS);
    CHECK(o.state == AURORA_CHARGE_FLOAT);

    c.state = (aurora_charge_state_t)0x7FU;
    o = aurora_charger_step(&c, &s, false, false, false, 2000U);
    CHECK(o.state == AURORA_CHARGE_FAULT);
    CHECK(!o.allow_charge);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_timed_protection(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证V2.7真实毫秒PV欠压/恢复、3ms BAT快速过压、100ms PV OCP和105°C温度保护。
 *---------------------------------------------------------------------------*/
static void test_timed_protection(void)
{
    aurora_protection_ctx_t p;
    aurora_charge_profile_t profile;
    aurora_measurement_t s;

    CHECK(aurora_charge_profile_get(AURORA_CHEM_LEAD, AURORA_PACK_48V, &profile));
    s = valid_sample(7000, 1000, 50000, 50000, 1U);
    aurora_protection_init(&p, 0U);
    protection_step_at(&p, &s, &profile, 1U);
    CHECK((aurora_protection_fault_mask(&p) & AURORA_FAULT_PV_UNDERVOLT) == 0U);
    protection_step_at(&p, &s, &profile, 1001U);
    CHECK((aurora_protection_fault_mask(&p) & AURORA_FAULT_PV_UNDERVOLT) != 0U);

    s.pv_voltage_mv = 10000;
    s.pv_power_mw = 10000;
    protection_step_at(&p, &s, &profile, 1002U);
    protection_step_at(&p, &s, &profile, 2002U);
    CHECK((aurora_protection_fault_mask(&p) & AURORA_FAULT_PV_UNDERVOLT) == 0U);

    /* 48V快速软件BAT OV：>61.8V连续3ms。 */
    s = valid_sample(20000, 1000, 62000, 62000, 3000U);
    protection_step_at(&p, &s, &profile, 3000U);
    CHECK((aurora_protection_fault_mask(&p) & AURORA_FAULT_BAT_OVERVOLT) == 0U);
    protection_step_at(&p, &s, &profile, 3003U);
    CHECK((aurora_protection_fault_mask(&p) & AURORA_FAULT_BAT_OVERVOLT) != 0U);
    s.battery_voltage_mv = 58000;
    protection_step_at(&p, &s, &profile, 3004U);
    protection_step_at(&p, &s, &profile, 5504U);
    CHECK((aurora_protection_fault_mask(&p) & AURORA_FAULT_BAT_OVERVOLT) == 0U);

    /* 300W候选基础12A的1.5倍=18A，连续100ms软件OCP。 */
    s = valid_sample(20000, 19000, 50000, 50000, 6000U);
    protection_step_at(&p, &s, &profile, 6000U);
    protection_step_at(&p, &s, &profile, 6100U);
    CHECK((aurora_protection_fault_mask(&p) & AURORA_FAULT_PV_OVERCURRENT) != 0U);

    /* 95°C只是降额起点，不是保护；>105°C持续1s才保护。 */
    aurora_protection_init(&p, 7000U);
    s = valid_sample(20000, 1000, 50000, 50000, 7000U);
    s.mos_temp_dC = 960;
    protection_step_at(&p, &s, &profile, 7000U);
    protection_step_at(&p, &s, &profile, 8000U);
    CHECK((aurora_protection_fault_mask(&p) & AURORA_FAULT_MOS_OVERTEMP) == 0U);
    s.mos_temp_dC = 1060;
    protection_step_at(&p, &s, &profile, 8001U);
    protection_step_at(&p, &s, &profile, 9001U);
    CHECK((aurora_protection_fault_mask(&p) & AURORA_FAULT_MOS_OVERTEMP) != 0U);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_software_ocp_requires_active_pwm(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证PWM尚未真正输出时PV软件OCP/过功率不计时；物理PWM有效后才按V2.7时间触发。
 *---------------------------------------------------------------------------*/
static void test_software_ocp_requires_active_pwm(void)
{
    aurora_protection_ctx_t p;
    aurora_charge_profile_t profile;
    aurora_measurement_t s;

    CHECK(aurora_charge_profile_get(AURORA_CHEM_LEAD, AURORA_PACK_48V, &profile));
    aurora_protection_init(&p, 0U);
    s = valid_sample(20000, 19000, 50000, 50000, 0U);

    s.timestamp_ms = 1U;
    aurora_protection_step(&p, &s, &profile, true, false, 1U);
    s.timestamp_ms = 1001U;
    aurora_protection_step(&p, &s, &profile, true, false, 1001U);
    CHECK((aurora_protection_fault_mask(&p) & AURORA_FAULT_PV_OVERCURRENT) == 0U);

    s.timestamp_ms = 2000U;
    aurora_protection_step(&p, &s, &profile, true, true, 2000U);
    s.timestamp_ms = 2100U;
    aurora_protection_step(&p, &s, &profile, true, true, 2100U);
    CHECK((aurora_protection_fault_mask(&p) & AURORA_FAULT_PV_OVERCURRENT) != 0U);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_fault_releases_relay_with_invalid_measurement(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证FAULT期间即使ADC关键测量同时失效，也必须在20ms放能后释放继电器，不能被缺测早退挡住。
 *---------------------------------------------------------------------------*/
static void test_fault_releases_relay_with_invalid_measurement(void)
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
    ctx.state = AURORA_POWER_FAULT;
    ctx.state_since_ms = 100U;
    ctx.relay_closed = true;

    command = aurora_power_stage_step(&ctx, &sample, &mppt, &charger,
                                      false, false, false, 110U);
    CHECK(command.state == AURORA_POWER_FAULT);
    CHECK(command.relay_enable);
    CHECK(!command.pwm_enable);

    command = aurora_power_stage_step(&ctx, &sample, &mppt, &charger,
                                      false, false, false,
                                      100U + AURORA_RELAY_FAULT_RELEASE_MS);
    CHECK(command.state == AURORA_POWER_FAULT);
    CHECK(!command.relay_enable);
    CHECK(!command.pwm_enable);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_strict_precharge_and_battery_stability(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证继电器在BST_U远离BAT_U时绝不闭合；只有压差稳定1s、机械复核和BAT_U稳定10s后才进入RUN。
 *---------------------------------------------------------------------------*/
static void test_strict_precharge_and_battery_stability(void)
{
    aurora_power_stage_ctx_t ctx;
    aurora_measurement_t sample = valid_sample(16000, 1000, 48000, 30000, 0U);
    aurora_mppt_output_t mppt;
    aurora_charge_output_t charger;
    aurora_power_command_t command;

    memset(&mppt, 0, sizeof(mppt));
    memset(&charger, 0, sizeof(charger));
    mppt.valid = true;
    mppt.theoretical_power_mw = 50000U;
    charger.allow_charge = true;
    charger.pv_power_limit_mw = 100000U;

    aurora_power_stage_init(&ctx, 0U);
    command = aurora_power_stage_step(&ctx, &sample, &mppt, &charger,
                                      true, false, false, 1U);
    CHECK(command.state == AURORA_POWER_WAIT_PV);
    CHECK(!command.relay_enable);

    command = aurora_power_stage_step(&ctx, &sample, &mppt, &charger,
                                      true, false, false,
                                      1U + AURORA_PV_START_QUALIFY_MS);
    CHECK(command.state == AURORA_POWER_START_DELAY);
    CHECK(!command.relay_enable);

    command = aurora_power_stage_step(&ctx, &sample, &mppt, &charger,
                                      true, false, false,
                                      1U + AURORA_PV_START_QUALIFY_MS +
                                      AURORA_START_DELAY_MIN_MS);
    CHECK(command.state == AURORA_POWER_ZERO_CAL);
    command = aurora_power_stage_step(&ctx, &sample, &mppt, &charger,
                                      true, true, false, 2001U);
    CHECK(command.state == AURORA_POWER_WAIT_BATTERY);
    command = aurora_power_stage_step(&ctx, &sample, &mppt, &charger,
                                      true, true, false, 2002U);
    CHECK(command.state == AURORA_POWER_PRECHARGE);

    /* BST_U只有30V、BAT_U为48V：必须Boost预充且继电器保持断开。 */
    command = aurora_power_stage_step(&ctx, &sample, &mppt, &charger,
                                      true, true, false, 2003U);
    CHECK(command.state == AURORA_POWER_PRECHARGE);
    CHECK(command.pwm_enable);
    CHECK(!command.relay_enable);

    /* 压差进入1.5V内后还必须连续稳定1s，不能交越瞬间吸合。 */
    sample.bus_voltage_mv = 47500;
    command = aurora_power_stage_step(&ctx, &sample, &mppt, &charger,
                                      true, true, false, 2004U);
    CHECK(!command.relay_enable);
    command = aurora_power_stage_step(&ctx, &sample, &mppt, &charger,
                                      true, true, false, 3004U);
    CHECK(command.state == AURORA_POWER_RELAY_SETTLE);
    CHECK(command.relay_enable);
    CHECK(!command.pwm_enable);
    CHECK(command.duty_q15 == 0U);

    /* 机械稳定后仍不允许发波，先进入10s BAT_U稳定窗口。 */
    command = aurora_power_stage_step(&ctx, &sample, &mppt, &charger,
                                      true, true, false, 3104U);
    CHECK(command.state == AURORA_POWER_BAT_STABILITY);
    CHECK(command.relay_enable);
    CHECK(!command.pwm_enable);

    sample.battery_voltage_mv = 49000;
    sample.bus_voltage_mv = 49000;
    command = aurora_power_stage_step(&ctx, &sample, &mppt, &charger,
                                      true, true, false, 8104U);
    CHECK(command.state == AURORA_POWER_BAT_STABILITY);
    sample.battery_voltage_mv = 48000;
    sample.bus_voltage_mv = 48000;
    command = aurora_power_stage_step(&ctx, &sample, &mppt, &charger,
                                      true, true, false, 13104U);
    CHECK(command.state == AURORA_POWER_RUN);
    CHECK(command.relay_enable);
    CHECK(!command.pwm_enable);

    /* 下一拍RUN才允许请求功率，避免继电器闭合与PWM同一拍发生。 */
    command = aurora_power_stage_step(&ctx, &sample, &mppt, &charger,
                                      true, true, false, 13105U);
    CHECK(command.pwm_enable);
    CHECK(command.duty_q15 <= AURORA_DUTY_STEP_Q15);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_full_battery_is_not_no_sun(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证电池完成充电但PV仍高时不会把charger禁止误认为无太阳并在30min后断继电器。
 *---------------------------------------------------------------------------*/
static void test_full_battery_is_not_no_sun(void)
{
    aurora_power_stage_ctx_t ctx;
    aurora_measurement_t sample = valid_sample(30000, 500, 58000, 58000, 0U);
    aurora_mppt_output_t mppt;
    aurora_charge_output_t charger;
    aurora_power_command_t command;

    memset(&ctx, 0, sizeof(ctx));
    memset(&mppt, 0, sizeof(mppt));
    memset(&charger, 0, sizeof(charger));
    ctx.state = AURORA_POWER_RUN;
    ctx.relay_closed = true;
    ctx.dynamic_start_delay_ms = AURORA_START_DELAY_MIN_MS;
    charger.allow_charge = false;
    mppt.valid = false;

    command = aurora_power_stage_step(&ctx, &sample, &mppt, &charger,
                                      true, true, false, 1U);
    CHECK(command.state == AURORA_POWER_RUN);
    CHECK(command.relay_enable);
    command = aurora_power_stage_step(&ctx, &sample, &mppt, &charger,
                                      true, true, false,
                                      AURORA_NO_SUN_OPEN_RELAY_MS + 1000U);
    CHECK(command.state == AURORA_POWER_RUN);
    CHECK(command.relay_enable);

    /* 只有PV<13V且MPPT不运行才开始30min真正无发电计时。 */
    sample.pv_voltage_mv = 12000;
    sample.pv_power_mw = 0;
    command = aurora_power_stage_step(&ctx, &sample, &mppt, &charger,
                                      true, true, false, 2000000U);
    CHECK(command.relay_enable);
    command = aurora_power_stage_step(&ctx, &sample, &mppt, &charger,
                                      true, true, false,
                                      2000000U + AURORA_NO_SUN_OPEN_RELAY_MS);
    CHECK(command.state == AURORA_POWER_NO_SUN);
    CHECK(!command.relay_enable);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_comparator_startup_semantics(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证PWM未实际输出时CMP只做诊断不软件锁存；真正输出后CMP必须走快速故障链。
 *---------------------------------------------------------------------------*/
static void test_comparator_startup_semantics(void)
{
    aurora_service_t service;
    uint32_t sequence;

    mock_reset();
    CHECK(aurora_service_init(&service));
    aurora_service_isr_comparator_fault(&service, AURORA_FAULT_FAST_MOS_OCP);
    CHECK(service.startup_comp_ignored_count == 1U);
    CHECK(service.pending_fault_mask == 0U);
    CHECK((aurora_protection_fault_mask(&service.app.protection) &
           AURORA_FAULT_FAST_MOS_OCP) == 0U);

    /* Host直接模拟已经完成0CCR自然UEV并放行PWM。 */
    CHECK(drv_pwm_prepare_arm_zero(&sequence));
    mock_apply_uev();
    CHECK(drv_pwm_arm());
    CHECK(mock_pwm_active());
    aurora_service_isr_comparator_fault(&service, AURORA_FAULT_FAST_MOS_OCP);
    CHECK(!mock_pwm_active());
    CHECK((service.pending_fault_mask & AURORA_FAULT_FAST_MOS_OCP) != 0U);
    aurora_service_poll(&service);
    CHECK((service.app.protection.latched_mask & AURORA_FAULT_FAST_MOS_OCP) != 0U);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_service_relay_transition_forces_pwm_off(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证任何物理继电器切换前Service都会先撤销PWM，防止预充闭合时仍在发波。
 *---------------------------------------------------------------------------*/
static void test_service_relay_transition_forces_pwm_off(void)
{
    aurora_service_t service;
    uint32_t sequence;

    mock_reset();
    CHECK(aurora_service_init(&service));
    CHECK(drv_pwm_prepare_arm_zero(&sequence));
    mock_apply_uev();
    CHECK(drv_pwm_arm());
    CHECK(mock_pwm_active());

    service.app.sample.valid_mask = AURORA_MEAS_VALID_BAT_V | AURORA_MEAS_VALID_BUS_V;
    service.app.sample.battery_voltage_mv = 48000;
    service.app.sample.bus_voltage_mv = 47500;
    service.app.power_command.state = AURORA_POWER_RELAY_SETTLE;
    service.app.power_command.relay_enable = true;
    service.app.power_command.pwm_enable = false;
    aurora_service_poll(&service);
    CHECK(mock_relay());
    CHECK(!mock_pwm_active());

    /* 重新断开后制造18V压差；即使APP错误请求闭合，Service也必须拒绝。 */
    service.app.power_command.relay_enable = false;
    aurora_service_poll(&service);
    CHECK(!mock_relay());
    service.app.sample.bus_voltage_mv = 30000;
    service.app.power_command.state = AURORA_POWER_RELAY_SETTLE;
    service.app.power_command.relay_enable = true;
    aurora_service_poll(&service);
    CHECK(!mock_relay());
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_watchdog_window_and_adc_overrun(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证健康票据齐全才喂狗，并验证同半DMA覆盖会锁存ADC_OVERRUN。
 *---------------------------------------------------------------------------*/
static void test_watchdog_window_and_adc_overrun(void)
{
    aurora_service_t service;

    mock_reset();
    CHECK(aurora_service_init(&service));
    mock_advance_ms(AURORA_WATCHDOG_STARTUP_GRACE_MS + AURORA_WATCHDOG_WINDOW_MS);
    aurora_service_poll(&service);
    CHECK(mock_watchdog_feeds() == 0U);
    run_service_ticks(&service, AURORA_WATCHDOG_WINDOW_MS);
    CHECK(mock_watchdog_feeds() == 1U);

    mock_reset();
    CHECK(aurora_service_init(&service));
    aurora_service_isr_adc_block(&service, 0U);
    aurora_service_isr_adc_block(&service, 0U);
    CHECK(service.adc_overrun_count == 1U);
    aurora_service_poll(&service);
    CHECK((service.app.protection.latched_mask & AURORA_FAULT_ADC_OVERRUN) != 0U);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_pwm_arm_race(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证PWM首次arm必须等待自然UEV且Break可在任一时刻阻断输出，Duty不会绕过shadow语义。
 *---------------------------------------------------------------------------*/
static void test_pwm_arm_race(void)
{
    uint32_t sequence;

    mock_reset();
    CHECK(drv_pwm_init());
    CHECK(drv_pwm_prepare_arm_zero(&sequence));
    CHECK(drv_pwm_applied_sequence() < sequence);
    mock_apply_uev();
    drv_pwm_update_isr_ack();
    CHECK(drv_pwm_applied_sequence() >= sequence);
    CHECK(drv_pwm_arm());
    CHECK(mock_pwm_active());

    mock_set_break(true);
    CHECK(!mock_pwm_active());
    CHECK(!drv_pwm_arm());
    drv_pwm_force_off_isr();
    CHECK(!mock_pwm_active());
}


/*---------------------------------------------------------------------------*
 * Name        : static void test_v090_ntc_and_zero_quality(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证NTC开/短物理方向、25°C量级换算，以及PV_I零点块间spread超标会拒绝启动。
 *---------------------------------------------------------------------------*/
static void test_v090_ntc_and_zero_quality(void)
{
    aurora_measurement_ctx_t ctx;
    aurora_measurement_t out;
    aurora_measurement_calibration_t c = unit_calibration();
    uint16_t raw[AURORA_ADC_BLOCK_WORDS];
    uint32_t i;

    fill_adc_block(raw, 100U);
    for (i = 0U; i < AURORA_ADC_SCANS_PER_BLOCK; ++i)
    {
        raw[i * AURORA_ADC_CHANNEL_COUNT + 4U] = 4094U;
        raw[i * AURORA_ADC_CHANNEL_COUNT + 5U] = 0U;
    }
    aurora_measurement_init(&ctx, &c);
    CHECK(aurora_measurement_process_block(&ctx, raw, AURORA_ADC_BLOCK_WORDS, 10U) ==
          AURORA_STATUS_OK);
    CHECK(aurora_measurement_read(&ctx, &out));
    CHECK(out.mos_ntc_status == AURORA_NTC_STATUS_OPEN);
    CHECK(out.ambient_ntc_status == AURORA_NTC_STATUS_SHORT);
    CHECK((out.valid_mask & AURORA_MEAS_VALID_MOS_TEMP) == 0U);
    CHECK((out.valid_mask & AURORA_MEAS_VALID_AMB_TEMP) == 0U);

    fill_adc_block(raw, 100U);
    for (i = 0U; i < AURORA_ADC_SCANS_PER_BLOCK; ++i)
    {
        raw[i * AURORA_ADC_CHANNEL_COUNT + 4U] = 3897U;
        raw[i * AURORA_ADC_CHANNEL_COUNT + 5U] = 3897U;
    }
    CHECK(aurora_measurement_process_block(&ctx, raw, AURORA_ADC_BLOCK_WORDS, 20U) ==
          AURORA_STATUS_OK);
    CHECK(aurora_measurement_read(&ctx, &out));
    CHECK(out.mos_ntc_status == AURORA_NTC_STATUS_OK);
    CHECK(out.ambient_ntc_status == AURORA_NTC_STATUS_OK);
    CHECK(out.mos_temp_dC >= 240 && out.mos_temp_dC <= 260);

    aurora_measurement_zero_cal_reset(&ctx);
    for (i = 0U; i < AURORA_ZERO_CAL_MAX_ATTEMPT_BLOCKS; ++i)
    {
        fill_adc_block(raw, (uint16_t)((i & 1U) ? 100U : 150U));
        if (i + 1U < AURORA_ZERO_CAL_MAX_ATTEMPT_BLOCKS)
        {
            CHECK(aurora_measurement_zero_cal_accumulate(&ctx, raw,
                                                         AURORA_ADC_BLOCK_WORDS) ==
                  AURORA_STATUS_BUSY);
        }
        else
        {
            CHECK(aurora_measurement_zero_cal_accumulate(&ctx, raw,
                                                         AURORA_ADC_BLOCK_WORDS) ==
                  AURORA_STATUS_INVALID);
        }
    }
    CHECK(aurora_measurement_zero_cal_failed(&ctx));
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_v090_lead_temp_comp_and_mature_timing(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证铅酸温补安全钳制、TC去抖、Float低流60s和Recharge 1s成熟时间行为。
 *---------------------------------------------------------------------------*/
static void test_v090_lead_temp_comp_and_mature_timing(void)
{
    aurora_charger_ctx_t c;
    aurora_measurement_t s;
    aurora_charge_output_t o;

    memset(&s, 0, sizeof(s));
    s.valid_mask = AURORA_MEAS_VALID_BAT_V | AURORA_MEAS_VALID_AMB_TEMP |
                   AURORA_MEAS_VALID_BAT_I_EST;
    s.ambient_ntc_status = AURORA_NTC_STATUS_OK;
    s.battery_current_quality = AURORA_MEAS_QUALITY_ESTIMATED;
    s.battery_current_est_ma = 1000;

    aurora_charger_init(&c, AURORA_CHEM_LEAD, AURORA_PACK_48V, 0U);
    s.ambient_temp_dC = 550;
    s.battery_voltage_mv = 50000;
    (void)aurora_charger_step(&c, &s, false, false, false, 1U);
    CHECK(c.profile.cv_target_mv == 55840U);
    CHECK(c.profile.float_target_mv == 52440U);

    aurora_charger_init(&c, AURORA_CHEM_LEAD, AURORA_PACK_48V, 0U);
    s.ambient_temp_dC = -200;
    s.battery_voltage_mv = 50000;
    (void)aurora_charger_step(&c, &s, false, false, false, 1U);
    CHECK(c.profile.cv_max_mv <= c.profile.ov_fast_mv - AURORA_LEAD_TEMP_COMP_FAST_OV_GUARD_MV);

    aurora_charger_init(&c, AURORA_CHEM_LEAD, AURORA_PACK_72V, 0U);
    s.ambient_temp_dC = 250;
    s.battery_voltage_mv = 71000;
    o = aurora_charger_step(&c, &s, false, false, false, 1U);
    CHECK(o.state == AURORA_CHARGE_TRICKLE);
    s.battery_voltage_mv = 73000;
    o = aurora_charger_step(&c, &s, false, false, false, 10U);
    CHECK(o.state == AURORA_CHARGE_TRICKLE);
    o = aurora_charger_step(&c, &s, false, false, false,
                            10U + AURORA_TRICKLE_TO_CC_HOLD_MS);
    CHECK(o.state == AURORA_CHARGE_CC);

    c.state = AURORA_CHARGE_FLOAT;
    c.float_started = true;
    c.float_start_ms = 1000U;
    c.tail_since_ms = 0U;
    s.battery_voltage_mv = 82000;
    s.battery_current_est_ma = 50;
    o = aurora_charger_step(&c, &s, false, false, false, 2000U);
    CHECK(o.state == AURORA_CHARGE_FLOAT);
    o = aurora_charger_step(&c, &s, false, false, false,
                            2000U + AURORA_TAIL_HOLD_MS);
    CHECK(o.state == AURORA_CHARGE_FLOAT);
    o = aurora_charger_step(&c, &s, false, false, false,
                            2000U + AURORA_FLOAT_END_HOLD_MS);
    CHECK(o.state == AURORA_CHARGE_COMPLETE);

    c.state = AURORA_CHARGE_COMPLETE;
    c.recharge_since_ms = 0U;
    s.battery_voltage_mv = 76000;
    o = aurora_charger_step(&c, &s, false, false, false, 70000U);
    CHECK(o.state == AURORA_CHARGE_COMPLETE);
    o = aurora_charger_step(&c, &s, false, false, false,
                            70000U + AURORA_RECHARGE_HOLD_MS);
    CHECK(o.state == AURORA_CHARGE_CC);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_v090_bus_saturation_and_current_plausibility(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证BST_U近满量程撤销BUS有效位并锁故障，以及PV_I运行/停机合理性诊断。
 *---------------------------------------------------------------------------*/
static void test_v090_bus_saturation_and_current_plausibility(void)
{
    aurora_measurement_ctx_t m;
    aurora_measurement_calibration_t c = unit_calibration();
    aurora_measurement_t s;
    aurora_protection_ctx_t p;
    aurora_charge_profile_t profile;
    uint16_t raw[AURORA_ADC_BLOCK_WORDS];
    size_t i;

    fill_adc_block(raw, 100U);
    for (i = 0U; i < AURORA_ADC_SCANS_PER_BLOCK; ++i)
    {
        raw[i * AURORA_ADC_CHANNEL_COUNT + 3U] =
            (uint16_t)(AURORA_ADC_NEAR_FULL_SCALE_CODE + 1U);
        raw[i * AURORA_ADC_CHANNEL_COUNT + 4U] = 3897U;
        raw[i * AURORA_ADC_CHANNEL_COUNT + 5U] = 3897U;
    }
    aurora_measurement_init(&m, &c);
    CHECK(aurora_measurement_process_block(&m, raw, AURORA_ADC_BLOCK_WORDS, 10U) ==
          AURORA_STATUS_OK);
    CHECK(aurora_measurement_read(&m, &s));
    CHECK((s.diagnostic_mask & AURORA_MEAS_DIAG_BUS_ADC_SATURATED) != 0U);
    CHECK((s.valid_mask & AURORA_MEAS_VALID_BUS_V) == 0U);

    CHECK(aurora_charge_profile_get(AURORA_CHEM_LEAD, AURORA_PACK_48V, &profile));
    aurora_protection_init(&p, 0U);
    aurora_protection_step(&p, &s, &profile, true, false, 10U);
    CHECK((aurora_protection_fault_mask(&p) & AURORA_FAULT_BUS_ADC_SATURATION) != 0U);

    aurora_protection_init(&p, 0U);
    s = valid_sample(20000, -1200, 50000, 50000, 0U);
    protection_step_at(&p, &s, &profile, 1U);
    protection_step_at(&p, &s, &profile, 11U);
    CHECK((aurora_protection_fault_mask(&p) & AURORA_FAULT_PV_CURRENT_PLAUSIBILITY) != 0U);

    aurora_protection_init(&p, 0U);
    s = valid_sample(20000, 3500, 50000, 50000, 0U);
    s.timestamp_ms = 1U;
    aurora_protection_step(&p, &s, &profile, false, false, 1U);
    s.timestamp_ms = 1501U;
    aurora_protection_step(&p, &s, &profile, false, false, 1501U);
    CHECK((aurora_protection_fault_mask(&p) & AURORA_FAULT_PV_CURRENT_PLAUSIBILITY) == 0U);

    aurora_protection_init(&p, 0U);
    s = valid_sample(20000, 3500, 50000, 50000, 0U);
    s.timestamp_ms = 1U;
    aurora_protection_step(&p, &s, &profile, true, false, 1U);
    s.timestamp_ms = 1501U;
    aurora_protection_step(&p, &s, &profile, true, false, 1501U);
    CHECK((aurora_protection_fault_mask(&p) & AURORA_FAULT_PV_CURRENT_PLAUSIBILITY) != 0U);
}

/*---------------------------------------------------------------------------*
 * Name        : int main(void)
 * Input       : 无
 * Output      : 0表示全部Host回归通过
 * Description : 运行协议、测量、V2.7电池/保护、严格预充、CMP、PWM、看门狗和Flash回归，并输出断言总数。
 *---------------------------------------------------------------------------*/
int main(void)
{
    test_measurement_and_zero_calibration();
    test_v090_ntc_and_zero_quality();
    test_v090_lead_temp_comp_and_mature_timing();
    test_v090_bus_saturation_and_current_plausibility();
    test_protocol_roundtrip();
    test_telemetry_legacy_identity();
    test_storage_atomic_format();
    test_mppt_reference_search();
    test_all_v27_battery_profiles();
    test_charger_estimated_current_and_tail_gating();
    test_timed_protection();
    test_software_ocp_requires_active_pwm();
    test_fault_releases_relay_with_invalid_measurement();
    test_strict_precharge_and_battery_stability();
    test_full_battery_is_not_no_sun();
    test_comparator_startup_semantics();
    test_service_relay_transition_forces_pwm_off();
    test_watchdog_window_and_adc_overrun();
    test_pwm_arm_race();
    printf("Aurora v0.9.0 host tests: %u assertions passed.\n", g_assertions);
    return 0;
}
