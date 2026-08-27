#include "charger.h"

#include "app_config.h"

#include <string.h>

/*
 * 旧产品的12组电池参数被整理成物理单位表；不再让大量宏散落在状态机里。
 * 浮充仅对铅酸有效；锂电与钠电在CV尾流判满后直接结束。
 */
static const aurora_charge_profile_t k_profiles[AURORA_CHEM_COUNT][AURORA_PACK_COUNT] =
{
    {
        {AURORA_CHEM_LEAD, AURORA_PACK_48V, 41500U, 48000U, 58200U, 58800U, 54800U, 51200U, 1000U, 3000U, 300U, 150U},
        {AURORA_CHEM_LEAD, AURORA_PACK_60V, 52000U, 60000U, 72700U, 73400U, 68500U, 64000U, 1000U, 3000U, 300U, 150U},
        {AURORA_CHEM_LEAD, AURORA_PACK_72V, 62500U, 72000U, 87200U, 88000U, 82200U, 76800U, 1000U, 3000U, 300U, 150U}
    },
    {
        {AURORA_CHEM_TERNARY, AURORA_PACK_48V, 35100U, 39000U, 54600U, 56880U, 0U, 52650U, 1000U, 3000U, 300U, 0U},
        {AURORA_CHEM_TERNARY, AURORA_PACK_60V, 45900U, 51000U, 71400U, 74380U, 0U, 68850U, 1000U, 3000U, 300U, 0U},
        {AURORA_CHEM_TERNARY, AURORA_PACK_72V, 54000U, 60000U, 84000U, 87500U, 0U, 81000U, 1000U, 3000U, 300U, 0U}
    },
    {
        {AURORA_CHEM_LFP, AURORA_PACK_48V, 40000U, 48000U, 57600U, 60000U, 0U, 53600U, 1000U, 3000U, 300U, 0U},
        {AURORA_CHEM_LFP, AURORA_PACK_60V, 50000U, 60000U, 72000U, 75000U, 0U, 67000U, 1000U, 3000U, 300U, 0U},
        {AURORA_CHEM_LFP, AURORA_PACK_72V, 60000U, 72000U, 86400U, 90000U, 0U, 80400U, 1000U, 3000U, 300U, 0U}
    },
    {
        {AURORA_CHEM_SODIUM, AURORA_PACK_48V, 30600U, 34000U, 56100U, 58650U, 0U, 52700U, 1000U, 3000U, 300U, 0U},
        {AURORA_CHEM_SODIUM, AURORA_PACK_60V, 39600U, 44000U, 72600U, 75900U, 0U, 68200U, 1000U, 3000U, 300U, 0U},
        {AURORA_CHEM_SODIUM, AURORA_PACK_72V, 46800U, 52000U, 85800U, 89700U, 0U, 80600U, 1000U, 3000U, 300U, 0U}
    }
};

static uint32_t elapsed_ms(uint32_t now, uint32_t then)
{
    return now - then;
}

static uint32_t current_to_power_limit(uint32_t voltage_mv, uint32_t current_ma)
{
    uint64_t power = ((uint64_t)voltage_mv * current_ma) / 1000ULL;
    if (power > AURORA_RATED_POWER_MW)
    {
        power = AURORA_RATED_POWER_MW;
    }
    return (uint32_t)power;
}

bool aurora_charge_profile_get(aurora_battery_chem_t chemistry,
                               aurora_battery_pack_t pack,
                               aurora_charge_profile_t *out)
{
    if ((out == NULL) || (chemistry >= AURORA_CHEM_COUNT) || (pack >= AURORA_PACK_COUNT))
    {
        return false;
    }
    *out = k_profiles[chemistry][pack];
    return true;
}

void aurora_charger_init(aurora_charger_ctx_t *ctx,
                         aurora_battery_chem_t chemistry,
                         aurora_battery_pack_t pack,
                         uint32_t now_ms)
{
    if (ctx == NULL)
    {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->initialized = aurora_charge_profile_get(chemistry, pack, &ctx->profile);
    ctx->state = ctx->initialized ? AURORA_CHARGE_OFF : AURORA_CHARGE_FAULT;
    ctx->state_since_ms = now_ms;
    ctx->charge_start_ms = now_ms;
}

static void enter_state(aurora_charger_ctx_t *ctx,
                        aurora_charge_state_t state,
                        uint32_t now_ms)
{
    ctx->state = state;
    ctx->state_since_ms = now_ms;
    ctx->tail_since_ms = 0U;
    if (state == AURORA_CHARGE_FLOAT)
    {
        ctx->float_start_ms = now_ms;
    }
}

static uint32_t cv_power_limit(aurora_charger_ctx_t *ctx,
                               const aurora_measurement_t *sample)
{
    int32_t error_mv = (int32_t)ctx->profile.cv_target_mv - sample->battery_voltage_mv;
    int64_t proportional_mw = (int64_t)error_mv * 50LL;
    int64_t candidate = ctx->cv_integral_mw + ((int64_t)error_mv * 2LL);
    int64_t output;

    if (candidate < 0LL)
    {
        candidate = 0LL;
    }
    if (candidate > AURORA_RATED_POWER_MW)
    {
        candidate = AURORA_RATED_POWER_MW;
    }
    output = proportional_mw + candidate;
    if (!(((output >= AURORA_RATED_POWER_MW) && (error_mv > 0)) ||
          ((output <= 0LL) && (error_mv < 0))))
    {
        ctx->cv_integral_mw = candidate;
    }
    if (output < 0LL)
    {
        output = 0LL;
    }
    if (output > AURORA_RATED_POWER_MW)
    {
        output = AURORA_RATED_POWER_MW;
    }
    return (uint32_t)output;
}

aurora_charge_output_t aurora_charger_step(aurora_charger_ctx_t *ctx,
                                           const aurora_measurement_t *sample,
                                           bool weak_light,
                                           bool thermal_limited,
                                           uint32_t now_ms)
{
    aurora_charge_output_t output;
    const uint32_t required = AURORA_MEAS_VALID_BAT_V;

    memset(&output, 0, sizeof(output));
    if ((ctx == NULL) || (sample == NULL) || !ctx->initialized ||
        ((sample->valid_mask & required) != required))
    {
        output.state = AURORA_CHARGE_FAULT;
        return output;
    }

    if ((uint32_t)sample->battery_voltage_mv > ctx->profile.cv_protect_mv)
    {
        enter_state(ctx, AURORA_CHARGE_FAULT, now_ms);
    }

    if ((ctx->state != AURORA_CHARGE_OFF) &&
        (ctx->state != AURORA_CHARGE_COMPLETE) &&
        (ctx->state != AURORA_CHARGE_FAULT) &&
        (elapsed_ms(now_ms, ctx->charge_start_ms) >= AURORA_MAX_CHARGE_TIME_MS))
    {
        enter_state(ctx, AURORA_CHARGE_COMPLETE, now_ms);
    }

    switch (ctx->state)
    {
    case AURORA_CHARGE_OFF:
        ctx->charge_start_ms = now_ms;
        ctx->cv_integral_mw = 0LL;
        if ((uint32_t)sample->battery_voltage_mv < ctx->profile.trickle_exit_mv)
        {
            enter_state(ctx, AURORA_CHARGE_TRICKLE, now_ms);
        }
        else
        {
            enter_state(ctx, AURORA_CHARGE_CC, now_ms);
        }
        break;

    case AURORA_CHARGE_TRICKLE:
        if ((uint32_t)sample->battery_voltage_mv >= ctx->profile.trickle_exit_mv)
        {
            enter_state(ctx, AURORA_CHARGE_CC, now_ms);
        }
        break;

    case AURORA_CHARGE_CC:
        if ((uint32_t)sample->battery_voltage_mv >= ctx->profile.cv_target_mv)
        {
            ctx->cv_integral_mw = current_to_power_limit((uint32_t)sample->battery_voltage_mv,
                                                        ctx->profile.cc_current_ma);
            enter_state(ctx, AURORA_CHARGE_CV, now_ms);
        }
        break;

    case AURORA_CHARGE_CV:
        if ((uint32_t)sample->battery_voltage_mv + 500U < ctx->profile.cv_target_mv)
        {
            enter_state(ctx, AURORA_CHARGE_CC, now_ms);
        }
        else if (((sample->valid_mask & AURORA_MEAS_VALID_BAT_I_EST) != 0U) &&
                 !weak_light && !thermal_limited &&
                 (sample->battery_current_est_ma >= 0) &&
                 ((uint32_t)sample->battery_current_est_ma <= ctx->profile.tail_current_ma))
        {
            if (ctx->tail_since_ms == 0U)
            {
                ctx->tail_since_ms = now_ms;
            }
            else if (elapsed_ms(now_ms, ctx->tail_since_ms) >= AURORA_TAIL_HOLD_MS)
            {
                enter_state(ctx,
                            (ctx->profile.chemistry == AURORA_CHEM_LEAD) ?
                                AURORA_CHARGE_FLOAT : AURORA_CHARGE_COMPLETE,
                            now_ms);
            }
        }
        else
        {
            /* 弱光导致估算电流下降时暂停判满，防止把云影误判为电池满电。 */
            ctx->tail_since_ms = 0U;
        }
        break;

    case AURORA_CHARGE_FLOAT:
        if (elapsed_ms(now_ms, ctx->float_start_ms) >= AURORA_FLOAT_TIME_MS)
        {
            enter_state(ctx, AURORA_CHARGE_COMPLETE, now_ms);
        }
        break;

    case AURORA_CHARGE_COMPLETE:
        if ((uint32_t)sample->battery_voltage_mv <= ctx->profile.recharge_mv)
        {
            ctx->charge_start_ms = now_ms;
            enter_state(ctx, AURORA_CHARGE_CC, now_ms);
        }
        break;

    case AURORA_CHARGE_FAULT:
    default:
        break;
    }

    output.state = ctx->state;
    output.weak_light = weak_light;
    output.power_limited = thermal_limited;
    output.voltage_target_mv = ctx->profile.cv_target_mv;

    switch (ctx->state)
    {
    case AURORA_CHARGE_TRICKLE:
        output.allow_charge = true;
        output.power_limit_mw = current_to_power_limit((uint32_t)sample->battery_voltage_mv,
                                                      ctx->profile.trickle_current_ma);
        break;
    case AURORA_CHARGE_CC:
        output.allow_charge = true;
        output.power_limit_mw = current_to_power_limit((uint32_t)sample->battery_voltage_mv,
                                                      ctx->profile.cc_current_ma);
        break;
    case AURORA_CHARGE_CV:
        output.allow_charge = true;
        output.power_limit_mw = cv_power_limit(ctx, sample);
        break;
    case AURORA_CHARGE_FLOAT:
        output.allow_charge = true;
        output.voltage_target_mv = ctx->profile.float_target_mv;
        output.power_limit_mw = current_to_power_limit((uint32_t)sample->battery_voltage_mv,
                                                      ctx->profile.float_current_ma);
        break;
    default:
        output.allow_charge = false;
        output.power_limit_mw = 0U;
        break;
    }

    if (output.power_limit_mw > AURORA_RATED_POWER_MW)
    {
        output.power_limit_mw = AURORA_RATED_POWER_MW;
    }
    return output;
}
