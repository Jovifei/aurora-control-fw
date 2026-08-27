#include "mppt.h"

#include "app_config.h"

#include <limits.h>
#include <string.h>

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

static uint32_t elapsed_ms(uint32_t now, uint32_t then)
{
    return now - then;
}

static uint32_t mppt_min_ref_mv(void)
{
    return (uint32_t)(AURORA_PV_START_MIN_MV + 500L);
}

static uint32_t mppt_max_ref_mv(const aurora_mppt_ctx_t *ctx)
{
    uint32_t absolute_max = (uint32_t)(AURORA_PV_ABSOLUTE_MAX_MV - 500L);

    if ((ctx->open_circuit_voltage_mv > AURORA_MPPT_VOC_MARGIN_MV) &&
        ((ctx->open_circuit_voltage_mv - AURORA_MPPT_VOC_MARGIN_MV) < absolute_max))
    {
        return ctx->open_circuit_voltage_mv - AURORA_MPPT_VOC_MARGIN_MV;
    }
    return absolute_max;
}

static uint32_t adaptive_step_mv(int32_t delta_power_mw, int32_t delta_voltage_mv)
{
    int64_t slope_abs;
    int64_t step;

    if (delta_voltage_mv == 0)
    {
        return AURORA_MPPT_STEP_MIN_MV;
    }

    slope_abs = ((int64_t)delta_power_mw * 1000LL) / delta_voltage_mv;
    if (slope_abs < 0LL)
    {
        slope_abs = -slope_abs;
    }

    /* 斜率越大，参考电压步长越大；峰值附近自动退到最小步长。 */
    step = AURORA_MPPT_STEP_MIN_MV + (slope_abs / 20LL);
    if (step > AURORA_MPPT_STEP_MAX_MV)
    {
        step = AURORA_MPPT_STEP_MAX_MV;
    }
    return (uint32_t)step;
}

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

void aurora_mppt_reset(aurora_mppt_ctx_t *ctx)
{
    const uint32_t voc_mv = (ctx != NULL) ? ctx->open_circuit_voltage_mv : 0U;

    aurora_mppt_init(ctx);
    if (ctx != NULL)
    {
        ctx->open_circuit_voltage_mv = voc_mv;
    }
}

void aurora_mppt_set_open_circuit_voltage(aurora_mppt_ctx_t *ctx, uint32_t voc_mv)
{
    if (ctx == NULL)
    {
        return;
    }

    ctx->open_circuit_voltage_mv = voc_mv;
    ctx->target_voltage_mv = clamp_u32_i64((int64_t)voc_mv - AURORA_MPPT_VOC_MARGIN_MV,
                                          mppt_min_ref_mv(),
                                          mppt_max_ref_mv(ctx));
    ctx->state = AURORA_MPPT_FAST_DESCENT;
    ctx->previous_valid = false;
    ctx->integral_mw = 0LL;
}

static void update_reference(aurora_mppt_ctx_t *ctx,
                             const aurora_measurement_t *sample,
                             bool external_limited)
{
    int32_t delta_voltage_mv;
    int32_t delta_power_mw;
    uint32_t step_mv;
    int64_t next_ref;

    if (external_limited)
    {
        /* 电池、温度或硬件包络正在限功率时，冻结搜索，避免把限幅误判成PV曲线变化。 */
        ctx->state = AURORA_MPPT_LIMITED;
        ctx->previous_voltage_mv = sample->pv_voltage_mv;
        ctx->previous_power_mw = sample->pv_power_mw;
        ctx->previous_valid = true;
        return;
    }

    if (ctx->state == AURORA_MPPT_FAST_DESCENT)
    {
        ctx->previous_voltage_mv = sample->pv_voltage_mv;
        ctx->previous_power_mw = sample->pv_power_mw;
        ctx->previous_valid = true;
        next_ref = (int64_t)ctx->target_voltage_mv - AURORA_MPPT_STEP_MAX_MV;
        if ((sample->pv_current_ma > 200) && (sample->pv_power_mw > AURORA_MPPT_P_NOISE_MW))
        {
            ctx->state = AURORA_MPPT_TRACKING;
        }
        ctx->target_voltage_mv = clamp_u32_i64(next_ref,
                                              mppt_min_ref_mv(),
                                              mppt_max_ref_mv(ctx));
        return;
    }

    if (!ctx->previous_valid)
    {
        ctx->previous_voltage_mv = sample->pv_voltage_mv;
        ctx->previous_power_mw = sample->pv_power_mw;
        ctx->previous_valid = true;
        return;
    }

    delta_voltage_mv = sample->pv_voltage_mv - ctx->previous_voltage_mv;
    delta_power_mw = sample->pv_power_mw - ctx->previous_power_mw;
    ctx->previous_voltage_mv = sample->pv_voltage_mv;
    ctx->previous_power_mw = sample->pv_power_mw;

    if ((delta_voltage_mv > -AURORA_MPPT_V_NOISE_MV) &&
             (delta_voltage_mv < AURORA_MPPT_V_NOISE_MV))
    {
        return;
    }
    else if ((delta_power_mw > -AURORA_MPPT_P_NOISE_MW) &&
             (delta_power_mw < AURORA_MPPT_P_NOISE_MW))
    {
        return;
    }
    else
    {
        step_mv = adaptive_step_mv(delta_power_mw, delta_voltage_mv);

        /*
         * dP/dV > 0：在峰值左侧，应提高PV参考电压；
         * dP/dV < 0：在峰值右侧，应降低PV参考电压。
         */
        if (((delta_power_mw > 0) && (delta_voltage_mv > 0)) ||
            ((delta_power_mw < 0) && (delta_voltage_mv < 0)))
        {
            next_ref = (int64_t)ctx->target_voltage_mv + step_mv;
        }
        else
        {
            next_ref = (int64_t)ctx->target_voltage_mv - step_mv;
        }
        ctx->state = AURORA_MPPT_TRACKING;
    }

    ctx->target_voltage_mv = clamp_u32_i64(next_ref,
                                          mppt_min_ref_mv(),
                                          mppt_max_ref_mv(ctx));
}

static uint32_t voltage_pi(aurora_mppt_ctx_t *ctx,
                           int32_t actual_voltage_mv,
                           uint32_t power_allow_mw)
{
    int32_t error_mv = actual_voltage_mv - (int32_t)ctx->target_voltage_mv;
    int64_t proportional_mw;
    int64_t candidate_integral;
    int64_t output_mw;

    /*
     * Vpv高于参考值表示Boost拉得太轻，功率请求应增加。
     * Kp约20mW/mV；Ki每10ms约1mW/mV，均使用整数实现。
     */
    proportional_mw = (int64_t)error_mv * 20LL;
    candidate_integral = ctx->integral_mw + (int64_t)error_mv;
    output_mw = proportional_mw + candidate_integral;

    /* 条件积分：输出饱和且误差还在推动饱和时，不继续积累。 */
    if (!(((output_mw >= (int64_t)power_allow_mw) && (error_mv > 0)) ||
          ((output_mw <= 0LL) && (error_mv < 0))))
    {
        ctx->integral_mw = candidate_integral;
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

aurora_mppt_output_t aurora_mppt_step(aurora_mppt_ctx_t *ctx,
                                      const aurora_measurement_t *sample,
                                      uint32_t power_allow_mw,
                                      bool external_limited,
                                      uint32_t now_ms)
{
    aurora_mppt_output_t output = {0U, 0U, false};
    const uint32_t required = AURORA_MEAS_VALID_PV_V |
                              AURORA_MEAS_VALID_PV_I |
                              AURORA_MEAS_VALID_PV_POWER;

    if ((ctx == NULL) || (sample == NULL) || ((sample->valid_mask & required) != required) ||
        (power_allow_mw == 0U))
    {
        return output;
    }

    if (ctx->state == AURORA_MPPT_DISABLED)
    {
        uint32_t initial_voc = (ctx->open_circuit_voltage_mv != 0U) ?
                                   ctx->open_circuit_voltage_mv :
                                   (uint32_t)sample->pv_voltage_mv;
        aurora_mppt_set_open_circuit_voltage(ctx, initial_voc);
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
        output.theoretical_power_mw = clamp_u32_i64(ctx->integral_mw, 0U, power_allow_mw);
    }

    output.target_voltage_mv = ctx->target_voltage_mv;
    output.valid = true;
    return output;
}
