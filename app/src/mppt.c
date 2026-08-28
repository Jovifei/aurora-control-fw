#include "mppt.h"

#include "app_config.h"

#include <string.h>

/*---------------------------------------------------------------------------*
 * Name        : static uint32_t clamp_u32_i64(int64_t value, uint32_t low, uint32_t high)
 * Input       : value - 有符号64位输入；low - 无符号下限；high - 无符号上限
 * Output      : 限制到[low, high]的32位无符号结果
 * Description : 统一处理MPPT参考电压、积分和功率请求的上下限，避免有符号/无符号混算。
 *---------------------------------------------------------------------------*/
static uint32_t clamp_u32_i64(int64_t value, uint32_t low, uint32_t high)
{
    if (value < (int64_t)low)
    {
        return low;
    }
    if (value > (int64_t)high)
    {
        return high;
    }
    return (uint32_t)value;
}

/*---------------------------------------------------------------------------*
 * Name        : static uint32_t elapsed_ms(uint32_t now_ms, uint32_t then_ms)
 * Input       : now_ms - 当前毫秒时间戳；then_ms - 起始毫秒时间戳
 * Output      : 两个时间戳之间的无符号间隔，单位ms
 * Description : 使用无符号减法计算时间间隔，兼容32位毫秒计数器自然回绕。
 *---------------------------------------------------------------------------*/
static uint32_t elapsed_ms(uint32_t now_ms, uint32_t then_ms)
{
    return now_ms - then_ms;
}

/*---------------------------------------------------------------------------*
 * Name        : static uint32_t mppt_min_ref_mv(void)
 * Input       : 无
 * Output      : MPPT允许的最小PV参考电压，单位mV
 * Description : 在PV启动门槛上增加控制裕量，避免搜索长期贴住欠压边界。
 *---------------------------------------------------------------------------*/
static uint32_t mppt_min_ref_mv(void)
{
    return (uint32_t)(AURORA_PV_START_MIN_MV + AURORA_MPPT_REF_MIN_MARGIN_MV);
}

/*---------------------------------------------------------------------------*
 * Name        : static uint32_t mppt_max_ref_mv(const aurora_mppt_ctx_t *ctx)
 * Input       : ctx - MPPT上下文
 * Output      : 当前允许的最大PV参考电压，单位mV
 * Description : 取“PV绝对上限减裕量”和“实测Voc减裕量”中更严格者，防止搜索进入开路区或越界。
 *---------------------------------------------------------------------------*/
static uint32_t mppt_max_ref_mv(const aurora_mppt_ctx_t *ctx)
{
    uint32_t maximum_mv =
        (uint32_t)(AURORA_PV_ABSOLUTE_MAX_MV - AURORA_MPPT_ABS_MAX_MARGIN_MV);

    if ((ctx->open_circuit_voltage_mv > AURORA_MPPT_VOC_MARGIN_MV) &&
        ((ctx->open_circuit_voltage_mv - AURORA_MPPT_VOC_MARGIN_MV) < maximum_mv))
    {
        maximum_mv = ctx->open_circuit_voltage_mv - AURORA_MPPT_VOC_MARGIN_MV;
    }
    return maximum_mv;
}

/*---------------------------------------------------------------------------*
 * Name        : static uint32_t adaptive_step_mv(int32_t delta_power_mw,
 *               int32_t delta_voltage_mv)
 * Input       : delta_power_mw - 相邻窗口功率变化，单位mW；
 *               delta_voltage_mv - 相邻窗口电压变化，单位mV
 * Output      : 本次PV参考电压步长，单位mV
 * Description : 根据|ΔP/ΔV|生成有上下限的自适应步长；远离MPP时加快，峰值附近退回最小扰动。
 *---------------------------------------------------------------------------*/
static uint32_t adaptive_step_mv(int32_t delta_power_mw,
                                 int32_t delta_voltage_mv)
{
    int64_t slope_abs;
    int64_t step_mv;

    if (delta_voltage_mv == 0)
    {
        return AURORA_MPPT_STEP_MIN_MV;
    }

    slope_abs = ((int64_t)delta_power_mw * AURORA_MPPT_SLOPE_SCALE) /
                delta_voltage_mv;
    if (slope_abs < 0LL)
    {
        slope_abs = -slope_abs;
    }

    step_mv = AURORA_MPPT_STEP_MIN_MV +
              (slope_abs / AURORA_MPPT_SLOPE_STEP_DIVISOR);
    if (step_mv > AURORA_MPPT_STEP_MAX_MV)
    {
        step_mv = AURORA_MPPT_STEP_MAX_MV;
    }
    return (uint32_t)step_mv;
}

/*---------------------------------------------------------------------------*
 * Name        : static void update_reference(aurora_mppt_ctx_t *ctx,
 *               const aurora_measurement_t *sample, bool external_limited)
 * Input       : ctx - MPPT上下文；sample - 最新PV测量；
 *               external_limited - 电池/温度/硬件包络正在限功率
 * Output      : 无
 * Description : 按80ms外层节拍更新Vpv_ref；限功率时冻结，启动时快速离开Voc，正常时按P-V斜率移动。
 *---------------------------------------------------------------------------*/
static void update_reference(aurora_mppt_ctx_t *ctx,
                             const aurora_measurement_t *sample,
                             bool external_limited)
{
    int32_t delta_voltage_mv;
    int32_t delta_power_mw;
    uint32_t step_mv;
    int64_t next_ref_mv;

    if (external_limited)
    {
        /* 外部包络改变了工作点，此时继续搜索会把限幅误认为PV曲线斜率。 */
        ctx->state = AURORA_MPPT_LIMITED;
        ctx->previous_voltage_mv = sample->pv_voltage_mv;
        ctx->previous_power_mw = sample->pv_power_mw;
        ctx->previous_valid = true;
        return;
    }

    if (ctx->state == AURORA_MPPT_FAST_DESCENT)
    {
        /* 从Voc附近每次向下移动最大步长，尽快建立有效输入电流。 */
        ctx->previous_voltage_mv = sample->pv_voltage_mv;
        ctx->previous_power_mw = sample->pv_power_mw;
        ctx->previous_valid = true;
        next_ref_mv = (int64_t)ctx->target_voltage_mv - AURORA_MPPT_STEP_MAX_MV;

        if ((sample->pv_current_ma > AURORA_MPPT_FAST_DESCENT_CURRENT_MA) &&
            (sample->pv_power_mw > AURORA_MPPT_P_NOISE_MW))
        {
            ctx->state = AURORA_MPPT_TRACKING;
        }
        ctx->target_voltage_mv = clamp_u32_i64(next_ref_mv,
                                              mppt_min_ref_mv(),
                                              mppt_max_ref_mv(ctx));
        return;
    }

    if (!ctx->previous_valid)
    {
        /* 第一组有效窗口只建立比较基线，不立即产生扰动。 */
        ctx->previous_voltage_mv = sample->pv_voltage_mv;
        ctx->previous_power_mw = sample->pv_power_mw;
        ctx->previous_valid = true;
        return;
    }

    delta_voltage_mv = sample->pv_voltage_mv - ctx->previous_voltage_mv;
    delta_power_mw = sample->pv_power_mw - ctx->previous_power_mw;
    ctx->previous_voltage_mv = sample->pv_voltage_mv;
    ctx->previous_power_mw = sample->pv_power_mw;

    /* 电压或功率变化落在噪声带内时保持参考值，避免MPP附近无意义抖动。 */
    if ((delta_voltage_mv > -AURORA_MPPT_V_NOISE_MV) &&
        (delta_voltage_mv < AURORA_MPPT_V_NOISE_MV))
    {
        return;
    }
    if ((delta_power_mw > -AURORA_MPPT_P_NOISE_MW) &&
        (delta_power_mw < AURORA_MPPT_P_NOISE_MW))
    {
        return;
    }

    step_mv = adaptive_step_mv(delta_power_mw, delta_voltage_mv);

    /*
     * ΔP与ΔV同号：dP/dV > 0，工作点在MPP左侧，应提高Vpv_ref；
     * ΔP与ΔV异号：dP/dV < 0，工作点在MPP右侧，应降低Vpv_ref。
     */
    if (((delta_power_mw > 0) && (delta_voltage_mv > 0)) ||
        ((delta_power_mw < 0) && (delta_voltage_mv < 0)))
    {
        next_ref_mv = (int64_t)ctx->target_voltage_mv + step_mv;
    }
    else
    {
        next_ref_mv = (int64_t)ctx->target_voltage_mv - step_mv;
    }

    ctx->state = AURORA_MPPT_TRACKING;
    ctx->target_voltage_mv = clamp_u32_i64(next_ref_mv,
                                          mppt_min_ref_mv(),
                                          mppt_max_ref_mv(ctx));
}

/*---------------------------------------------------------------------------*
 * Name        : static uint32_t voltage_pi(aurora_mppt_ctx_t *ctx,
 *               int32_t actual_voltage_mv, uint32_t power_allow_mw)
 * Input       : ctx - MPPT上下文；actual_voltage_mv - 实际PV电压，单位mV；
 *               power_allow_mw - 外部允许功率，单位mW
 * Output      : 理论功率请求，单位mW
 * Description : 用Vpv-Vpv_ref误差计算功率请求，并通过条件积分和上下限避免积分饱和。
 *---------------------------------------------------------------------------*/
static uint32_t voltage_pi(aurora_mppt_ctx_t *ctx,
                           int32_t actual_voltage_mv,
                           uint32_t power_allow_mw)
{
    const int32_t error_mv = actual_voltage_mv - (int32_t)ctx->target_voltage_mv;
    const int64_t proportional_mw =
        (int64_t)error_mv * AURORA_MPPT_VOLTAGE_KP_MW_PER_MV;
    const int64_t candidate_integral_mw =
        ctx->integral_mw +
        ((int64_t)error_mv * AURORA_MPPT_VOLTAGE_KI_MW_PER_MV_STEP);
    int64_t output_mw = proportional_mw + candidate_integral_mw;

    /* Vpv高于参考表示Boost拉得太轻，PI应提高功率请求；反之降低。 */
    if (!(((output_mw >= (int64_t)power_allow_mw) && (error_mv > 0)) ||
          ((output_mw <= 0LL) && (error_mv < 0))))
    {
        ctx->integral_mw = candidate_integral_mw;
    }

    if (ctx->integral_mw < 0LL)
    {
        ctx->integral_mw = 0LL;
    }
    if (ctx->integral_mw > (int64_t)power_allow_mw)
    {
        ctx->integral_mw = power_allow_mw;
    }

    output_mw = proportional_mw + ctx->integral_mw;
    return clamp_u32_i64(output_mw, 0U, power_allow_mw);
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_mppt_init(aurora_mppt_ctx_t *ctx)
 * Input       : ctx - MPPT上下文
 * Output      : 无
 * Description : 清零动态状态、关闭MPPT输出，并把PV参考电压初始化到安全下限。
 *---------------------------------------------------------------------------*/
void aurora_mppt_init(aurora_mppt_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->state = AURORA_MPPT_DISABLED;
    ctx->target_voltage_mv = mppt_min_ref_mv();
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_mppt_reset(aurora_mppt_ctx_t *ctx)
 * Input       : ctx - MPPT上下文
 * Output      : 无
 * Description : 清除搜索和PI动态状态，但保留最近一次Voc，供重新启动时建立安全参考范围。
 *---------------------------------------------------------------------------*/
void aurora_mppt_reset(aurora_mppt_ctx_t *ctx)
{
    const uint32_t voc_mv = (ctx != NULL) ? ctx->open_circuit_voltage_mv : 0U;

    aurora_mppt_init(ctx);
    if (ctx != NULL)
    {
        ctx->open_circuit_voltage_mv = voc_mv;
    }
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_mppt_set_open_circuit_voltage(aurora_mppt_ctx_t *ctx,
 *               uint32_t voc_mv)
 * Input       : ctx - MPPT上下文；voc_mv - 运行时测得的PV开路电压，单位mV
 * Output      : 无
 * Description : 记录Voc，从Voc减裕量建立初始参考，并进入快速下降阶段；不直接接触PWM或CCR。
 *---------------------------------------------------------------------------*/
void aurora_mppt_set_open_circuit_voltage(aurora_mppt_ctx_t *ctx,
                                          uint32_t voc_mv)
{
    if (ctx == NULL)
    {
        return;
    }

    ctx->open_circuit_voltage_mv = voc_mv;
    ctx->target_voltage_mv =
        clamp_u32_i64((int64_t)voc_mv - AURORA_MPPT_VOC_MARGIN_MV,
                      mppt_min_ref_mv(),
                      mppt_max_ref_mv(ctx));
    ctx->state = AURORA_MPPT_FAST_DESCENT;
    ctx->previous_valid = false;
    ctx->integral_mw = 0LL;
}

/*---------------------------------------------------------------------------*
 * Name        : aurora_mppt_output_t aurora_mppt_step(aurora_mppt_ctx_t *ctx,
 *               const aurora_measurement_t *sample, uint32_t power_allow_mw,
 *               bool external_limited, uint32_t now_ms)
 * Input       : ctx - MPPT上下文；sample - 最新PV测量；power_allow_mw - 外部功率上限；
 *               external_limited - 外部限功率标志；now_ms - 当前毫秒时间戳
 * Output      : PV目标电压、理论功率请求和有效标志
 * Description : 按80ms更新P-V搜索、按10ms更新电压PI；功率许可为0时清动态状态，避免复充带旧积分重发波。
 *---------------------------------------------------------------------------*/
aurora_mppt_output_t aurora_mppt_step(aurora_mppt_ctx_t *ctx,
                                      const aurora_measurement_t *sample,
                                      uint32_t power_allow_mw,
                                      bool external_limited,
                                      uint32_t now_ms)
{
    aurora_mppt_output_t output = {0};
    const uint32_t required_measurements = AURORA_MEAS_VALID_PV_V |
                                           AURORA_MEAS_VALID_PV_I |
                                           AURORA_MEAS_VALID_PV_POWER;

    if ((ctx == NULL) || (sample == NULL))
    {
        return output;
    }

    /* 无能量传输许可时等价于旧120W init阶段的MPPT_Reset：保留Voc，清搜索和PI动态量。 */
    if (power_allow_mw == 0U)
    {
        aurora_mppt_reset(ctx);
        return output;
    }

    if ((sample->valid_mask & required_measurements) != required_measurements)
    {
        return output;
    }

    /* 首次启用时优先使用已测Voc；没有Voc时只以当前PV电压作为候选初值。 */
    if (ctx->state == AURORA_MPPT_DISABLED)
    {
        const uint32_t initial_voc_mv =
            (ctx->open_circuit_voltage_mv != 0U) ?
                ctx->open_circuit_voltage_mv : (uint32_t)sample->pv_voltage_mv;

        aurora_mppt_set_open_circuit_voltage(ctx, initial_voc_mv);
        ctx->last_search_ms = now_ms;
        ctx->last_pi_ms = now_ms;
    }

    if (elapsed_ms(now_ms, ctx->last_search_ms) >= AURORA_MPPT_UPDATE_MS)
    {
        update_reference(ctx, sample, external_limited);
        ctx->last_search_ms = now_ms;
    }

    if (elapsed_ms(now_ms, ctx->last_pi_ms) >= AURORA_MPPT_PI_UPDATE_MS)
    {
        output.theoretical_power_mw = voltage_pi(ctx,
                                                sample->pv_voltage_mv,
                                                power_allow_mw);
        ctx->last_pi_ms = now_ms;
    }
    else
    {
        /* 两次PI更新之间保持已积累的功率基值，避免高频重复积分。 */
        output.theoretical_power_mw =
            clamp_u32_i64(ctx->integral_mw, 0U, power_allow_mw);
    }

    output.target_voltage_mv = ctx->target_voltage_mv;
    output.valid = true;
    return output;
}
