#include "main.h"

#include "driver.h"
#include "mock_driver.h"

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
 * Description : 构造v0.10.3行为测试使用的最小有效测量标定。
 *---------------------------------------------------------------------------*/
static aurora_measurement_calibration_t unit_calibration(void)
{
    aurora_measurement_calibration_t calibration;
    size_t index;

    memset(&calibration, 0, sizeof(calibration));
    for (index = 0U; index < AURORA_ADC_CHANNEL_COUNT; ++index)
    {
        calibration.channel[index].gain_num = 1;
        calibration.channel[index].gain_den = 1;
        calibration.channel[index].polarity = 1;
        calibration.channel[index].valid = true;
    }
    return calibration;
}

/*---------------------------------------------------------------------------*
 * Name        : static aurora_measurement_t valid_sample(uint32_t sequence,
 *               uint32_t now_ms)
 * Input       : sequence - 测量序号；now_ms - 时间戳
 * Output      : 功率状态机和能量统计可使用的完整样本
 * Description : 构造16V PV、48V BAT、47.5V BST与1A PV电流的稳定测试点。
 *---------------------------------------------------------------------------*/
static aurora_measurement_t valid_sample(uint32_t sequence, uint32_t now_ms)
{
    aurora_measurement_t sample;

    memset(&sample, 0, sizeof(sample));
    sample.sequence = sequence;
    sample.timestamp_ms = now_ms;
    sample.valid_mask = AURORA_MEAS_VALID_PV_V | AURORA_MEAS_VALID_PV_I |
                        AURORA_MEAS_VALID_BAT_V | AURORA_MEAS_VALID_BUS_V |
                        AURORA_MEAS_VALID_PV_POWER | AURORA_MEAS_VALID_MOS_TEMP |
                        AURORA_MEAS_VALID_AMB_TEMP;
    sample.pv_voltage_mv = 16000;
    sample.pv_current_ma = 1000;
    sample.pv_power_mw = 16000;
    sample.battery_voltage_mv = 48000;
    sample.bus_voltage_mv = 47500;
    sample.mos_temp_dC = 250;
    sample.ambient_temp_dC = 250;
    sample.mos_ntc_status = AURORA_NTC_STATUS_OK;
    sample.ambient_ntc_status = AURORA_NTC_STATUS_OK;
    return sample;
}

/*---------------------------------------------------------------------------*
 * Name        : static void put_u32_le(uint8_t *destination, uint32_t value)
 * Input       : destination - 4字节输出；value - 32位数值
 * Output      : 无
 * Description : 构造Demo配置协议载荷。
 *---------------------------------------------------------------------------*/
static void put_u32_le(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
    destination[2] = (uint8_t)(value >> 16U);
    destination[3] = (uint8_t)(value >> 24U);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_break_uses_software_arm_state(void)
 * Input       : 无
 * Output      : 无
 * Description : 模拟硬件Break先清MOE、ISR后到，验证ACTIVE仍锁存；WAIT_ZERO只记启动诊断。
 *---------------------------------------------------------------------------*/
static void test_break_uses_software_arm_state(void)
{
    aurora_runtime_t runtime;
    uint32_t sequence;

    mock_reset();
    CHECK(aurora_runtime_init(&runtime));
    CHECK(drv_pwm_prepare_arm_zero(&sequence));
    mock_apply_uev();
    CHECK(drv_pwm_arm());
    runtime.pwm_arm_state = AURORA_RUNTIME_PWM_ARM_ACTIVE;

    /* 真实硬件顺序：Break先撤销MOE，再进入软件ISR。 */
    mock_set_break(true);
    CHECK(!mock_pwm_active());
    aurora_runtime_isr_comparator_fault(&runtime, AURORA_FAULT_FAST_MOS_OCP);
    CHECK((runtime.pending_fault_mask & AURORA_FAULT_FAST_MOS_OCP) != 0U);
    CHECK(runtime.startup_comp_ignored_count == 0U);

    mock_reset();
    CHECK(aurora_runtime_init(&runtime));
    runtime.pwm_arm_state = AURORA_RUNTIME_PWM_ARM_WAIT_ZERO;
    aurora_runtime_isr_comparator_fault(&runtime, AURORA_FAULT_FAST_MOS_OCP);
    CHECK(runtime.pending_fault_mask == 0U);
    CHECK(runtime.startup_comp_ignored_count == 1U);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_relay_holdoff_and_applied_feedback(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证Battery闭合前必须20ms关波和新序号，且Runtime未应用Relay时不能计100ms稳定时间。
 *---------------------------------------------------------------------------*/
static void test_relay_holdoff_and_applied_feedback(void)
{
    aurora_power_stage_ctx_t ctx;
    aurora_measurement_t sample = valid_sample(1U, 1U);
    aurora_mppt_output_t mppt;
    aurora_charge_output_t charger;
    aurora_power_command_t command;

    memset(&mppt, 0, sizeof(mppt));
    memset(&charger, 0, sizeof(charger));
    mppt.valid = true;
    mppt.theoretical_power_mw = 50000U;
    charger.allow_charge = true;
    charger.pv_power_limit_mw = 100000U;
    charger.voltage_target_mv = 50000U;

    aurora_power_stage_init(&ctx, 0U);
    ctx.state = AURORA_POWER_PRECHARGE;
    ctx.state_since_ms = 0U;
    ctx.precharge_failure_count = 2U;

    command = aurora_power_stage_step_ex(&ctx, &sample, &mppt, &charger, true, true, false,
                                         false, AURORA_MODE_BATTERY, 48000U, 30000U, 1U);
    CHECK(command.state == AURORA_POWER_PRECHARGE);
    CHECK(!command.relay_enable);

    sample.timestamp_ms = 1001U;
    command = aurora_power_stage_step_ex(&ctx, &sample, &mppt, &charger, true, true, false,
                                         false, AURORA_MODE_BATTERY, 48000U, 30000U, 1001U);
    CHECK(command.state == AURORA_POWER_RELAY_HOLD_OFF);
    CHECK(!command.pwm_enable);
    CHECK(!command.relay_enable);

    sample.timestamp_ms = 1021U;
    command = aurora_power_stage_step_ex(&ctx, &sample, &mppt, &charger, true, true, false,
                                         false, AURORA_MODE_BATTERY, 48000U, 30000U, 1021U);
    CHECK(command.state == AURORA_POWER_RELAY_HOLD_OFF);
    CHECK(!command.relay_enable);

    sample.sequence = 2U;
    sample.timestamp_ms = 1022U;
    command = aurora_power_stage_step_ex(&ctx, &sample, &mppt, &charger, true, true, false,
                                         false, AURORA_MODE_BATTERY, 48000U, 30000U, 1022U);
    CHECK(command.state == AURORA_POWER_RELAY_SETTLE);
    CHECK(command.relay_enable);

    sample.timestamp_ms = 1070U;
    command = aurora_power_stage_step_ex(&ctx, &sample, &mppt, &charger, true, true, false,
                                         false, AURORA_MODE_BATTERY, 48000U, 30000U, 1070U);
    CHECK(command.state == AURORA_POWER_RELAY_SETTLE);

    sample.timestamp_ms = 1071U;
    command = aurora_power_stage_step_ex(&ctx, &sample, &mppt, &charger, true, true, false,
                                         true, AURORA_MODE_BATTERY, 48000U, 30000U, 1071U);
    CHECK(command.state == AURORA_POWER_RELAY_SETTLE);

    sample.timestamp_ms = 1171U;
    command = aurora_power_stage_step_ex(&ctx, &sample, &mppt, &charger, true, true, false,
                                         true, AURORA_MODE_BATTERY, 48000U, 30000U, 1171U);
    CHECK(command.state == AURORA_POWER_BAT_STABILITY);
    CHECK(ctx.precharge_failure_count == 0U);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_precharge_timeout_is_not_weak_light(void)
 * Input       : 无
 * Output      : 无
 * Description : PV电压仍在启动窗时，即使Ppv很低，预充超时也必须消耗BUS预充失败次数。
 *---------------------------------------------------------------------------*/
static void test_precharge_timeout_is_not_weak_light(void)
{
    aurora_power_stage_ctx_t ctx;
    aurora_measurement_t sample = valid_sample(1U, AURORA_PRECHARGE_TIMEOUT_MS);
    aurora_mppt_output_t mppt;
    aurora_charge_output_t charger;
    aurora_power_command_t command;

    memset(&mppt, 0, sizeof(mppt));
    memset(&charger, 0, sizeof(charger));
    sample.bus_voltage_mv = 30000;
    sample.pv_current_ma = 10;
    sample.pv_power_mw = 160;

    aurora_power_stage_init(&ctx, 0U);
    ctx.state = AURORA_POWER_PRECHARGE;
    ctx.state_since_ms = 0U;
    command = aurora_power_stage_step_ex(
        &ctx, &sample, &mppt, &charger, true, true, false, false, AURORA_MODE_BATTERY,
        48000U, 30000U, AURORA_PRECHARGE_TIMEOUT_MS);

    CHECK(command.state == AURORA_POWER_FAULT);
    CHECK(ctx.last_failure_reason == AURORA_START_FAIL_BUS_PRECHARGE_TIMEOUT);
    CHECK(ctx.precharge_failure_count == 1U);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_demo_relay_and_power_cap(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证Demo使用独立Relay条件，外部有源电压被拒，并拒绝协议配置超过30W。
 *---------------------------------------------------------------------------*/
static void test_demo_relay_and_power_cap(void)
{
    aurora_runtime_t runtime;
    aurora_app_t app;
    aurora_measurement_calibration_t calibration = unit_calibration();
    aurora_protocol_frame_t request;
    aurora_protocol_frame_t response;
    bool has_response = false;

    mock_reset();
    CHECK(aurora_runtime_init(&runtime));
    runtime.app.storage.settings.operating_mode = AURORA_MODE_DEMO_LOAD;
    runtime.app.sample = valid_sample(1U, 0U);
    runtime.app.sample.battery_voltage_mv = 0;
    runtime.app.sample.bus_voltage_mv = 0;
    runtime.app.power_command.state = AURORA_POWER_DEMO_RELAY_SETTLE;
    runtime.app.power_command.relay_enable = true;
    runtime.app.power_command.pwm_enable = false;
    aurora_runtime_poll(&runtime);
    CHECK(runtime.relay_applied);
    CHECK(mock_relay());

    runtime.app.power_command.relay_enable = false;
    aurora_runtime_poll(&runtime);
    CHECK(!mock_relay());
    runtime.app.sample.battery_voltage_mv = AURORA_DEMO_EXTERNAL_SOURCE_MAX_MV + 1L;
    runtime.app.power_command.state = AURORA_POWER_DEMO_RELAY_SETTLE;
    runtime.app.power_command.relay_enable = true;
    aurora_runtime_poll(&runtime);
    CHECK(!runtime.relay_applied);
    CHECK(!mock_relay());

    aurora_app_init(&app, &calibration, 0U);
    memset(&request, 0, sizeof(request));
    request.action = AURORA_PROTOCOL_ACTION_WRITE;
    request.resource = AURORA_PROTOCOL_RESOURCE_DEMO_CONFIG;
    request.data_length = AURORA_PROTOCOL_DEMO_CONFIG_DATA_LENGTH;
    put_u32_le(&request.data[0], 48000U);
    put_u32_le(&request.data[4], AURORA_DEMO_HARD_POWER_CAP_MW + 1U);
    aurora_app_on_protocol_frame(&app, &request, &response, &has_response, 1U);
    CHECK(has_response);
    CHECK(response.data[0] == AURORA_PROTOCOL_RESULT_INVALID);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_stale_pv_energy_is_not_accumulated(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证陈旧正功率即使缓存仍为正也不累计；新鲜样本且物理PWM有效才累计。
 *---------------------------------------------------------------------------*/
static void test_stale_pv_energy_is_not_accumulated(void)
{
    aurora_app_t stale_app;
    aurora_app_t fresh_app;
    aurora_measurement_calibration_t calibration = unit_calibration();
    aurora_measurement_t sample;

    aurora_app_init(&stale_app, &calibration, 0U);
    sample = valid_sample(1U, 0U);
    stale_app.measurement.latest = sample;
    stale_app.measurement.publish_sequence = sample.sequence;
    aurora_app_step_1ms(&stale_app, AURORA_MEASUREMENT_STALE_MS + 1U, true);
    CHECK(stale_app.energy_accumulator_mw_ms == 0ULL);

    aurora_app_init(&fresh_app, &calibration, 0U);
    sample = valid_sample(1U, 10U);
    fresh_app.measurement.latest = sample;
    fresh_app.measurement.publish_sequence = sample.sequence;
    aurora_app_step_1ms(&fresh_app, 10U, true);
    CHECK(fresh_app.energy_accumulator_mw_ms == 160000ULL);
}

/*---------------------------------------------------------------------------*
 * Name        : int main(void)
 * Input       : 无
 * Output      : 0表示v0.10.3全部行为回归通过
 * Description : 覆盖Break先关MOE、Relay Hold-off/反馈、Demo闭合、预充分类和陈旧能量。
 *---------------------------------------------------------------------------*/
int main(void)
{
    test_break_uses_software_arm_state();
    test_relay_holdoff_and_applied_feedback();
    test_precharge_timeout_is_not_weak_light();
    test_demo_relay_and_power_cap();
    test_stale_pv_energy_is_not_accumulated();
    printf("Aurora v0.10.3 safety tests: %u assertions passed.\n", g_assertions);
    return 0;
}
