#!/usr/bin/env python3
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
path = root / "app/src/main.c"
text = path.read_text(encoding="utf-8")

region = r'''/*---------------------------------------------------------------------------*
 * Name        : void aurora_app_on_adc_block(aurora_app_t *app,
 *               const uint16_t *raw, size_t word_count, uint32_t timestamp_ms)
 * Input       : app - 应用总上下文；raw - 完整DMA块；word_count - 块字数；timestamp_ms - 完成时间
 * Output      : 无
 * Description : 处理完整DMA块；ZERO_CAL阶段且PV已稳定2s时额外累计PV_I运行时零点。
 *---------------------------------------------------------------------------*/
void aurora_app_on_adc_block(aurora_app_t *app, const uint16_t *raw, size_t word_count,
                             uint32_t timestamp_ms)
{
    if (app == NULL)
    {
        return;
    }

    if (aurora_measurement_process_block(&app->measurement, raw, word_count, timestamp_ms) ==
        AURORA_STATUS_OK)
    {
        (void)aurora_measurement_read(&app->measurement, &app->sample);
    }

    if ((app->power_stage.state == AURORA_POWER_ZERO_CAL) && !app->power_stage.relay_closed &&
        (app->power_stage.pv_valid_since_ms != 0U) &&
        ((timestamp_ms - app->power_stage.pv_valid_since_ms) >= AURORA_ZERO_CAL_PV_STABLE_MS))
    {
        (void)aurora_measurement_zero_cal_accumulate(&app->measurement, raw, word_count);
    }
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_app_on_fast_fault(aurora_app_t *app,
 *               uint32_t fault_mask, uint32_t now_ms)
 * Input       : app - 应用总上下文；fault_mask - ISR快速故障；now_ms - 当前毫秒
 * Output      : 无
 * Description : 把真正运行阶段的快速硬件故障纳入统一锁存；恢复由运行层和保护策略共同裁决。
 *---------------------------------------------------------------------------*/
void aurora_app_on_fast_fault(aurora_app_t *app, uint32_t fault_mask, uint32_t now_ms)
{
    if (app != NULL)
    {
        aurora_protection_latch_fast_fault(&app->protection, fault_mask, now_ms);
    }
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_app_step_1ms(aurora_app_t *app, uint32_t now_ms,
 *               bool boost_output_active)
 * Input       : app - 应用总上下文；now_ms - 当前毫秒；
 *               boost_output_active - Driver报告的PWM实际状态
 * Output      : 无
 * Description : 先更新测量/保护，再按ADC时间轴统计有效传能；1ms执行Power Stage，10ms更新Charger/MPPT/UI。
 *---------------------------------------------------------------------------*/
void aurora_app_step_1ms(aurora_app_t *app, uint32_t now_ms, bool boost_output_active)
{
    uint32_t elapsed_step_ms;
    uint32_t sample_elapsed_ms;
    aurora_power_state_t previous_power_state;
    bool pv_energy_qualified;

    if (app == NULL)
    {
        return;
    }

    elapsed_step_ms = now_ms - app->last_step_ms;
    if (elapsed_step_ms == 0U)
    {
        return;
    }
    if (elapsed_step_ms > AURORA_MAX_ELAPSED_STEP_MS)
    {
        elapsed_step_ms = AURORA_MAX_ELAPSED_STEP_MS;
    }
    app->last_step_ms = now_ms;

    (void)aurora_measurement_read(&app->measurement, &app->sample);

    // 先更新本轮电池电流估算和Protection，禁止先对已经失效的样本做时间/能量积分。
    aurora_measurement_estimate_battery_current(
        &app->sample, estimate_efficiency_q15(&app->sample), app->relay_applied_feedback,
        (app->power_stage.state == AURORA_POWER_RELAY_SETTLE) ||
            (app->power_stage.state == AURORA_POWER_BAT_STABILITY));
    aurora_protection_step_ex(
        &app->protection, &app->sample, &app->charger.profile, app->storage.settings.operating_mode,
        aurora_measurement_zero_cal_ready(&app->measurement), boost_output_active, now_ms);

    // 只使用两个真实ADC发布时间戳之间的间隔；首个样本只建立时间基准，不外推墙上时间。
    sample_elapsed_ms = 0U;
    if ((app->sample.sequence != 0U) &&
        (app->sample.sequence != app->last_energy_sample_sequence))
    {
        if (app->last_energy_sample_sequence != 0U)
        {
            const uint32_t measured_delta_ms =
                app->sample.timestamp_ms - app->last_energy_sample_timestamp_ms;
            if (measured_delta_ms <= AURORA_MEASUREMENT_STALE_MS)
            {
                sample_elapsed_ms = measured_delta_ms;
            }
        }
        app->last_energy_sample_sequence = app->sample.sequence;
        app->last_energy_sample_timestamp_ms = app->sample.timestamp_ms;
    }

    pv_energy_qualified = pv_energy_sample_qualified(app, now_ms, boost_output_active);

    // Battery实际传能必须同时匹配当前Relay事务，不能让上一次闭合反馈继续授权新的会话。
    app->actual_power_transfer =
        (app->storage.settings.operating_mode == AURORA_MODE_BATTERY) &&
        (app->power_stage.state == AURORA_POWER_RUN) && app->relay_applied_feedback &&
        (app->relay_applied_generation_feedback == app->power_command.relay_generation) &&
        boost_output_active && app->charge_output.allow_charge && app->power_command.pwm_enable &&
        pv_energy_qualified &&
        (app->sample.pv_power_mw >= (int32_t)AURORA_ACTUAL_TRANSFER_MIN_POWER_MW);
    aurora_charger_account_active_time(&app->charger, app->actual_power_transfer, sample_elapsed_ms);

    // PV实测能量只在新鲜有效样本间隔内累计，PRECHARGE/Battery RUN/Demo Probe或Run才有资格。
    if (pv_energy_qualified && (sample_elapsed_ms != 0U))
    {
        app->energy_accumulator_mw_ms +=
            (uint64_t)(uint32_t)app->sample.pv_power_mw * sample_elapsed_ms;
        while (app->energy_accumulator_mw_ms >= AURORA_ONE_WH_MW_MS)
        {
            app->energy_accumulator_mw_ms -= AURORA_ONE_WH_MW_MS;
            app->storage.settings.lifetime_energy_wh++;
            aurora_storage_energy_history_update(&app->storage.settings);
            aurora_storage_mark_dirty(&app->storage, now_ms);
        }
    }

    // 新板没有BAT_I，电池侧账本仍为Ppv×η估算，只在真实Battery传能会话累计。
    if (app->actual_power_transfer && (sample_elapsed_ms != 0U))
    {
        const uint64_t charge_power_mw =
            ((uint64_t)(uint32_t)app->sample.pv_power_mw * estimate_efficiency_q15(&app->sample)) /
            AURORA_DUTY_Q15_ONE;
        app->charge_energy_accumulator_mw_ms += charge_power_mw * sample_elapsed_ms;
        while (app->charge_energy_accumulator_mw_ms >= AURORA_ONE_WH_MW_MS)
        {
            app->charge_energy_accumulator_mw_ms -= AURORA_ONE_WH_MW_MS;
            app->storage.settings.charge_est_lifetime_energy_wh++;
            aurora_storage_energy_history_update(&app->storage.settings);
            aurora_storage_mark_dirty(&app->storage, now_ms);
        }
    }
    app->storage.settings.pv_energy_remainder_mw_ms = app->energy_accumulator_mw_ms;
    app->storage.settings.charge_est_energy_remainder_mw_ms = app->charge_energy_accumulator_mw_ms;

    // 24h窗口相位跟随系统时间；即使暂时无发电，也需要继续推进窗口边界。
    {
        uint64_t interval_ms =
            (uint64_t)app->storage.settings.history_interval_elapsed_ms + elapsed_step_ms;
        while (interval_ms >= AURORA_ENERGY_HISTORY_INTERVAL_MS)
        {
            interval_ms -= AURORA_ENERGY_HISTORY_INTERVAL_MS;
            aurora_storage_energy_history_checkpoint(&app->storage.settings);
            aurora_storage_mark_dirty(&app->storage, now_ms);
        }
        app->storage.settings.history_interval_elapsed_ms = (uint32_t)interval_ms;
    }
    if ((now_ms - app->last_energy_history_ms) >= AURORA_ENERGY_PERSIST_REQUEST_MS)
    {
        aurora_storage_mark_dirty(&app->storage, now_ms);
        app->last_energy_history_ms = now_ms;
    }

    if ((now_ms - app->last_10ms) >= AURORA_CONTROL_PERIOD_MS)
    {
        const uint32_t elapsed_control_ms = now_ms - app->last_10ms;
        const bool weak_light = app->sample.pv_power_mw < APP_WEAK_LIGHT_POWER_MW;
        const uint32_t thermal_limit_mw = thermal_power_limit_mw(&app->sample);
        const uint32_t voltage_limit_mw = input_voltage_power_limit_mw(app->sample.pv_voltage_mv);
        uint32_t current_limit_mw = AURORA_RATED_POWER_MW;
        uint32_t hardware_limit_mw;
        uint32_t pv_required_mw;
        uint16_t efficiency_q15;
        bool input_limited_previous = app->charge_output.input_limited;
        bool thermal_limited = thermal_limit_mw < AURORA_RATED_POWER_MW;
        bool external_limited;

        if (app->sample.pv_voltage_mv > 0)
        {
            const uint64_t current_power = ((uint64_t)(uint32_t)app->sample.pv_voltage_mv *
                                            (uint32_t)AURORA_PV_CURRENT_LIMIT_MA) /
                                           AURORA_MV_MA_PER_MW;
            if (current_power < current_limit_mw)
            {
                current_limit_mw = (uint32_t)current_power;
            }
        }

        hardware_limit_mw = min_u32(AURORA_RATED_POWER_MW, thermal_limit_mw);
        hardware_limit_mw = min_u32(hardware_limit_mw, voltage_limit_mw);
        hardware_limit_mw = min_u32(hardware_limit_mw, current_limit_mw);

        if (app->storage.settings.operating_mode == AURORA_MODE_BATTERY)
        {
            app->charge_output =
                aurora_charger_step(&app->charger, &app->sample, weak_light, thermal_limited,
                                    input_limited_previous, now_ms);
        }
        else
        {
            memset(&app->charge_output, 0, sizeof(app->charge_output));
            app->charge_output.state = AURORA_CHARGE_OFF;
        }

        efficiency_q15 = estimate_efficiency_q15(&app->sample);
        pv_required_mw =
            battery_to_pv_power_mw(app->charge_output.battery_power_target_mw, efficiency_q15);
        app->charge_output.pv_power_limit_mw = min_u32(pv_required_mw, hardware_limit_mw);
        if (app->storage.settings.operating_mode == AURORA_MODE_DEMO_LOAD)
        {
            const uint32_t demo_limit_mw =
                min_u32(app->storage.settings.demo_power_limit_mw,
                        AURORA_DEMO_HARD_POWER_CAP_MW);
            app->charge_output.pv_power_limit_mw = min_u32(demo_limit_mw, hardware_limit_mw);
        }
        app->charge_output.input_limited =
            app->charge_output.allow_charge && (pv_required_mw > hardware_limit_mw);
        app->charge_output.thermal_limited = thermal_limited;

        external_limited = thermal_limited || app->charge_output.input_limited ||
                           ((app->storage.settings.operating_mode == AURORA_MODE_BATTERY) &&
                            !app->charge_output.allow_charge);
        app->mppt_output =
            aurora_mppt_step(&app->mppt, &app->sample, app->charge_output.pv_power_limit_mw,
                             external_limited, now_ms);
        app->ui_output =
            aurora_ui_step(&app->ui, app->power_stage.state,
                           aurora_protection_fault_mask(&app->protection), elapsed_control_ms);
        app->last_10ms = now_ms;
    }

    previous_power_state = app->power_stage.state;
    app->power_command = aurora_power_stage_step_ex(
        &app->power_stage, &app->sample, &app->mppt_output, &app->charge_output,
        aurora_protection_is_safe(&app->protection),
        aurora_measurement_zero_cal_ready(&app->measurement),
        aurora_measurement_zero_cal_failed(&app->measurement), app->relay_applied_feedback,
        app->relay_applied_generation_feedback, app->storage.settings.operating_mode,
        app->storage.settings.demo_target_voltage_mv, app->storage.settings.demo_power_limit_mw,
        now_ms);

    if (app->power_stage.startup_locked)
    {
        uint32_t fault = AURORA_FAULT_RELAY;
        switch (app->power_stage.last_failure_reason)
        {
        case AURORA_START_FAIL_ZERO_CAL:
            fault = AURORA_FAULT_PV_CURRENT_PLAUSIBILITY;
            break;
        case AURORA_START_FAIL_BUS_OVERSHOOT:
            fault = AURORA_FAULT_BUS_OVERVOLT;
            break;
        case AURORA_START_FAIL_BUS_MEAS_INVALID:
            fault = AURORA_FAULT_BUS_ADC_SATURATION;
            break;
        case AURORA_START_FAIL_DEMO_EXTERNAL_SOURCE:
        case AURORA_START_FAIL_DEMO_OVERLOAD:
            fault = AURORA_FAULT_DEMO_OUTPUT;
            break;
        case AURORA_START_FAIL_BUS_PRECHARGE_TIMEOUT:
        case AURORA_START_FAIL_RELAY_CLOSE_VERIFY:
        case AURORA_START_FAIL_BAT_STABILITY:
        case AURORA_START_FAIL_NONE:
        case AURORA_START_FAIL_PV_WEAK:
        case AURORA_START_FAIL_DEMO_NO_LOAD:
        default:
            fault = AURORA_FAULT_RELAY;
            break;
        }
        aurora_protection_latch_software_fault(&app->protection, fault, true, now_ms);
    }

    if ((app->power_stage.state == AURORA_POWER_START_DELAY) &&
        (previous_power_state != AURORA_POWER_START_DELAY))
    {
        aurora_measurement_zero_cal_reset(&app->measurement);
    }

    app->link_request = (app->sample.pv_voltage_mv >= AURORA_PV_START_MIN_MV) ||
                        (app->power_stage.state == AURORA_POWER_PRECHARGE) ||
                        (app->power_stage.state == AURORA_POWER_RELAY_HOLD_OFF) ||
                        (app->power_stage.state == AURORA_POWER_RELAY_SETTLE) ||
                        (app->power_stage.state == AURORA_POWER_BAT_STABILITY) ||
                        (app->power_stage.state == AURORA_POWER_RUN) ||
                        (app->power_stage.state == AURORA_POWER_DEMO_OUTPUT_CHECK) ||
                        (app->power_stage.state == AURORA_POWER_DEMO_RELAY_SETTLE) ||
                        (app->power_stage.state == AURORA_POWER_DEMO_PROBE) ||
                        (app->power_stage.state == AURORA_POWER_DEMO_RUN);
}

'''

pattern = r'/\*---------------------------------------------------------------------------\*\n \* Name        : void aurora_app_on_adc_block.*?(?=/\*---------------------------------------------------------------------------\*\n \* Name        : void aurora_app_on_protocol_frame)'
new_text, count = re.subn(pattern, region, text, count=1, flags=re.S)
if count != 1:
    raise SystemExit(f"main.c app region match count={count}")
path.write_text(new_text, encoding="utf-8", newline="\n")
print("main.c app callbacks/control region restored with bounded rewrite")
