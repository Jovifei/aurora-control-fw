#include "protection.h"

#include "app_config.h"

#include <limits.h>
#include <string.h>

/* Protection定时器语义索引；每项保护拥有独立真实毫秒计时。 */
typedef enum
{
    TIMER_PV_UV_TRIP = 0,
    TIMER_PV_UV_RECOVER,
    TIMER_PV_OV_TRIP,
    TIMER_PV_OV_RECOVER,
    TIMER_BAT_UV_TRIP,
    TIMER_BAT_UV_RECOVER,
    TIMER_BAT_OV_SLOW,
    TIMER_BAT_OV_MEDIUM,
    TIMER_BAT_OV_FAST,
    TIMER_BAT_OV_ABSOLUTE,
    TIMER_BAT_OV_RECOVER,
    TIMER_PV_OCP_SLOW,
    TIMER_PV_OCP_MEDIUM,
    TIMER_PV_OCP_FAST,
    TIMER_PV_OCP_RECOVER,
    TIMER_PV_POWER_TRIP,
    TIMER_PV_POWER_RECOVER,
    TIMER_MOS_TEMP_TRIP,
    TIMER_MOS_TEMP_RECOVER,
    TIMER_AMB_HIGH_TRIP,
    TIMER_AMB_LOW_TRIP,
    TIMER_AMB_TEMP_RECOVER,
    TIMER_MOS_NTC_OPEN_TRIP,
    TIMER_MOS_NTC_OPEN_RECOVER,
    TIMER_MOS_NTC_SHORT_TRIP,
    TIMER_MOS_NTC_SHORT_RECOVER,
    TIMER_AMB_NTC_OPEN_TRIP,
    TIMER_AMB_NTC_OPEN_RECOVER,
    TIMER_AMB_NTC_SHORT_TRIP,
    TIMER_AMB_NTC_SHORT_RECOVER,
    TIMER_BUS_ADC_SAT_RECOVER,
    TIMER_PV_I_RUN_NEG_TRIP,
    TIMER_PV_I_OFF_ABS_TRIP,
    TIMER_PV_I_PLAUS_RECOVER
} protection_timer_index_t;

/*---------------------------------------------------------------------------*
 * Name        : static bool timer_elapsed(aurora_protection_ctx_t *ctx,
 *               protection_timer_index_t index, bool condition,
 *               uint32_t delay_ms, uint32_t now_ms)
 * Input       : ctx - 保护上下文；index - 独立定时器；condition - 当前条件；
 *               delay_ms - 连续成立时间；now_ms - 当前毫秒
 * Output      : true表示条件已连续成立达到指定毫秒数
 * Description : 条件断开立即复位计时，使用无符号时间差兼容32位自然回绕。
 *---------------------------------------------------------------------------*/
static bool timer_elapsed(aurora_protection_ctx_t *ctx, protection_timer_index_t index,
                          bool condition, uint32_t delay_ms, uint32_t now_ms)
{
    aurora_condition_timer_t *timer = &ctx->timer[(size_t)index];

    if (!condition)
    {
        timer->timing = false;
        timer->since_ms = now_ms;
        return false;
    }

    if (!timer->timing)
    {
        timer->timing = true;
        timer->since_ms = now_ms;
        return delay_ms == 0U;
    }

    return (now_ms - timer->since_ms) >= delay_ms;
}

/*---------------------------------------------------------------------------*
 * Name        : static void set_fault(aurora_protection_ctx_t *ctx,
 *               uint32_t mask, bool latch, uint32_t now_ms)
 * Input       : ctx - 保护上下文；mask - 故障位；latch - 是否历史锁存；now_ms - 当前毫秒
 * Output      : 无
 * Description : 设置active及可选latched位；首次出现时递增epoch使旧PWM授权立即作废。
 *---------------------------------------------------------------------------*/
static void set_fault(aurora_protection_ctx_t *ctx, uint32_t mask, bool latch, uint32_t now_ms)
{
    const uint32_t before = ctx->active_mask | ctx->latched_mask;

    ctx->active_mask |= mask;
    if (latch)
    {
        ctx->latched_mask |= mask;
    }

    if ((before & mask) == 0U)
    {
        if (before == 0U)
        {
            ctx->first_fault_ms = now_ms;
        }
        ctx->epoch++;
    }
}

/*---------------------------------------------------------------------------*
 * Name        : static void clear_auto_fault(aurora_protection_ctx_t *ctx,
 *               uint32_t mask)
 * Input       : ctx - 保护上下文；mask - 自动恢复故障位
 * Output      : 无
 * Description : 清除已满足恢复时间的active/latched位，并递增epoch废弃恢复前的授权。
 *---------------------------------------------------------------------------*/
static void clear_auto_fault(aurora_protection_ctx_t *ctx, uint32_t mask)
{
    if (((ctx->active_mask | ctx->latched_mask) & mask) != 0U)
    {
        ctx->active_mask &= ~mask;
        ctx->latched_mask &= ~mask;
        ctx->epoch++;
    }
}

/*---------------------------------------------------------------------------*
 * Name        : static int32_t current_threshold_ma(int32_t base_ma,
 *               int32_t numerator)
 * Input       : base_ma - 基础限流，mA；numerator - 百分比比例分子
 * Output      : 分级软件过流阈值，mA
 * Description : 按V2.7的1.2/1.35/1.5倍结构计算当前BOM保护阈值。
 *---------------------------------------------------------------------------*/
static int32_t current_threshold_ma(int32_t base_ma, int32_t numerator)
{
    return (int32_t)(((int64_t)base_ma * numerator) / AURORA_PV_OCP_RATIO_DEN);
}

/*---------------------------------------------------------------------------*
 * Name        : static int32_t abs_current_ma(int32_t current_ma)
 * Input       : current_ma - 有符号PV电流
 * Output      : 饱和绝对值
 * Description : PV_I合理性诊断避免INT32_MIN取反溢出。
 *---------------------------------------------------------------------------*/
static int32_t abs_current_ma(int32_t current_ma)
{
    if (current_ma == INT32_MIN)
    {
        return INT32_MAX;
    }
    return (current_ma < 0) ? -current_ma : current_ma;
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_protection_init(aurora_protection_ctx_t *ctx,
 *               uint32_t now_ms)
 * Input       : ctx - 保护上下文；now_ms - 当前毫秒
 * Output      : 无
 * Description : 清零故障和所有独立定时器，建立初始安全epoch与首测量宽限起点。
 *---------------------------------------------------------------------------*/
void aurora_protection_init(aurora_protection_ctx_t *ctx, uint32_t now_ms)
{
    if (ctx == NULL)
    {
        return;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->epoch = 1U;
    ctx->startup_ms = now_ms;
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_protection_latch_fast_fault(
 *               aurora_protection_ctx_t *ctx, uint32_t fault_mask,
 *               uint32_t now_ms)
 * Input       : ctx - 保护上下文；fault_mask - ISR快速故障位；now_ms - 当前毫秒
 * Output      : 无
 * Description : 比较器、Break、DMA和内部快速路径采用active+latched硬锁存，禁止自动重发波。
 *---------------------------------------------------------------------------*/
void aurora_protection_latch_fast_fault(aurora_protection_ctx_t *ctx, uint32_t fault_mask,
                                        uint32_t now_ms)
{
    if (ctx != NULL)
    {
        set_fault(ctx, fault_mask, true, now_ms);
    }
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_protection_latch_software_fault(
 *               aurora_protection_ctx_t *ctx, uint32_t fault_mask,
 *               bool latch, uint32_t now_ms)
 * Input       : ctx - 保护上下文；fault_mask - 软件诊断故障位；
 *               latch - true表示必须显式/延时恢复；now_ms - 当前毫秒
 * Output      : 无
 * Description :
 * 供PowerStage启动失败、BUS过压和Demo输出诊断进入统一fault_mask，禁止模块私自绕过保护仲裁。
 *---------------------------------------------------------------------------*/
void aurora_protection_latch_software_fault(aurora_protection_ctx_t *ctx, uint32_t fault_mask,
                                            bool latch, uint32_t now_ms)
{
    if ((ctx == NULL) || (fault_mask == 0U))
    {
        return;
    }
    set_fault(ctx, fault_mask, latch, now_ms);
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_protection_step_ex(aurora_protection_ctx_t *ctx,
 *               const aurora_measurement_t *sample,
 *               const aurora_charge_profile_t *profile,
 *               aurora_operating_mode_t operating_mode,
 *               bool pv_current_calibrated, bool boost_output_active,
 *               uint32_t now_ms)
 * Input       : ctx - 保护上下文；sample - 最新测量；profile - 当前电池档案；
 *               operating_mode - Battery/Demo；pv_current_calibrated - PV_I零点已完成；
 *               boost_output_active - 物理Boost PWM是否真正输出；now_ms - 当前毫秒
 * Output      : 无
 * Description : 按V2.7逐项执行PV/BAT/OCP/过功率/温度/NTC真实毫秒保护。
 *               软件PV OCP/过功率仅在物理PWM真正输出后计时。
 *---------------------------------------------------------------------------*/
void aurora_protection_step_ex(aurora_protection_ctx_t *ctx, const aurora_measurement_t *sample,
                               const aurora_charge_profile_t *profile,
                               aurora_operating_mode_t operating_mode, bool pv_current_calibrated,
                               bool boost_output_active, uint32_t now_ms)
{
    const uint32_t required = AURORA_MEAS_VALID_PV_V | AURORA_MEAS_VALID_PV_I |
                              AURORA_MEAS_VALID_BAT_V | AURORA_MEAS_VALID_BUS_V |
                              AURORA_MEAS_VALID_PV_POWER;
    bool trip;
    bool recover;
    const bool battery_mode = operating_mode == AURORA_MODE_BATTERY;

    if ((ctx == NULL) || (sample == NULL) || (profile == NULL))
    {
        return;
    }

    /* BST_U近满量程时立即进入独立故障；Measurement同时撤销BUS有效位，Relay二次复核必然拒绝闭合。 */
    if ((sample->diagnostic_mask & AURORA_MEAS_DIAG_BUS_ADC_SATURATED) != 0U)
    {
        set_fault(ctx, AURORA_FAULT_BUS_ADC_SATURATION, true, now_ms);
        (void)timer_elapsed(ctx, TIMER_BUS_ADC_SAT_RECOVER, false,
                            AURORA_BUS_ADC_SAT_RECOVER_DELAY_MS, now_ms);
        return;
    }
    if (timer_elapsed(ctx, TIMER_BUS_ADC_SAT_RECOVER,
                      (sample->diagnostic_mask & AURORA_MEAS_DIAG_BUS_ADC_SATURATED) == 0U,
                      AURORA_BUS_ADC_SAT_RECOVER_DELAY_MS, now_ms))
    {
        clear_auto_fault(ctx, AURORA_FAULT_BUS_ADC_SATURATION);
    }

    if ((sample->sequence != 0U) && ((sample->valid_mask & required) == required))
    {
        ctx->measurement_seen = true;
    }

    if (!ctx->measurement_seen)
    {
        if ((now_ms - ctx->startup_ms) > AURORA_MEASUREMENT_STARTUP_GRACE_MS)
        {
            set_fault(ctx, AURORA_FAULT_ADC_STALE, true, now_ms);
        }
        return;
    }

    if (((sample->valid_mask & required) != required) ||
        ((now_ms - sample->timestamp_ms) > AURORA_MEASUREMENT_STALE_MS))
    {
        set_fault(ctx, AURORA_FAULT_ADC_STALE, true, now_ms);
        return;
    }

    /* PV欠压/恢复：8V 1s / 9V 1s。 */
    trip = timer_elapsed(ctx, TIMER_PV_UV_TRIP, sample->pv_voltage_mv < AURORA_PV_UV_TRIP_MV,
                         AURORA_PV_UV_TRIP_DELAY_MS, now_ms);
    recover =
        timer_elapsed(ctx, TIMER_PV_UV_RECOVER, sample->pv_voltage_mv > AURORA_PV_UV_RECOVER_MV,
                      AURORA_PV_UV_RECOVER_DELAY_MS, now_ms);
    if (trip)
    {
        set_fault(ctx, AURORA_FAULT_PV_UNDERVOLT, false, now_ms);
    }
    else if (recover)
    {
        clear_auto_fault(ctx, AURORA_FAULT_PV_UNDERVOLT);
    }

    /* PV过压/恢复：55V 1s / 54V 1s。 */
    trip = timer_elapsed(ctx, TIMER_PV_OV_TRIP, sample->pv_voltage_mv > AURORA_PV_OV_TRIP_MV,
                         AURORA_PV_OV_TRIP_DELAY_MS, now_ms);
    recover =
        timer_elapsed(ctx, TIMER_PV_OV_RECOVER, sample->pv_voltage_mv < AURORA_PV_OV_RECOVER_MV,
                      AURORA_PV_OV_RECOVER_DELAY_MS, now_ms);
    if (trip)
    {
        set_fault(ctx, AURORA_FAULT_PV_OVERVOLT, false, now_ms);
    }
    else if (recover)
    {
        clear_auto_fault(ctx, AURORA_FAULT_PV_OVERVOLT);
    }

    if (battery_mode)
    {
        /* BAT欠压：只有确认接入电池后才检查，恢复点为档案下限+0.5V。 */
        trip = timer_elapsed(ctx, TIMER_BAT_UV_TRIP,
                             (sample->battery_voltage_mv > AURORA_BATTERY_CONNECTED_MIN_MV) &&
                                 ((uint32_t)sample->battery_voltage_mv < profile->battery_uv_mv),
                             AURORA_BAT_UV_TRIP_DELAY_MS, now_ms);
        recover = timer_elapsed(
            ctx, TIMER_BAT_UV_RECOVER,
            (sample->battery_voltage_mv > 0) &&
                ((uint32_t)sample->battery_voltage_mv >= profile->battery_uv_recover_mv),
            AURORA_BAT_UV_RECOVER_DELAY_MS, now_ms);
        if (trip)
        {
            set_fault(ctx, AURORA_FAULT_BAT_UNDERVOLT, false, now_ms);
        }
        else if (recover)
        {
            clear_auto_fault(ctx, AURORA_FAULT_BAT_UNDERVOLT);
        }

        /*
         * BAT多级OV：5s一级、1s加严、3ms快速、1s绝对93V。
         * 任一级成立都使用同一BAT_OVERVOLT故障位，恢复必须回到CV上限以下2.5s。
         */
        trip = timer_elapsed(ctx, TIMER_BAT_OV_SLOW,
                             (sample->battery_voltage_mv > 0) &&
                                 ((uint32_t)sample->battery_voltage_mv > profile->ov_slow_mv),
                             AURORA_BAT_OV_SLOW_DELAY_MS, now_ms) ||
               timer_elapsed(ctx, TIMER_BAT_OV_MEDIUM,
                             (sample->battery_voltage_mv > 0) &&
                                 ((uint32_t)sample->battery_voltage_mv > profile->ov_medium_mv),
                             AURORA_BAT_OV_MEDIUM_DELAY_MS, now_ms) ||
               timer_elapsed(ctx, TIMER_BAT_OV_FAST,
                             (sample->battery_voltage_mv > 0) &&
                                 ((uint32_t)sample->battery_voltage_mv > profile->ov_fast_mv),
                             AURORA_BAT_OV_FAST_DELAY_MS, now_ms) ||
               timer_elapsed(ctx, TIMER_BAT_OV_ABSOLUTE,
                             (sample->battery_voltage_mv > 0) &&
                                 ((uint32_t)sample->battery_voltage_mv > profile->ov_absolute_mv),
                             AURORA_BAT_OV_ABSOLUTE_DELAY_MS, now_ms);
        recover = timer_elapsed(ctx, TIMER_BAT_OV_RECOVER,
                                (sample->battery_voltage_mv > 0) &&
                                    ((uint32_t)sample->battery_voltage_mv < profile->cv_max_mv),
                                AURORA_BAT_OV_RECOVER_DELAY_MS, now_ms);
        if (trip)
        {
            set_fault(ctx, AURORA_FAULT_BAT_OVERVOLT, false, now_ms);
        }
        else if (recover)
        {
            clear_auto_fault(ctx, AURORA_FAULT_BAT_OVERVOLT);
        }
    }
    else
    {
        // Demo不连接电池，清除Battery专属自动故障并复位其计时条件。
        clear_auto_fault(ctx, AURORA_FAULT_BAT_UNDERVOLT | AURORA_FAULT_BAT_OVERVOLT);
        (void)timer_elapsed(ctx, TIMER_BAT_UV_TRIP, false, AURORA_BAT_UV_TRIP_DELAY_MS, now_ms);
        (void)timer_elapsed(ctx, TIMER_BAT_OV_SLOW, false, AURORA_BAT_OV_SLOW_DELAY_MS, now_ms);
    }

    /* PV软件分级过流：结构继承V2.7，300W基础12A仍是待台架冻结候选。 */
    trip = timer_elapsed(ctx, TIMER_PV_OCP_SLOW,
                         boost_output_active && sample->pv_current_ma >
                                                    current_threshold_ma(AURORA_PV_CURRENT_LIMIT_MA,
                                                                         AURORA_PV_OCP_SLOW_NUM),
                         AURORA_PV_OCP_SLOW_DELAY_MS, now_ms) ||
           timer_elapsed(ctx, TIMER_PV_OCP_MEDIUM,
                         boost_output_active && sample->pv_current_ma >
                                                    current_threshold_ma(AURORA_PV_CURRENT_LIMIT_MA,
                                                                         AURORA_PV_OCP_MID_NUM),
                         AURORA_PV_OCP_MID_DELAY_MS, now_ms) ||
           timer_elapsed(ctx, TIMER_PV_OCP_FAST,
                         boost_output_active && sample->pv_current_ma >
                                                    current_threshold_ma(AURORA_PV_CURRENT_LIMIT_MA,
                                                                         AURORA_PV_OCP_FAST_NUM),
                         AURORA_PV_OCP_FAST_DELAY_MS, now_ms);
    recover = timer_elapsed(ctx, TIMER_PV_OCP_RECOVER,
                            sample->pv_current_ma >= 0 &&
                                sample->pv_current_ma <= AURORA_PV_CURRENT_LIMIT_MA,
                            AURORA_PV_OCP_RECOVER_DELAY_MS, now_ms);
    if (trip)
    {
        set_fault(ctx, AURORA_FAULT_PV_OVERCURRENT, true, now_ms);
    }
    else if (recover)
    {
        clear_auto_fault(ctx, AURORA_FAULT_PV_OVERCURRENT);
    }

    /*
     * PV_I物理合理性：运行时明显反向电流持续10ms，或PWM关闭时仍出现>=3A持续1.5s，
     * 都提示零点/OPA/极性/采样链异常。恢复必须PWM关闭且|PV_I|<=0.5A持续30s。
     */
    trip = timer_elapsed(ctx, TIMER_PV_I_RUN_NEG_TRIP,
                         pv_current_calibrated && boost_output_active &&
                             sample->pv_current_ma <= AURORA_PV_I_RUN_NEGATIVE_TRIP_MA,
                         AURORA_PV_I_RUN_NEGATIVE_DELAY_MS, now_ms) ||
           timer_elapsed(ctx, TIMER_PV_I_OFF_ABS_TRIP,
                         pv_current_calibrated && !boost_output_active &&
                             abs_current_ma(sample->pv_current_ma) >= AURORA_PV_I_OFF_ABS_TRIP_MA,
                         AURORA_PV_I_OFF_ABS_DELAY_MS, now_ms);
    recover = timer_elapsed(ctx, TIMER_PV_I_PLAUS_RECOVER,
                            !boost_output_active && abs_current_ma(sample->pv_current_ma) <=
                                                        AURORA_PV_I_PLAUS_RECOVER_ABS_MA,
                            AURORA_PV_I_PLAUS_RECOVER_DELAY_MS, now_ms);
    if (trip)
    {
        set_fault(ctx, AURORA_FAULT_PV_CURRENT_PLAUSIBILITY, true, now_ms);
    }
    else if (recover)
    {
        clear_auto_fault(ctx, AURORA_FAULT_PV_CURRENT_PLAUSIBILITY);
    }

    /* 过功率：Rated×1.2持续5s，降回Rated并稳定30s后允许重新启动。 */
    {
        const uint32_t trip_power_mw =
            (uint32_t)(((uint64_t)AURORA_RATED_POWER_MW * AURORA_OVERPOWER_NUM) /
                       AURORA_OVERPOWER_DEN);
        trip = timer_elapsed(ctx, TIMER_PV_POWER_TRIP,
                             boost_output_active && sample->pv_power_mw > 0 &&
                                 (uint32_t)sample->pv_power_mw > trip_power_mw,
                             AURORA_OVERPOWER_DELAY_MS, now_ms);
        recover = timer_elapsed(ctx, TIMER_PV_POWER_RECOVER,
                                sample->pv_power_mw >= 0 &&
                                    (uint32_t)sample->pv_power_mw <= AURORA_RATED_POWER_MW,
                                AURORA_OVERPOWER_RECOVER_DELAY_MS, now_ms);
        if (trip)
        {
            set_fault(ctx, AURORA_FAULT_PV_OVERPOWER, true, now_ms);
        }
        else if (recover)
        {
            clear_auto_fault(ctx, AURORA_FAULT_PV_OVERPOWER);
        }
    }

    /* MOS NTC：物理状态由Measurement按原始ADC方向判定，正常时才参与温度保护。 */
    {
        const bool open = sample->mos_ntc_status == AURORA_NTC_STATUS_OPEN;
        const bool shorted = sample->mos_ntc_status == AURORA_NTC_STATUS_SHORT;

        if (timer_elapsed(ctx, TIMER_MOS_NTC_OPEN_TRIP, open, AURORA_NTC_FAULT_DELAY_MS, now_ms))
        {
            set_fault(ctx, AURORA_FAULT_MOS_NTC_OPEN, false, now_ms);
        }
        else if (timer_elapsed(ctx, TIMER_MOS_NTC_OPEN_RECOVER, !open, AURORA_NTC_RECOVER_DELAY_MS,
                               now_ms))
        {
            clear_auto_fault(ctx, AURORA_FAULT_MOS_NTC_OPEN);
        }

        if (timer_elapsed(ctx, TIMER_MOS_NTC_SHORT_TRIP, shorted, AURORA_NTC_FAULT_DELAY_MS,
                          now_ms))
        {
            set_fault(ctx, AURORA_FAULT_MOS_NTC_SHORT, false, now_ms);
        }
        else if (timer_elapsed(ctx, TIMER_MOS_NTC_SHORT_RECOVER, !shorted,
                               AURORA_NTC_RECOVER_DELAY_MS, now_ms))
        {
            clear_auto_fault(ctx, AURORA_FAULT_MOS_NTC_SHORT);
        }

        if (!open && !shorted && ((sample->valid_mask & AURORA_MEAS_VALID_MOS_TEMP) != 0U))
        {
            trip = timer_elapsed(ctx, TIMER_MOS_TEMP_TRIP,
                                 sample->mos_temp_dC > AURORA_MOS_TRIP_TEMP_DC,
                                 AURORA_MOS_TEMP_TRIP_DELAY_MS, now_ms);
            recover = timer_elapsed(ctx, TIMER_MOS_TEMP_RECOVER,
                                    sample->mos_temp_dC < AURORA_MOS_RECOVER_TEMP_DC,
                                    AURORA_MOS_TEMP_RECOVER_DELAY_MS, now_ms);
            if (trip)
            {
                set_fault(ctx, AURORA_FAULT_MOS_OVERTEMP, false, now_ms);
            }
            else if (recover)
            {
                clear_auto_fault(ctx, AURORA_FAULT_MOS_OVERTEMP);
            }
        }
    }

    /* 环境NTC：开路=ADC靠近VDD，短路=ADC靠近GND；正常状态才参与高低温保护和铅酸温补。 */
    {
        const bool open = sample->ambient_ntc_status == AURORA_NTC_STATUS_OPEN;
        const bool shorted = sample->ambient_ntc_status == AURORA_NTC_STATUS_SHORT;

        if (timer_elapsed(ctx, TIMER_AMB_NTC_OPEN_TRIP, open, AURORA_NTC_FAULT_DELAY_MS, now_ms))
        {
            set_fault(ctx, AURORA_FAULT_AMB_NTC_OPEN, false, now_ms);
        }
        else if (timer_elapsed(ctx, TIMER_AMB_NTC_OPEN_RECOVER, !open, AURORA_NTC_RECOVER_DELAY_MS,
                               now_ms))
        {
            clear_auto_fault(ctx, AURORA_FAULT_AMB_NTC_OPEN);
        }

        if (timer_elapsed(ctx, TIMER_AMB_NTC_SHORT_TRIP, shorted, AURORA_NTC_FAULT_DELAY_MS,
                          now_ms))
        {
            set_fault(ctx, AURORA_FAULT_AMB_NTC_SHORT, false, now_ms);
        }
        else if (timer_elapsed(ctx, TIMER_AMB_NTC_SHORT_RECOVER, !shorted,
                               AURORA_NTC_RECOVER_DELAY_MS, now_ms))
        {
            clear_auto_fault(ctx, AURORA_FAULT_AMB_NTC_SHORT);
        }

        if (!open && !shorted && ((sample->valid_mask & AURORA_MEAS_VALID_AMB_TEMP) != 0U))
        {
            trip = timer_elapsed(ctx, TIMER_AMB_HIGH_TRIP,
                                 sample->ambient_temp_dC > AURORA_AMB_HIGH_TRIP_TEMP_DC,
                                 AURORA_AMB_TEMP_TRIP_DELAY_MS, now_ms) ||
                   timer_elapsed(ctx, TIMER_AMB_LOW_TRIP,
                                 sample->ambient_temp_dC < (battery_mode
                                                                ? profile->ambient_low_trip_dC
                                                                : AURORA_AMB_LOW_TRIP_TEMP_DC),
                                 AURORA_AMB_TEMP_TRIP_DELAY_MS, now_ms);
            recover = timer_elapsed(ctx, TIMER_AMB_TEMP_RECOVER,
                                    sample->ambient_temp_dC >=
                                            (battery_mode ? profile->ambient_low_recover_dC
                                                          : AURORA_AMB_LOW_RECOVER_TEMP_DC) &&
                                        sample->ambient_temp_dC <= AURORA_AMB_HIGH_RECOVER_TEMP_DC,
                                    AURORA_AMB_TEMP_RECOVER_DELAY_MS, now_ms);
            if (trip)
            {
                set_fault(ctx, AURORA_FAULT_AMB_TEMP, false, now_ms);
            }
            else if (recover)
            {
                clear_auto_fault(ctx, AURORA_FAULT_AMB_TEMP);
            }
        }
    }
}

/*---------------------------------------------------------------------------*
 * Name        : bool aurora_protection_clear(aurora_protection_ctx_t *ctx,
 *               uint32_t clear_mask, bool hardware_sources_inactive)
 * Input       : ctx - 保护上下文；clear_mask - 请求清除位；hardware_sources_inactive - 硬件源已失效
 * Output      : true表示指定锁存已清除；false表示仍有active条件或硬件源
 * Description : 仅用于快速/硬锁存故障；普通V2.7保护由各自恢复时间自动解除。
 *---------------------------------------------------------------------------*/
bool aurora_protection_clear(aurora_protection_ctx_t *ctx, uint32_t clear_mask,
                             bool hardware_sources_inactive)
{
    if ((ctx == NULL) || !hardware_sources_inactive || ((ctx->active_mask & clear_mask) != 0U))
    {
        return false;
    }

    if ((ctx->latched_mask & clear_mask) != 0U)
    {
        ctx->latched_mask &= ~clear_mask;
        ctx->epoch++;
    }
    return true;
}

/*---------------------------------------------------------------------------*
 * Name        : bool aurora_protection_clear_verified_fast_fault(
 *               aurora_protection_ctx_t *ctx, uint32_t clear_mask,
 *               bool hardware_sources_inactive)
 * Input       : ctx - 保护上下文；clear_mask - 已验证可恢复的快速故障；
 *               hardware_sources_inactive - 比较器/Break实时源已确认失效
 * Output      : true表示指定快速故障已清除；false表示硬件源仍有效或参数错误
 * Description : 只供Service在硬件源连续消失并满足恢复时间后调用；同时清active/latched并递增epoch。
 *---------------------------------------------------------------------------*/
bool aurora_protection_clear_verified_fast_fault(aurora_protection_ctx_t *ctx, uint32_t clear_mask,
                                                 bool hardware_sources_inactive)
{
    if ((ctx == NULL) || !hardware_sources_inactive)
    {
        return false;
    }

    if (((ctx->active_mask | ctx->latched_mask) & clear_mask) != 0U)
    {
        ctx->active_mask &= (uint32_t)~clear_mask;
        ctx->latched_mask &= (uint32_t)~clear_mask;
        ctx->epoch++;
    }
    return true;
}

/*---------------------------------------------------------------------------*
 * Name        : bool aurora_protection_is_safe(const aurora_protection_ctx_t *ctx)
 * Input       : ctx - 保护上下文
 * Output      : true表示active与latched均为0
 * Description : 功率状态机和Service统一读取该结果；不代表板级人工门禁已经放行。
 *---------------------------------------------------------------------------*/
bool aurora_protection_is_safe(const aurora_protection_ctx_t *ctx)
{
    return (ctx != NULL) && (ctx->active_mask == 0U) && (ctx->latched_mask == 0U);
}

/*---------------------------------------------------------------------------*
 * Name        : uint32_t aurora_protection_epoch(const aurora_protection_ctx_t *ctx)
 * Input       : ctx - 保护上下文
 * Output      : 当前软件安全epoch；参数无效返回0
 * Description : Service在检查安全与执行发波之间复核epoch，阻断TOCTOU旧授权。
 *---------------------------------------------------------------------------*/
uint32_t aurora_protection_epoch(const aurora_protection_ctx_t *ctx)
{
    return (ctx != NULL) ? ctx->epoch : 0U;
}

/*---------------------------------------------------------------------------*
 * Name        : uint32_t aurora_protection_fault_mask(const aurora_protection_ctx_t *ctx)
 * Input       : ctx - 保护上下文
 * Output      : active与latched的并集
 * Description : UI和遥测应展示当前或尚未清除的故障，不能只看历史latched位。
 *---------------------------------------------------------------------------*/
uint32_t aurora_protection_fault_mask(const aurora_protection_ctx_t *ctx)
{
    return (ctx != NULL) ? (ctx->active_mask | ctx->latched_mask) : AURORA_FAULT_INTERNAL;
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_protection_step(aurora_protection_ctx_t *ctx,
 *               const aurora_measurement_t *sample,
 *               const aurora_charge_profile_t *profile,
 *               bool pv_current_calibrated, bool boost_output_active,
 *               uint32_t now_ms)
 * Input       : 与历史Battery模式接口一致
 * Output      : 无
 * Description : 保留旧调用方和Host测试兼容；内部固定选择Battery模式执行完整保护。
 *---------------------------------------------------------------------------*/
void aurora_protection_step(aurora_protection_ctx_t *ctx, const aurora_measurement_t *sample,
                            const aurora_charge_profile_t *profile, bool pv_current_calibrated,
                            bool boost_output_active, uint32_t now_ms)
{
    aurora_protection_step_ex(ctx, sample, profile, AURORA_MODE_BATTERY, pv_current_calibrated,
                              boost_output_active, now_ms);
}
