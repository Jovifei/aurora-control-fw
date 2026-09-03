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
 * Description : 构造v0.10.3安全行为测试使用的最小有效测量标定。
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
 * Input       : sequence - 测量发布序号；now_ms - 测量时间戳，ms
 * Output      : 可供功率状态机、保护和能量统计使用的完整样本
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
 * Name        : static void test_break_uses_software_arm_state(void)
 * Input       : 无
 * Output      : 无
 * Description : 模拟硬件Break先撤销MOE、ISR后到，验证ACTIVE仍锁存而WAIT_ZERO只记启动诊断。
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
 * Name        : static void test_runtime_captures_post_pwm_off_baseline(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证HOLD_OFF的ADC基准只能由Runtime在物理关PWM后建立。
 *---------------------------------------------------------------------------*/
static void test_runtime_captures_post_pwm_off_baseline(void)
{
    aurora_runtime_t runtime;
    mock_reset();
    CHECK(aurora_runtime_init(&runtime));
    /* 模拟32位ADC发布序号恰好回绕到0；0不能再被当成“未捕获”哨兵。 */
    runtime.app.sample = valid_sample(0U, 0U);
    runtime.app.power_stage.state = AURORA_POWER_RELAY_HOLD_OFF;
    runtime.app.power_stage.relay_holdoff_sequence = 123U;
    runtime.app.power_stage.relay_holdoff_sequence_valid = false;
    runtime.app.power_command.state = AURORA_POWER_RELAY_HOLD_OFF;
    runtime.app.power_command.pwm_enable = false;
    runtime.app.power_command.relay_enable = false;
    aurora_runtime_poll(&runtime);
    CHECK(runtime.relay_holdoff_baseline_captured);
    CHECK(runtime.app.power_stage.relay_holdoff_sequence_valid);
    CHECK(runtime.app.power_stage.relay_holdoff_sequence == 0U);
    CHECK(!mock_pwm_active());
    CHECK(!mock_relay());
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_holdoff_requires_two_new_blocks_and_applied_feedback(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证Relay闭合需两个关波后新ADC发布，且只有Runtime已落实GPIO后才开始100ms稳定计时。
 *---------------------------------------------------------------------------*/
static void test_holdoff_requires_two_new_blocks_and_applied_feedback(void)
{
    aurora_power_stage_ctx_t ctx;
    aurora_measurement_t sample = valid_sample(1U, 1U);
    aurora_mppt_output_t mppt = {0};
    aurora_charge_output_t charger = {0};
    aurora_power_command_t command;
    charger.allow_charge = true;
    charger.pv_power_limit_mw = 100000U;
    charger.voltage_target_mv = 50000U;

    aurora_power_stage_init(&ctx, 0U);
    ctx.state = AURORA_POWER_RELAY_HOLD_OFF;
    ctx.state_since_ms = 1000U;
    ctx.relay_holdoff_sequence = 1U;
    ctx.relay_holdoff_sequence_valid = true;

    sample.sequence = 2U;
    sample.timestamp_ms = 1021U;
    command = aurora_power_stage_step_ex(&ctx, &sample, &mppt, &charger, true, true, false, false, AURORA_MODE_BATTERY, 48000U, 30000U, 1021U);
    CHECK(command.state == AURORA_POWER_RELAY_HOLD_OFF);
    CHECK(!command.relay_enable);

    sample.sequence = 3U;
    sample.timestamp_ms = 1022U;
    command = aurora_power_stage_step_ex(&ctx, &sample, &mppt, &charger, true, true, false, false, AURORA_MODE_BATTERY, 48000U, 30000U, 1022U);
    CHECK(command.state == AURORA_POWER_RELAY_SETTLE);
    CHECK(command.relay_enable);

    sample.timestamp_ms = 1070U;
    command = aurora_power_stage_step_ex(&ctx, &sample, &mppt, &charger, true, true, false,
                                         false, AURORA_MODE_BATTERY, 48000U, 30000U, 1070U);
    CHECK(command.state == AURORA_POWER_RELAY_SETTLE);
    CHECK(ctx.delta_ok_since_ms == 0U);

    sample.timestamp_ms = 1071U;
    command = aurora_power_stage_step_ex(&ctx, &sample, &mppt, &charger, true, true, false,
                                         true, AURORA_MODE_BATTERY, 48000U, 30000U, 1071U);
    CHECK(command.state == AURORA_POWER_RELAY_SETTLE);
    CHECK(ctx.delta_ok_since_ms == 1071U);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_holdoff_sequence_wrap_zero_is_valid(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证关波基准恰好为sequence=0时仍按无符号回绕等待两个新DMA块，不会永久卡死。
 *---------------------------------------------------------------------------*/
static void test_holdoff_sequence_wrap_zero_is_valid(void)
{
    aurora_power_stage_ctx_t ctx;
    aurora_measurement_t sample = valid_sample(1U, 1021U);
    aurora_mppt_output_t mppt = {0};
    aurora_charge_output_t charger = {0};
    aurora_power_command_t command;

    charger.voltage_target_mv = 50000U;
    aurora_power_stage_init(&ctx, 0U);
    ctx.state = AURORA_POWER_RELAY_HOLD_OFF;
    ctx.state_since_ms = 1000U;
    ctx.relay_holdoff_sequence = 0U;
    ctx.relay_holdoff_sequence_valid = true;

    command = aurora_power_stage_step_ex(&ctx, &sample, &mppt, &charger, true, true, false, false, AURORA_MODE_BATTERY, 48000U, 30000U, 1021U);
    CHECK(command.state == AURORA_POWER_RELAY_HOLD_OFF);
    CHECK(!command.relay_enable);

    sample.sequence = 2U;
    sample.timestamp_ms = 1022U;
    command = aurora_power_stage_step_ex(&ctx, &sample, &mppt, &charger, true, true, false, false, AURORA_MODE_BATTERY, 48000U, 30000U, 1022U);
    CHECK(command.state == AURORA_POWER_RELAY_SETTLE);
    CHECK(command.relay_enable);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_holdoff_delta_loss_is_bounded(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证关波后均压丢失会进入有限预充失败，而不是PRECHARGE/HOLD_OFF无限循环。
 *---------------------------------------------------------------------------*/
static void test_holdoff_delta_loss_is_bounded(void)
{
    aurora_power_stage_ctx_t ctx;
    aurora_measurement_t sample = valid_sample(12U, 600U);
    aurora_mppt_output_t mppt = {0};
    aurora_charge_output_t charger = {0};
    aurora_power_command_t command;
    aurora_power_stage_init(&ctx, 0U);
    ctx.state = AURORA_POWER_RELAY_HOLD_OFF;
    ctx.state_since_ms = 0U;
    ctx.relay_holdoff_sequence = 10U;
    ctx.relay_holdoff_sequence_valid = true;
    sample.bus_voltage_mv = sample.battery_voltage_mv - AURORA_RELAY_CLOSE_DELTA_MV - 1000L;
    command = aurora_power_stage_step_ex(&ctx, &sample, &mppt, &charger, true, true, false, false, AURORA_MODE_BATTERY, 48000U, 30000U, 600U);
    CHECK(command.state == AURORA_POWER_FAULT);
    CHECK(ctx.last_failure_reason == AURORA_START_FAIL_BUS_PRECHARGE_TIMEOUT);
    CHECK(ctx.precharge_failure_count == 1U);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_precharge_timeout_and_weak_pv_are_separate(void)
 * Input       : 无
 * Output      : 无
 * Description : PV仍>=13V时30s预充失败必须算BUS路径失败；只有跌出13V才归类弱光且不耗重试。
 *---------------------------------------------------------------------------*/
static void test_precharge_timeout_and_weak_pv_are_separate(void)
{
    aurora_power_stage_ctx_t ctx;
    aurora_measurement_t sample = valid_sample(1U, AURORA_PRECHARGE_TIMEOUT_MS);
    aurora_mppt_output_t mppt = {0};
    aurora_charge_output_t charger = {0};
    aurora_power_command_t command;

    aurora_power_stage_init(&ctx, 0U);
    ctx.state = AURORA_POWER_PRECHARGE;
    ctx.state_since_ms = 0U;
    sample.bus_voltage_mv = 30000;
    sample.pv_voltage_mv = 14000;
    sample.pv_power_mw = 1000;
    command = aurora_power_stage_step_ex(&ctx, &sample, &mppt, &charger, true, true, false,
                                         false, AURORA_MODE_BATTERY, 48000U, 30000U,
                                         AURORA_PRECHARGE_TIMEOUT_MS);
    CHECK(command.state == AURORA_POWER_FAULT);
    CHECK(ctx.last_failure_reason == AURORA_START_FAIL_BUS_PRECHARGE_TIMEOUT);
    CHECK(ctx.precharge_failure_count == 1U);

    aurora_power_stage_init(&ctx, 0U);
    ctx.state = AURORA_POWER_PRECHARGE;
    ctx.state_since_ms = 0U;
    sample.pv_voltage_mv = AURORA_PV_START_MIN_MV - 1L;
    sample.timestamp_ms++;
    command = aurora_power_stage_step_ex(&ctx, &sample, &mppt, &charger, true, true, false,
                                         false, AURORA_MODE_BATTERY, 48000U, 30000U,
                                         sample.timestamp_ms);
    CHECK(command.state == AURORA_POWER_WAIT_PV);
    CHECK(ctx.last_failure_reason == AURORA_START_FAIL_PV_WEAK);
    CHECK(ctx.precharge_failure_count == 0U);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_demo_low_residual_bus_and_gate_path(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证Demo Relay拒绝高BST_U残压，并在Host专用门禁放行后才允许安全闭合。
 *---------------------------------------------------------------------------*/
static void test_demo_low_residual_bus_and_gate_path(void)
{
    aurora_runtime_t runtime;
    mock_reset();
    CHECK(aurora_runtime_init(&runtime));
    runtime.app.storage.settings.operating_mode = AURORA_MODE_DEMO_LOAD;
    runtime.app.sample = valid_sample(1U, 0U);
    runtime.app.sample.battery_voltage_mv = 0;
    runtime.app.sample.bus_voltage_mv = AURORA_DEMO_RELAY_CLOSE_BUS_MAX_MV + 1L;
    runtime.app.power_command.state = AURORA_POWER_DEMO_RELAY_SETTLE;
    runtime.app.power_command.relay_enable = true;
    aurora_runtime_poll(&runtime);
    CHECK(!runtime.relay_applied);
    CHECK(!mock_relay());

    runtime.app.sample.bus_voltage_mv = AURORA_DEMO_RELAY_CLOSE_BUS_MAX_MV;
    runtime.app.sample.timestamp_ms = drv_time_now_ms();
    aurora_runtime_poll(&runtime);
    CHECK(runtime.relay_applied);
    CHECK(mock_relay());
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_pending_fault_keeps_break_latched(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证ISR已投递但Protection未消费的快速故障会阻止启动清理路径提前擦除Break锁存。
 *---------------------------------------------------------------------------*/
static void test_pending_fault_keeps_break_latched(void)
{
    aurora_runtime_t runtime;
    mock_reset();
    CHECK(aurora_runtime_init(&runtime));
    mock_set_break(true);
    mock_set_break(false);
    CHECK(drv_pwm_break_latched());
    runtime.pending_fault_mask = AURORA_FAULT_FAST_MOS_OCP;
    runtime.app.power_command.pwm_enable = true;
    aurora_runtime_poll(&runtime);
    CHECK(drv_pwm_break_latched());
    CHECK(!mock_pwm_active());
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_stale_energy_and_adc_timebase(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证能量只按新ADC时间戳间隔累计，首样本和陈旧样本都不会被墙上时间外推。
 *---------------------------------------------------------------------------*/
static void test_stale_energy_and_adc_timebase(void)
{
    aurora_app_t app;
    aurora_measurement_calibration_t calibration = unit_calibration();
    aurora_measurement_t sample;
    aurora_app_init(&app, &calibration, 0U);
    app.power_stage.state = AURORA_POWER_PRECHARGE;

    sample = valid_sample(1U, 10U);
    app.measurement.latest = sample;
    app.measurement.publish_sequence = sample.sequence;
    aurora_app_step_1ms(&app, 10U, true);
    CHECK(app.energy_accumulator_mw_ms == 0ULL);

    sample.sequence = 2U;
    sample.timestamp_ms = 20U;
    app.measurement.latest = sample;
    app.measurement.publish_sequence = sample.sequence;
    aurora_app_step_1ms(&app, 20U, true);
    CHECK(app.energy_accumulator_mw_ms == 160000ULL);

    sample.sequence = 3U;
    sample.timestamp_ms = 20U + AURORA_MEASUREMENT_STALE_MS + 1U;
    app.measurement.latest = sample;
    app.measurement.publish_sequence = sample.sequence;
    aurora_app_step_1ms(&app, sample.timestamp_ms, true);
    CHECK(app.energy_accumulator_mw_ms == 160000ULL);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_legacy_energy_fields_keep_v0102_pv_semantics(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证旧30字节布局与v0.10.2 PV发电量语义均保持不变；charge_est继续作为内部独立账本。
 *---------------------------------------------------------------------------*/
static void test_legacy_energy_fields_keep_v0102_pv_semantics(void)
{
    aurora_protocol_frame_t frame;
    aurora_measurement_t sample = valid_sample(1U, 1U);
    aurora_persistent_settings_t settings;
    memset(&settings, 0, sizeof(settings));
    settings.daily_energy_wh = 111U;
    settings.lifetime_energy_wh = 222U;
    settings.charge_est_daily_energy_wh = 33U;
    settings.charge_est_lifetime_energy_wh = 44U;
    aurora_protocol_fill_telemetry_ex(&frame, 1U, &sample, AURORA_CHARGE_CC, true, 0U, &settings);
    CHECK(frame.data[4] == 111U);
    CHECK(frame.data[5] == 0U);
    CHECK(frame.data[15] == 222U);
    CHECK(frame.data[16] == 0U);
    CHECK(frame.data[17] == 0U);
    CHECK(frame.data[18] == 0U);
    CHECK(frame.data[20] == 111U);
    CHECK(frame.data[21] == 0U);
}

/*---------------------------------------------------------------------------*
 * Name        : static uint32_t storage_test_ready_ms(void)
 * Input       : 无
 * Output      : 满足dirty与停机稳定窗口后的测试时间
 * Description : 统一构造可触发安全存储调度的时间点。
 *---------------------------------------------------------------------------*/
static uint32_t storage_test_ready_ms(void)
{
    return AURORA_STORAGE_DIRTY_HOLD_MS + AURORA_STORAGE_STOP_HOLD_MS + 10U;
}

/*---------------------------------------------------------------------------*
 * Name        : static void prepare_stopped_dirty_runtime(aurora_runtime_t *runtime)
 * Input       : runtime - 待准备的运行上下文
 * Output      : 无
 * Description : 从擦除Flash启动，保持WAIT_PV停机态并等待足够时间，使dirty只差电源资格即可落盘。
 *---------------------------------------------------------------------------*/
static void prepare_stopped_dirty_runtime(aurora_runtime_t *runtime)
{
    mock_reset();
    CHECK(aurora_runtime_init(runtime));
    runtime->app.power_stage.state = AURORA_POWER_WAIT_PV;
    runtime->app.power_stage.state_since_ms = 0U;
    runtime->app.storage.dirty = true;
    runtime->app.storage.dirty_since_ms = 0U;
    mock_advance_ms(storage_test_ready_ms());
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_flash_low_supply_is_defer_only(void)
 * Input       : 无
 * Output      : 无
 * Description : 欠压/PVD不合格时不得擦写Flash，也不得把“没保存”升级成Storage Fault；dirty保留待下次停机。
 *---------------------------------------------------------------------------*/
static void test_flash_low_supply_is_defer_only(void)
{
    aurora_runtime_t runtime;

    prepare_stopped_dirty_runtime(&runtime);
    mock_set_flash_supply_safe(false);
    aurora_runtime_poll(&runtime);

    CHECK(mock_flash_erase_count() == 0U);
    CHECK(mock_flash_program_count() == 0U);
    CHECK(runtime.app.storage.dirty);
    CHECK(!runtime.app.storage.write_inhibited);
    CHECK((aurora_protection_fault_mask(&runtime.app.protection) & AURORA_FAULT_STORAGE) == 0U);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_flash_only_writes_in_stable_stop_state(void)
 * Input       : 无
 * Output      : 无
 * Description : 即使PWM/Relay物理关闭，START_DELAY等启动瞬态也不得把RAM dirty写入Flash。
 *---------------------------------------------------------------------------*/
static void test_flash_only_writes_in_stable_stop_state(void)
{
    aurora_runtime_t runtime;

    prepare_stopped_dirty_runtime(&runtime);
    runtime.app.power_stage.state = AURORA_POWER_START_DELAY;
    runtime.app.power_stage.state_since_ms = 0U;
    aurora_runtime_poll(&runtime);

    CHECK(mock_flash_erase_count() == 0U);
    CHECK(mock_flash_program_count() == 0U);
    CHECK(runtime.app.storage.dirty);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_flash_safe_stop_commits_and_reads_back(void)
 * Input       : 无
 * Output      : 无
 * Description : 稳定停机且VDD正常时从无active页写A页，Commit回读有效后才清dirty并提交sequence。
 *---------------------------------------------------------------------------*/
static void test_flash_safe_stop_commits_and_reads_back(void)
{
    aurora_runtime_t runtime;
    aurora_persistent_settings_t restored;
    uint32_t sequence = 0U;
    uint8_t page[AURORA_STORAGE_PAGE_SIZE];

    prepare_stopped_dirty_runtime(&runtime);
    aurora_runtime_poll(&runtime);

    CHECK(mock_flash_erase_count() == 1U);
    CHECK(mock_flash_program_count() == 3U);
    CHECK(!runtime.app.storage.dirty);
    CHECK(runtime.app.storage.active_page == AURORA_STORAGE_ACTIVE_PAGE_A);
    CHECK(runtime.app.storage.sequence == 2U);
    CHECK(drv_flash_read(drv_board_flash_page_a(), page, sizeof(page)));
    CHECK(aurora_storage_classify_page(page, sizeof(page), &restored, &sequence) ==
          AURORA_STORAGE_PAGE_VALID);
    CHECK(sequence == runtime.app.storage.sequence);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_flash_retries_same_inactive_page_once(void)
 * Input       : 无
 * Output      : 无
 * Description : 第一次program I/O失败时只在同一备用A页重试一次，成功后才提交sequence且不产生Storage Fault。
 *---------------------------------------------------------------------------*/
static void test_flash_retries_same_inactive_page_once(void)
{
    aurora_runtime_t runtime;

    prepare_stopped_dirty_runtime(&runtime);
    mock_fail_next_flash_program(1U);
    aurora_runtime_poll(&runtime);

    CHECK(mock_flash_erase_count() == 2U);
    CHECK(mock_flash_program_count() == 4U);
    CHECK(!runtime.app.storage.dirty);
    CHECK(!runtime.app.storage.write_inhibited);
    CHECK(runtime.app.storage.active_page == AURORA_STORAGE_ACTIVE_PAGE_A);
    CHECK(runtime.app.storage.sequence == 2U);
    CHECK((aurora_protection_fault_mask(&runtime.app.protection) & AURORA_FAULT_STORAGE) == 0U);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_flash_double_failure_preserves_active_page(void)
 * Input       : 无
 * Output      : 无
 * Description : 已有A页有效时，B页两次写失败不得递增已提交sequence或回头擦A页；本会话随后禁止继续擦写。
 *---------------------------------------------------------------------------*/
static void test_flash_double_failure_preserves_active_page(void)
{
    aurora_runtime_t runtime;
    aurora_persistent_settings_t restored;
    uint32_t first_sequence;
    uint32_t restored_sequence = 0U;
    uint32_t erase_before;
    uint32_t program_before;
    uint8_t page[AURORA_STORAGE_PAGE_SIZE];

    prepare_stopped_dirty_runtime(&runtime);
    aurora_runtime_poll(&runtime);
    CHECK(runtime.app.storage.active_page == AURORA_STORAGE_ACTIVE_PAGE_A);
    first_sequence = runtime.app.storage.sequence;

    aurora_storage_mark_dirty(&runtime.app.storage, drv_time_now_ms());
    mock_advance_ms(storage_test_ready_ms());
    runtime.app.power_stage.state_since_ms = drv_time_now_ms() - AURORA_STORAGE_STOP_HOLD_MS;
    erase_before = mock_flash_erase_count();
    program_before = mock_flash_program_count();
    mock_fail_next_flash_program(2U);
    aurora_runtime_poll(&runtime);

    CHECK(mock_flash_erase_count() == erase_before + 2U);
    CHECK(mock_flash_program_count() == program_before + 2U);
    CHECK(runtime.app.storage.sequence == first_sequence);
    CHECK(runtime.app.storage.active_page == AURORA_STORAGE_ACTIVE_PAGE_A);
    CHECK(runtime.app.storage.dirty);
    CHECK(runtime.app.storage.write_inhibited);
    CHECK((aurora_protection_fault_mask(&runtime.app.protection) & AURORA_FAULT_STORAGE) != 0U);
    CHECK(drv_flash_read(drv_board_flash_page_a(), page, sizeof(page)));
    CHECK(aurora_storage_classify_page(page, sizeof(page), &restored, &restored_sequence) ==
          AURORA_STORAGE_PAGE_VALID);
    CHECK(restored_sequence == first_sequence);

    erase_before = mock_flash_erase_count();
    program_before = mock_flash_program_count();
    mock_advance_ms(storage_test_ready_ms());
    aurora_runtime_poll(&runtime);
    CHECK(mock_flash_erase_count() == erase_before);
    CHECK(mock_flash_program_count() == program_before);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_flash_driver_rejects_zero_and_cross_page(void)
 * Input       : 无
 * Output      : 无
 * Description : Driver最后防线必须拒绝0地址以及从A页尾跨入B页的单次program请求。
 *---------------------------------------------------------------------------*/
static void test_flash_driver_rejects_zero_and_cross_page(void)
{
    const uint32_t words[2] = {0x12345678UL, 0xABCDEF00UL};
    const uint32_t cross = drv_board_flash_page_a() + AURORA_STORAGE_PAGE_SIZE - (uint32_t)sizeof(uint32_t);

    mock_reset();
    CHECK(!drv_flash_program(0U, words, sizeof(uint32_t)));
    CHECK(!drv_flash_program(cross, words, sizeof(words)));
    CHECK(mock_flash_program_count() == 0U);
}

/*---------------------------------------------------------------------------*
 * Name        : int main(void)
 * Input       : 无
 * Output      : 0表示全部v0.10.3二次审阅行为回归通过
 * Description : 顺序执行快速故障、Relay事务、Demo、预充、能量与旧协议兼容测试。
 *---------------------------------------------------------------------------*/
int main(void)
{
    test_break_uses_software_arm_state();
    test_runtime_captures_post_pwm_off_baseline();
    test_holdoff_requires_two_new_blocks_and_applied_feedback();
    test_holdoff_sequence_wrap_zero_is_valid();
    test_holdoff_delta_loss_is_bounded();
    test_precharge_timeout_and_weak_pv_are_separate();
    test_demo_low_residual_bus_and_gate_path();
    test_pending_fault_keeps_break_latched();
    test_stale_energy_and_adc_timebase();
    test_legacy_energy_fields_keep_v0102_pv_semantics();
    test_flash_low_supply_is_defer_only();
    test_flash_only_writes_in_stable_stop_state();
    test_flash_safe_stop_commits_and_reads_back();
    test_flash_retries_same_inactive_page_once();
    test_flash_double_failure_preserves_active_page();
    test_flash_driver_rejects_zero_and_cross_page();
    printf("Aurora v0.10.3 reviewed closeout tests: %u assertions passed.\n", g_assertions);
    return 0;
}
