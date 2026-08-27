#include "app.h"

#include "app_config.h"

#include <string.h>

/*---------------------------------------------------------------------------*
 * Name        : static uint16_t estimate_efficiency_q15(
 *               const aurora_measurement_t *sample)
 * Input       : sample - 最新测量快照
 * Output      : 当前功率区间的保守效率估计，Q15
 * Description : 无BAT_I通道时按PV功率分段估算效率；实测效率图确认后只调整集中参数，不改控制结构。
 *---------------------------------------------------------------------------*/
static uint16_t estimate_efficiency_q15(const aurora_measurement_t *sample)
{
    if (sample->pv_power_mw < AURORA_EFFICIENCY_LOW_LIMIT_MW)
    {
        return AURORA_EFFICIENCY_LOW_Q15;
    }
    if (sample->pv_power_mw < AURORA_EFFICIENCY_MID_LIMIT_MW)
    {
        return AURORA_EFFICIENCY_MID_Q15;
    }
    return AURORA_EFFICIENCY_HIGH_Q15;
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_app_init(aurora_app_t *app,
 *               const aurora_measurement_calibration_t *calibration,
 *               uint32_t now_ms)
 * Input       : app - 应用总上下文；calibration - 六通道测量标定；
 *               now_ms - 当前毫秒时间戳
 * Output      : 无
 * Description : 清零应用状态并按依赖顺序初始化测量、MPPT、保护、功率级、UI、协议、存储和充电器。
 *---------------------------------------------------------------------------*/
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

/*---------------------------------------------------------------------------*
 * Name        : void aurora_app_apply_settings(aurora_app_t *app,
 *               const aurora_persistent_settings_t *settings,
 *               uint32_t now_ms)
 * Input       : app - 应用总上下文；settings - 已校验的持久化设置；
 *               now_ms - 当前毫秒时间戳
 * Output      : 无
 * Description : 应用电池档案并安全复位充电、MPPT和功率状态，禁止旧参数下的积分或Duty跨配置继续运行。
 *---------------------------------------------------------------------------*/
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
    aurora_charger_init(&app->charger,
                        settings->chemistry,
                        settings->pack,
                        now_ms);
    aurora_mppt_reset(&app->mppt);
    aurora_power_stage_init(&app->power_stage, now_ms);

    /* 立即撤销上一次控制周期产生的命令，Service本轮即可执行关波。 */
    memset(&app->charge_output, 0, sizeof(app->charge_output));
    memset(&app->mppt_output, 0, sizeof(app->mppt_output));
    memset(&app->power_command, 0, sizeof(app->power_command));
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_app_on_adc_block(aurora_app_t *app,
 *               const uint16_t *raw, size_t word_count,
 *               uint32_t timestamp_ms)
 * Input       : app - 应用总上下文；raw - 完整ADC DMA块；word_count - 块内字数；
 *               timestamp_ms - DMA完成时间戳
 * Output      : 无
 * Description : 处理Service发布的完整ADC块；仅在换算成功后更新应用层测量快照。
 *---------------------------------------------------------------------------*/
void aurora_app_on_adc_block(aurora_app_t *app,
                             const uint16_t *raw,
                             size_t word_count,
                             uint32_t timestamp_ms)
{
    if (app == NULL)
    {
        return;
    }

    if (aurora_measurement_process_block(&app->measurement,
                                         raw,
                                         word_count,
                                         timestamp_ms) == AURORA_STATUS_OK)
    {
        (void)aurora_measurement_read(&app->measurement, &app->sample);
    }
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_app_on_fast_fault(aurora_app_t *app,
 *               uint32_t fault_mask, uint32_t now_ms)
 * Input       : app - 应用总上下文；fault_mask - ISR快速故障位；
 *               now_ms - 当前毫秒时间戳
 * Output      : 无
 * Description : 把ISR事件纳入统一保护锁存；本函数只记录故障，不执行任何恢复或重新发波。
 *---------------------------------------------------------------------------*/
void aurora_app_on_fast_fault(aurora_app_t *app,
                              uint32_t fault_mask,
                              uint32_t now_ms)
{
    if (app != NULL)
    {
        aurora_protection_latch_fast_fault(&app->protection,
                                           fault_mask,
                                           now_ms);
    }
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_app_step_1ms(aurora_app_t *app, uint32_t now_ms)
 * Input       : app - 应用总上下文；now_ms - 当前毫秒时间戳
 * Output      : 无
 * Description : 1ms应用调度入口：累计能量、估算BAT_I、执行保护；每10ms更新充电、MPPT和UI，再生成功率命令。
 *---------------------------------------------------------------------------*/
void aurora_app_step_1ms(aurora_app_t *app, uint32_t now_ms)
{
    bool weak_light;
    bool thermal_limited;
    bool external_limited;
    uint32_t charge_limit_mw;
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

    /* 调试暂停不应把整段墙上时间错误计入发电量。 */
    if (elapsed_step_ms > AURORA_MAX_ELAPSED_STEP_MS)
    {
        elapsed_step_ms = AURORA_MAX_ELAPSED_STEP_MS;
    }
    app->last_step_ms = now_ms;

    (void)aurora_measurement_read(&app->measurement, &app->sample);

    /* 使用mW·ms整型累计，达到1Wh后再更新持久化计数。 */
    if (app->sample.pv_power_mw > 0)
    {
        app->energy_accumulator_mw_ms +=
            (uint64_t)(uint32_t)app->sample.pv_power_mw * elapsed_step_ms;
        while (app->energy_accumulator_mw_ms >= AURORA_ONE_WH_MW_MS)
        {
            app->energy_accumulator_mw_ms -= AURORA_ONE_WH_MW_MS;
            app->storage.settings.lifetime_energy_wh++;
            app->storage.settings.daily_energy_wh++;
            aurora_storage_mark_dirty(&app->storage, now_ms);
        }
    }

    /* BAT_I为估算量；继电器瞬态或断开时必须撤销有效标记。 */
    aurora_measurement_estimate_battery_current(
        &app->sample,
        estimate_efficiency_q15(&app->sample),
        app->power_stage.relay_closed,
        app->power_stage.state == AURORA_POWER_RELAY_SETTLE);

    aurora_protection_step(&app->protection,
                           &app->sample,
                           &app->charger.profile,
                           now_ms);

    if ((now_ms - app->last_10ms) >= AURORA_CONTROL_PERIOD_MS)
    {
        const uint32_t elapsed_control_ms = now_ms - app->last_10ms;

        weak_light = app->sample.pv_power_mw < AURORA_NO_SUN_RECOVER_MW;
        thermal_limited =
            ((app->sample.valid_mask & AURORA_MEAS_VALID_MOS_TEMP) != 0U) &&
            (app->sample.mos_temp_dC > AURORA_MOS_DERATE_TEMP_DC);

        app->charge_output = aurora_charger_step(&app->charger,
                                                 &app->sample,
                                                 weak_light,
                                                 thermal_limited,
                                                 now_ms);

        /* 允许功率取充电阶段、PV电流和BOM额定功率三者的最小值。 */
        charge_limit_mw = app->charge_output.power_limit_mw;
        if (app->sample.pv_voltage_mv > 0)
        {
            const uint32_t pv_current_power_limit_mw =
                (uint32_t)(((uint64_t)(uint32_t)app->sample.pv_voltage_mv *
                            (uint32_t)AURORA_PV_CURRENT_LIMIT_MA) /
                           AURORA_MV_MA_PER_MW);

            if (pv_current_power_limit_mw < charge_limit_mw)
            {
                charge_limit_mw = pv_current_power_limit_mw;
            }
        }
        if (charge_limit_mw > AURORA_RATED_POWER_MW)
        {
            charge_limit_mw = AURORA_RATED_POWER_MW;
        }

        /* 已受外部包络限制时冻结MPPT参考搜索，避免错误追踪限幅后的工作点。 */
        external_limited =
            thermal_limited ||
            !app->charge_output.allow_charge ||
            ((charge_limit_mw > AURORA_MPPT_P_NOISE_MW) &&
             ((uint32_t)((app->sample.pv_power_mw > 0) ?
                             app->sample.pv_power_mw : 0) >=
              (charge_limit_mw - AURORA_MPPT_P_NOISE_MW)));

        app->mppt_output = aurora_mppt_step(&app->mppt,
                                            &app->sample,
                                            charge_limit_mw,
                                            external_limited,
                                            now_ms);
        app->ui_output = aurora_ui_step(&app->ui,
                                        app->power_stage.state,
                                        app->protection.latched_mask,
                                        elapsed_control_ms);
        app->last_10ms = now_ms;
    }

    /* 功率状态机每1ms运行，能够及时撤销旧的PWM/继电器命令。 */
    app->power_command =
        aurora_power_stage_step(&app->power_stage,
                                &app->sample,
                                &app->mppt_output,
                                &app->charge_output,
                                aurora_protection_is_safe(&app->protection),
                                now_ms);
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_app_on_protocol_frame(aurora_app_t *app,
 *               const aurora_protocol_frame_t *frame,
 *               aurora_protocol_frame_t *response, bool *has_response,
 *               uint32_t now_ms)
 * Input       : app - 应用总上下文；frame - 已校验请求帧；response - 应答帧输出；
 *               has_response - 应答有效标志；now_ms - 当前毫秒时间戳
 * Output      : 无；通过response和has_response返回可选应答
 * Description : 处理电池档案设置和能量复位命令；运行期改档案会立即撤销旧控制状态并重新预充。
 *---------------------------------------------------------------------------*/
void aurora_app_on_protocol_frame(aurora_app_t *app,
                                  const aurora_protocol_frame_t *frame,
                                  aurora_protocol_frame_t *response,
                                  bool *has_response,
                                  uint32_t now_ms)
{
    if ((app == NULL) || (frame == NULL) ||
        (response == NULL) || (has_response == NULL))
    {
        return;
    }

    *has_response = false;

    if ((frame->resource == AURORA_PROTOCOL_RESOURCE_SETTING) &&
        (frame->action == AURORA_PROTOCOL_ACTION_WRITE))
    {
        uint8_t result = AURORA_PROTOCOL_RESULT_INVALID;

        if ((frame->data_length == AURORA_PROTOCOL_SETTING_DATA_LENGTH) &&
            (frame->data[0] < AURORA_CHEM_COUNT) &&
            (frame->data[1] < AURORA_PACK_COUNT))
        {
            aurora_persistent_settings_t settings = app->storage.settings;

            settings.chemistry = (aurora_battery_chem_t)frame->data[0];
            settings.pack = (aurora_battery_pack_t)frame->data[1];
            settings.settings_revision++;
            aurora_app_apply_settings(app, &settings, now_ms);
            aurora_storage_mark_dirty(&app->storage, now_ms);
            result = AURORA_PROTOCOL_RESULT_OK;
        }

        memset(response, 0, sizeof(*response));
        response->action = AURORA_PROTOCOL_ACTION_RESPONSE;
        response->resource = frame->resource;
        response->message_id = frame->message_id;
        response->data_length = AURORA_PROTOCOL_RESULT_DATA_LENGTH;
        response->data[0] = result;
        *has_response = true;
    }
    else if ((frame->resource == AURORA_PROTOCOL_RESOURCE_RESET) &&
             (frame->action == AURORA_PROTOCOL_ACTION_WRITE))
    {
        app->storage.settings.lifetime_energy_wh = 0U;
        app->storage.settings.daily_energy_wh = 0U;
        app->energy_accumulator_mw_ms = 0U;
        aurora_storage_mark_dirty(&app->storage, now_ms);

        memset(response, 0, sizeof(*response));
        response->action = AURORA_PROTOCOL_ACTION_RESPONSE;
        response->resource = frame->resource;
        response->message_id = frame->message_id;
        response->data_length = AURORA_PROTOCOL_RESULT_DATA_LENGTH;
        response->data[0] = AURORA_PROTOCOL_RESULT_OK;
        *has_response = true;
    }
}
