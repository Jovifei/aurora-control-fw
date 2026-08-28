#include "app.h"

#include "app_config.h"

#include <string.h>

/* 弱光判定仅用于暂停尾流和MPPT搜索，不等价于13V无太阳关机条件。 */
#define APP_WEAK_LIGHT_POWER_MW                    (3000L)

/*---------------------------------------------------------------------------*
 * Name        : static uint32_t min_u32(uint32_t a, uint32_t b)
 * Input       : a/b - 两个无符号32位值
 * Output      : 较小值
 * Description : 组合电池、输入电流、低输入电压、温度和额定功率包络时统一取更严格上限。
 *---------------------------------------------------------------------------*/
static uint32_t min_u32(uint32_t a, uint32_t b)
{
    return (a < b) ? a : b;
}

/*---------------------------------------------------------------------------*
 * Name        : static uint16_t estimate_efficiency_q15(
 *               const aurora_measurement_t *sample)
 * Input       : sample - 最新测量快照
 * Output      : 当前功率区间的保守效率估计，Q15
 * Description : 无BAT_I通道时按PV功率分段估算效率；实测效率图确认后只调整集中参数。
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
 * Name        : static uint32_t input_voltage_power_limit_mw(int32_t pv_voltage_mv)
 * Input       : pv_voltage_mv - PV电压，mV
 * Output      : 低输入电压包络允许的最大PV功率，mW
 * Description : 继承V2.7：<=12V为50W，>=17V为当前BOM额定功率，中间线性插值。
 *---------------------------------------------------------------------------*/
static uint32_t input_voltage_power_limit_mw(int32_t pv_voltage_mv)
{
    const uint32_t floor_mw = min_u32(AURORA_LOW_PV_POWER_FLOOR_MW,
                                      AURORA_RATED_POWER_MW);

    if (pv_voltage_mv <= AURORA_LOW_PV_POWER_START_MV)
    {
        return floor_mw;
    }
    if (pv_voltage_mv >= AURORA_LOW_PV_POWER_FULL_MV)
    {
        return AURORA_RATED_POWER_MW;
    }

    return floor_mw +
           (uint32_t)(((uint64_t)(uint32_t)(pv_voltage_mv - AURORA_LOW_PV_POWER_START_MV) *
                       (AURORA_RATED_POWER_MW - floor_mw)) /
                      (uint32_t)(AURORA_LOW_PV_POWER_FULL_MV -
                                 AURORA_LOW_PV_POWER_START_MV));
}

/*---------------------------------------------------------------------------*
 * Name        : static uint32_t thermal_power_limit_mw(
 *               const aurora_measurement_t *sample)
 * Input       : sample - 最新测量快照
 * Output      : 当前MOS温度允许的PV功率，mW
 * Description : 用户确认95°C开始降额，104°C降到候选最小功率；105°C保护由Protection独立处理。
 *---------------------------------------------------------------------------*/
static uint32_t thermal_power_limit_mw(const aurora_measurement_t *sample)
{
    if ((sample->valid_mask & AURORA_MEAS_VALID_MOS_TEMP) == 0U)
    {
        return AURORA_RATED_POWER_MW;
    }
    if (sample->mos_temp_dC <= AURORA_MOS_DERATE_TEMP_DC)
    {
        return AURORA_RATED_POWER_MW;
    }
    if (sample->mos_temp_dC >= AURORA_MOS_DERATE_END_TEMP_DC)
    {
        return min_u32(AURORA_THERMAL_MIN_POWER_MW, AURORA_RATED_POWER_MW);
    }

    {
        const uint32_t minimum_mw = min_u32(AURORA_THERMAL_MIN_POWER_MW,
                                             AURORA_RATED_POWER_MW);
        const uint32_t temperature_span_dC =
            (uint32_t)(AURORA_MOS_DERATE_END_TEMP_DC - AURORA_MOS_DERATE_TEMP_DC);
        const uint32_t elapsed_dC =
            (uint32_t)(sample->mos_temp_dC - AURORA_MOS_DERATE_TEMP_DC);
        const uint32_t reduction_mw =
            (uint32_t)(((uint64_t)(AURORA_RATED_POWER_MW - minimum_mw) * elapsed_dC) /
                       temperature_span_dC);
        return AURORA_RATED_POWER_MW - reduction_mw;
    }
}

/*---------------------------------------------------------------------------*
 * Name        : static uint32_t battery_to_pv_power_mw(uint32_t battery_power_mw,
 *               uint16_t efficiency_q15)
 * Input       : battery_power_mw - 电池侧目标功率；efficiency_q15 - 估算效率
 * Output      : 达成电池目标所需的PV侧功率，mW
 * Description : Ppv≈Pbat/η；避免旧实现把Vbat×Ibat直接误当PV输入功率上限。
 *---------------------------------------------------------------------------*/
static uint32_t battery_to_pv_power_mw(uint32_t battery_power_mw,
                                       uint16_t efficiency_q15)
{
    uint64_t pv_power_mw;

    if ((battery_power_mw == 0U) || (efficiency_q15 == 0U))
    {
        return 0U;
    }

    pv_power_mw = ((uint64_t)battery_power_mw * AURORA_DUTY_Q15_ONE +
                   efficiency_q15 - 1U) /
                  efficiency_q15;
    return (pv_power_mw > AURORA_RATED_POWER_MW) ?
               AURORA_RATED_POWER_MW : (uint32_t)pv_power_mw;
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_app_init(aurora_app_t *app,
 *               const aurora_measurement_calibration_t *calibration,
 *               uint32_t now_ms)
 * Input       : app - 应用总上下文；calibration - 六通道标定；now_ms - 当前毫秒
 * Output      : 无
 * Description : 按依赖顺序初始化测量、MPPT、保护、功率级、UI、协议、存储和充电器。
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
 * Input       : app - 应用总上下文；settings - 已校验设置；now_ms - 当前毫秒
 * Output      : 无
 * Description : 切换电池档案时撤销旧PI/Duty/启动状态，并要求重新零点校准、预充和电池稳定验证。
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
    aurora_charger_init(&app->charger, settings->chemistry, settings->pack, now_ms);
    aurora_mppt_reset(&app->mppt);
    aurora_power_stage_init(&app->power_stage, now_ms);
    aurora_measurement_zero_cal_reset(&app->measurement);
    memset(&app->charge_output, 0, sizeof(app->charge_output));
    memset(&app->mppt_output, 0, sizeof(app->mppt_output));
    memset(&app->power_command, 0, sizeof(app->power_command));
    app->link_request = false;
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_app_on_adc_block(aurora_app_t *app,
 *               const uint16_t *raw, size_t word_count, uint32_t timestamp_ms)
 * Input       : app - 应用总上下文；raw - 完整DMA块；word_count - 块字数；timestamp_ms - 完成时间
 * Output      : 无
 * Description : 处理完整DMA块；ZERO_CAL阶段且PV已稳定2s时额外累计PV_I运行时零点。
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

    if ((app->power_stage.state == AURORA_POWER_ZERO_CAL) &&
        !app->power_stage.relay_closed &&
        (app->power_stage.pv_valid_since_ms != 0U) &&
        ((timestamp_ms - app->power_stage.pv_valid_since_ms) >=
         AURORA_ZERO_CAL_PV_STABLE_MS))
    {
        (void)aurora_measurement_zero_cal_accumulate(&app->measurement,
                                                     raw,
                                                     word_count);
    }
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_app_on_fast_fault(aurora_app_t *app,
 *               uint32_t fault_mask, uint32_t now_ms)
 * Input       : app - 应用总上下文；fault_mask - ISR快速故障；now_ms - 当前毫秒
 * Output      : 无
 * Description : 把真正运行阶段的快速硬件故障纳入统一锁存；恢复由Service和保护策略共同裁决。
 *---------------------------------------------------------------------------*/
void aurora_app_on_fast_fault(aurora_app_t *app,
                              uint32_t fault_mask,
                              uint32_t now_ms)
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
 *               boost_output_active - Service提供的物理PWM实际输出状态
 * Output      : 无
 * Description : 1ms执行保护和Power Stage；10ms更新Charger/MPPT/UI，并把电池侧目标换算为PV侧功率包络。
 *---------------------------------------------------------------------------*/
void aurora_app_step_1ms(aurora_app_t *app,
                         uint32_t now_ms,
                         bool boost_output_active)
{
    uint32_t elapsed_step_ms;
    aurora_power_state_t previous_power_state;

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

    /* RELAY_SETTLE/BAT_STABILITY阶段虽已闭合继电器，但PWM为0，不发布BAT_I_EST。 */
    aurora_measurement_estimate_battery_current(
        &app->sample,
        estimate_efficiency_q15(&app->sample),
        app->power_stage.relay_closed,
        (app->power_stage.state == AURORA_POWER_RELAY_SETTLE) ||
        (app->power_stage.state == AURORA_POWER_BAT_STABILITY));

    aurora_protection_step(&app->protection,
                           &app->sample,
                           &app->charger.profile,
                           boost_output_active,
                           now_ms);

    if ((now_ms - app->last_10ms) >= AURORA_CONTROL_PERIOD_MS)
    {
        const uint32_t elapsed_control_ms = now_ms - app->last_10ms;
        const bool weak_light = app->sample.pv_power_mw < APP_WEAK_LIGHT_POWER_MW;
        const uint32_t thermal_limit_mw = thermal_power_limit_mw(&app->sample);
        const uint32_t voltage_limit_mw =
            input_voltage_power_limit_mw(app->sample.pv_voltage_mv);
        uint32_t current_limit_mw = AURORA_RATED_POWER_MW;
        uint32_t hardware_limit_mw;
        uint32_t pv_required_mw;
        uint16_t efficiency_q15;
        bool input_limited_previous = app->charge_output.input_limited;
        bool thermal_limited = thermal_limit_mw < AURORA_RATED_POWER_MW;
        bool external_limited;

        if (app->sample.pv_voltage_mv > 0)
        {
            const uint64_t current_power =
                ((uint64_t)(uint32_t)app->sample.pv_voltage_mv *
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

        app->charge_output = aurora_charger_step(&app->charger,
                                                 &app->sample,
                                                 weak_light,
                                                 thermal_limited,
                                                 input_limited_previous,
                                                 now_ms);

        efficiency_q15 = estimate_efficiency_q15(&app->sample);
        pv_required_mw = battery_to_pv_power_mw(
            app->charge_output.battery_power_target_mw, efficiency_q15);
        app->charge_output.pv_power_limit_mw =
            min_u32(pv_required_mw, hardware_limit_mw);
        app->charge_output.input_limited =
            app->charge_output.allow_charge && (pv_required_mw > hardware_limit_mw);
        app->charge_output.thermal_limited = thermal_limited;

        external_limited = thermal_limited ||
                           app->charge_output.input_limited ||
                           !app->charge_output.allow_charge;
        app->mppt_output = aurora_mppt_step(&app->mppt,
                                            &app->sample,
                                            app->charge_output.pv_power_limit_mw,
                                            external_limited,
                                            now_ms);
        app->ui_output = aurora_ui_step(&app->ui,
                                        app->power_stage.state,
                                        aurora_protection_fault_mask(&app->protection),
                                        elapsed_control_ms);
        app->last_10ms = now_ms;
    }

    previous_power_state = app->power_stage.state;
    app->power_command = aurora_power_stage_step(
        &app->power_stage,
        &app->sample,
        &app->mppt_output,
        &app->charge_output,
        aurora_protection_is_safe(&app->protection),
        aurora_measurement_zero_cal_ready(&app->measurement),
        aurora_measurement_zero_cal_failed(&app->measurement),
        now_ms);

    /* 每次重新进入启动延时都重新建立本轮PV_I零点，禁止跨故障沿用旧运行时校准。 */
    if ((app->power_stage.state == AURORA_POWER_START_DELAY) &&
        (previous_power_state != AURORA_POWER_START_DELAY))
    {
        aurora_measurement_zero_cal_reset(&app->measurement);
    }

    /* V2.7 Link：PV>=13V或功率流程已经启动时保持ON；真正无PV后关闭。 */
    app->link_request =
        (app->sample.pv_voltage_mv >= AURORA_PV_START_MIN_MV) ||
        (app->power_stage.state == AURORA_POWER_PRECHARGE) ||
        (app->power_stage.state == AURORA_POWER_RELAY_SETTLE) ||
        (app->power_stage.state == AURORA_POWER_BAT_STABILITY) ||
        (app->power_stage.state == AURORA_POWER_RUN);
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_app_on_protocol_frame(aurora_app_t *app,
 *               const aurora_protocol_frame_t *frame,
 *               aurora_protocol_frame_t *response, bool *has_response,
 *               uint32_t now_ms)
 * Input       : app - 应用总上下文；frame - 已校验请求；response - 应答；has_response - 应答标志；now_ms - 当前毫秒
 * Output      : 无；通过response/has_response返回可选应答
 * Description : 处理电池档案设置和能量复位；运行期改档案立即撤销旧控制并重新完整启动。
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
