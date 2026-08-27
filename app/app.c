#include "app.h"

#include "app_config.h"

#include <string.h>

static uint16_t estimate_efficiency_q15(const aurora_measurement_t *sample)
{
    /* 先用保守分段表；台架取得真实效率图后只替换此表，不改充电/MPPT结构。 */
    if (sample->pv_power_mw < 20000)
    {
        return 27853U; /* 85% */
    }
    if (sample->pv_power_mw < 100000)
    {
        return 29491U; /* 90% */
    }
    return 30147U;     /* 92% */
}

void aurora_app_init(aurora_app_t *app,
                     const aurora_measurement_calibration_t *calibration,
                     uint32_t now_ms)
{
    if (app == NULL)
    {
        return;
    }
    memset(app, 0, sizeof(*app));
    aurora_measurement_init(&app->measurement, calibration);
    aurora_mppt_init(&app->mppt);
    aurora_protection_init(&app->protection, now_ms);
    aurora_power_stage_init(&app->power_stage, now_ms);
    aurora_ui_init(&app->ui);
    aurora_protocol_init(&app->protocol);
    aurora_storage_init_defaults(&app->storage);
    aurora_charger_init(&app->charger,
                        app->storage.settings.chemistry,
                        app->storage.settings.pack,
                        now_ms);
    app->last_step_ms = now_ms;
    app->last_10ms = now_ms;
}

void aurora_app_apply_settings(aurora_app_t *app,
                               const aurora_persistent_settings_t *settings,
                               uint32_t now_ms)
{
    if ((app == NULL) || (settings == NULL) ||
        (settings->chemistry >= AURORA_CHEM_COUNT) ||
        (settings->pack >= AURORA_PACK_COUNT))
    {
        return;
    }
    app->storage.settings = *settings;
    aurora_charger_init(&app->charger, settings->chemistry, settings->pack, now_ms);
    aurora_mppt_reset(&app->mppt);
}

void aurora_app_on_adc_block(aurora_app_t *app,
                             const uint16_t *raw,
                             size_t word_count,
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
}

void aurora_app_on_fast_fault(aurora_app_t *app,
                              uint32_t fault_mask,
                              uint32_t now_ms)
{
    if (app != NULL)
    {
        aurora_protection_latch_fast_fault(&app->protection, fault_mask, now_ms);
    }
}

void aurora_app_step_1ms(aurora_app_t *app, uint32_t now_ms)
{
    bool weak_light;
    bool thermal_limited;
    uint32_t charge_limit;
    uint32_t elapsed_step_ms;

    if (app == NULL)
    {
        return;
    }

    elapsed_step_ms = now_ms - app->last_step_ms;
    if (elapsed_step_ms == 0U)
    {
        return;
    }
    /* 看门狗会处理长时间卡死；这里限幅只防止调试暂停后把整段时间误计入能量。 */
    if (elapsed_step_ms > 1000U)
    {
        elapsed_step_ms = 1000U;
    }
    app->last_step_ms = now_ms;

    (void)aurora_measurement_read(&app->measurement, &app->sample);
    if (app->sample.pv_power_mw > 0)
    {
        app->energy_accumulator_mw_ms +=
            (uint64_t)(uint32_t)app->sample.pv_power_mw * elapsed_step_ms;
        while (app->energy_accumulator_mw_ms >= 3600000000ULL)
        {
            app->energy_accumulator_mw_ms -= 3600000000ULL;
            app->storage.settings.lifetime_energy_wh++;
            app->storage.settings.daily_energy_wh++;
            aurora_storage_mark_dirty(&app->storage, now_ms);
        }
    }

    aurora_measurement_estimate_battery_current(&app->sample,
                                                estimate_efficiency_q15(&app->sample),
                                                app->power_stage.relay_closed,
                                                app->power_stage.state == AURORA_POWER_RELAY_SETTLE);

    aurora_protection_step(&app->protection,
                           &app->sample,
                           &app->charger.profile,
                           now_ms);

    if ((now_ms - app->last_10ms) >= 10U)
    {
        weak_light = app->sample.pv_power_mw < AURORA_NO_SUN_RECOVER_MW;
        thermal_limited = ((app->sample.valid_mask & AURORA_MEAS_VALID_MOS_TEMP) != 0U) &&
                          (app->sample.mos_temp_dC > 750);
        app->charge_output = aurora_charger_step(&app->charger,
                                                 &app->sample,
                                                 weak_light,
                                                 thermal_limited,
                                                 now_ms);
        charge_limit = app->charge_output.power_limit_mw;
        if ((app->sample.pv_voltage_mv > 0) &&
            (((uint64_t)(uint32_t)app->sample.pv_voltage_mv *
              (uint32_t)AURORA_PV_CURRENT_LIMIT_MA) / 1000ULL < charge_limit))
        {
            charge_limit = (uint32_t)(((uint64_t)(uint32_t)app->sample.pv_voltage_mv *
                                       (uint32_t)AURORA_PV_CURRENT_LIMIT_MA) / 1000ULL);
        }
        if (charge_limit > AURORA_RATED_POWER_MW)
        {
            charge_limit = AURORA_RATED_POWER_MW;
        }
        app->mppt_output = aurora_mppt_step(&app->mppt,
                                            &app->sample,
                                            charge_limit,
                                            thermal_limited ||
                                                !app->charge_output.allow_charge ||
                                                ((charge_limit > AURORA_MPPT_P_NOISE_MW) &&
                                                 ((uint32_t)((app->sample.pv_power_mw > 0) ?
                                                     app->sample.pv_power_mw : 0) >=
                                                  (charge_limit - AURORA_MPPT_P_NOISE_MW))),
                                            now_ms);
        app->ui_output = aurora_ui_step(&app->ui,
                                        app->power_stage.state,
                                        app->protection.latched_mask,
                                        now_ms - app->last_10ms);
        app->last_10ms = now_ms;
    }

    app->power_command = aurora_power_stage_step(&app->power_stage,
                                                 &app->sample,
                                                 &app->mppt_output,
                                                 &app->charge_output,
                                                 aurora_protection_is_safe(&app->protection),
                                                 now_ms);
}

void aurora_app_on_protocol_frame(aurora_app_t *app,
                                  const aurora_protocol_frame_t *frame,
                                  aurora_protocol_frame_t *response,
                                  bool *has_response,
                                  uint32_t now_ms)
{
    if ((app == NULL) || (frame == NULL) || (response == NULL) || (has_response == NULL))
    {
        return;
    }
    *has_response = false;

    if ((frame->resource == AURORA_PROTOCOL_RESOURCE_SETTING) &&
        (frame->action == 0x02U))
    {
        uint8_t result = 1U;
        if ((frame->data_length == 2U) &&
            (frame->data[0] < AURORA_CHEM_COUNT) &&
            (frame->data[1] < AURORA_PACK_COUNT))
        {
            app->storage.settings.chemistry = (aurora_battery_chem_t)frame->data[0];
            app->storage.settings.pack = (aurora_battery_pack_t)frame->data[1];
            app->storage.settings.settings_revision++;
            aurora_storage_mark_dirty(&app->storage, now_ms);
            aurora_charger_init(&app->charger,
                                app->storage.settings.chemistry,
                                app->storage.settings.pack,
                                now_ms);
            result = 0U;
        }
        memset(response, 0, sizeof(*response));
        response->action = 0x82U;
        response->resource = frame->resource;
        response->message_id = frame->message_id;
        response->data_length = 1U;
        response->data[0] = result;
        *has_response = true;
    }
    else if ((frame->resource == AURORA_PROTOCOL_RESOURCE_RESET) &&
             (frame->action == 0x02U))
    {
        app->storage.settings.lifetime_energy_wh = 0U;
        app->storage.settings.daily_energy_wh = 0U;
        aurora_storage_mark_dirty(&app->storage, now_ms);
        memset(response, 0, sizeof(*response));
        response->action = 0x82U;
        response->resource = frame->resource;
        response->message_id = frame->message_id;
        response->data_length = 1U;
        response->data[0] = 0U;
        *has_response = true;
    }
}
