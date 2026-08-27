#include "power_stage.h"

#include "app_config.h"

#include <string.h>

static uint32_t elapsed_ms(uint32_t now, uint32_t then)
{
    return now - then;
}

static int32_t abs_i32(int32_t value)
{
    return (value < 0) ? -value : value;
}

static void enter_state(aurora_power_stage_ctx_t *ctx,
                        aurora_power_state_t state,
                        uint32_t now_ms)
{
    ctx->state = state;
    ctx->state_since_ms = now_ms;
    ctx->delta_ok_since_ms = 0U;
    if ((state == AURORA_POWER_OFF) || (state == AURORA_POWER_WAIT_BATTERY) ||
        (state == AURORA_POWER_FAULT))
    {
        ctx->duty_q15 = 0U;
        ctx->power_integral = 0LL;
    }
}

static uint16_t slew_duty(uint16_t previous, uint16_t target)
{
    if (target > previous)
    {
        uint32_t next = (uint32_t)previous + AURORA_DUTY_STEP_Q15;
        return (uint16_t)((next < target) ? next : target);
    }
    if (target < previous)
    {
        uint32_t difference = (uint32_t)previous - target;
        return (difference > AURORA_DUTY_STEP_Q15) ?
                   (uint16_t)(previous - AURORA_DUTY_STEP_Q15) : target;
    }
    return previous;
}

static uint16_t power_to_duty(aurora_power_stage_ctx_t *ctx,
                              const aurora_measurement_t *sample,
                              uint32_t power_command_mw,
                              uint16_t maximum_duty_q15)
{
    int64_t feedforward_q15;
    int32_t power_error_mw;
    int64_t correction_q15;
    int64_t target;

    if ((sample->pv_voltage_mv <= 0) || (power_command_mw == 0U))
    {
        return 0U;
    }

    /*
     * 预充起点可能出现 BST_U <= PV_U。此时理想Boost公式尚不成立，
     * 但绝不能返回0而让母线永远无法建立；从最小物理Duty受限爬升。
     */
    if (sample->bus_voltage_mv <= sample->pv_voltage_mv)
    {
        feedforward_q15 = AURORA_DUTY_MIN_Q15;
    }
    else
    {
        feedforward_q15 = ((int64_t)(sample->bus_voltage_mv - sample->pv_voltage_mv) *
                           AURORA_DUTY_Q15_ONE) /
                          sample->bus_voltage_mv;
    }
    power_error_mw = (int32_t)power_command_mw - sample->pv_power_mw;

    /* 快速执行器只修正功率误差；MPPT外层永远不接触CCR。 */
    ctx->power_integral += (int64_t)power_error_mw / 128LL;
    if (ctx->power_integral > 4096LL)
    {
        ctx->power_integral = 4096LL;
    }
    if (ctx->power_integral < -4096LL)
    {
        ctx->power_integral = -4096LL;
    }
    correction_q15 = ((int64_t)power_error_mw * 64LL) / 1000LL + ctx->power_integral;
    target = feedforward_q15 + correction_q15;

    if (target < AURORA_DUTY_MIN_Q15)
    {
        target = AURORA_DUTY_MIN_Q15;
    }
    if (target > maximum_duty_q15)
    {
        target = maximum_duty_q15;
    }
    return slew_duty(ctx->duty_q15, (uint16_t)target);
}

void aurora_power_stage_init(aurora_power_stage_ctx_t *ctx, uint32_t now_ms)
{
    if (ctx == NULL)
    {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = AURORA_POWER_WAIT_BATTERY;
    ctx->state_since_ms = now_ms;
}

aurora_power_command_t aurora_power_stage_step(aurora_power_stage_ctx_t *ctx,
                                               const aurora_measurement_t *sample,
                                               const aurora_mppt_output_t *mppt,
                                               const aurora_charge_output_t *charger,
                                               bool protection_safe,
                                               uint32_t now_ms)
{
    aurora_power_command_t command;
    uint32_t requested_power_mw = 0U;
    int32_t relay_delta_mv;
    const uint32_t needed = AURORA_MEAS_VALID_PV_V |
                            AURORA_MEAS_VALID_PV_POWER |
                            AURORA_MEAS_VALID_BAT_V |
                            AURORA_MEAS_VALID_BUS_V;

    memset(&command, 0, sizeof(command));
    if ((ctx == NULL) || (sample == NULL) || (mppt == NULL) || (charger == NULL))
    {
        command.state = AURORA_POWER_FAULT;
        return command;
    }

    if (!protection_safe && (ctx->state != AURORA_POWER_FAULT))
    {
        /* 先硬关PWM；继电器保留一个很短的放能窗口，避免带电感电流拉弧。 */
        enter_state(ctx, AURORA_POWER_FAULT, now_ms);
    }
    else if ((ctx->state == AURORA_POWER_FAULT) && protection_safe)
    {
        /* 故障清除后仍从断电状态重新走预充，绝不直接恢复发波。 */
        enter_state(ctx, AURORA_POWER_WAIT_BATTERY, now_ms);
    }

    if ((sample->valid_mask & needed) != needed)
    {
        ctx->duty_q15 = 0U;
        command.state = ctx->state;
        command.relay_enable = ctx->relay_closed;
        return command;
    }

    relay_delta_mv = abs_i32(sample->bus_voltage_mv - sample->battery_voltage_mv);

    switch (ctx->state)
    {
    case AURORA_POWER_WAIT_BATTERY:
        ctx->relay_closed = false;
        if ((sample->battery_voltage_mv > 10000) &&
            (sample->pv_voltage_mv >= AURORA_PV_START_MIN_MV))
        {
            enter_state(ctx, AURORA_POWER_PRECHARGE, now_ms);
        }
        break;

    case AURORA_POWER_PRECHARGE:
        ctx->relay_closed = false;
        if (elapsed_ms(now_ms, ctx->state_since_ms) >= AURORA_PRECHARGE_TIMEOUT_MS)
        {
            enter_state(ctx, AURORA_POWER_FAULT, now_ms);
        }
        else if (relay_delta_mv <= AURORA_RELAY_CLOSE_DELTA_MV)
        {
            if (ctx->delta_ok_since_ms == 0U)
            {
                ctx->delta_ok_since_ms = now_ms;
            }
            else if (elapsed_ms(now_ms, ctx->delta_ok_since_ms) >= AURORA_RELAY_DELTA_HOLD_MS)
            {
                ctx->relay_closed = true;
                ctx->duty_q15 = 0U;
                enter_state(ctx, AURORA_POWER_RELAY_SETTLE, now_ms);
                break;
            }
        }
        else
        {
            ctx->delta_ok_since_ms = 0U;
        }
        requested_power_mw = AURORA_PRECHARGE_POWER_MW;
        ctx->duty_q15 = power_to_duty(ctx, sample, requested_power_mw,
                                     (uint16_t)(AURORA_DUTY_MAX_Q15 / 2U));
        command.pwm_enable = true;
        break;

    case AURORA_POWER_RELAY_SETTLE:
        ctx->relay_closed = true;
        ctx->duty_q15 = 0U;
        if (elapsed_ms(now_ms, ctx->state_since_ms) >= AURORA_RELAY_SETTLE_MS)
        {
            if (relay_delta_mv <= AURORA_RELAY_VERIFY_DELTA_MV)
            {
                enter_state(ctx, AURORA_POWER_RUN, now_ms);
            }
            else
            {
                ctx->relay_closed = false;
                enter_state(ctx, AURORA_POWER_FAULT, now_ms);
            }
        }
        break;

    case AURORA_POWER_RUN:
        ctx->relay_closed = true;
        if ((sample->pv_power_mw < AURORA_NO_SUN_ENTER_MW) || !charger->allow_charge)
        {
            if (ctx->no_sun_since_ms == 0U)
            {
                ctx->no_sun_since_ms = now_ms;
            }
            if (elapsed_ms(now_ms, ctx->no_sun_since_ms) >= AURORA_NO_SUN_OPEN_RELAY_MS)
            {
                ctx->duty_q15 = 0U;
                ctx->relay_closed = false;
                enter_state(ctx, AURORA_POWER_NO_SUN, now_ms);
                break;
            }
        }
        else if (sample->pv_power_mw > AURORA_NO_SUN_RECOVER_MW)
        {
            ctx->no_sun_since_ms = 0U;
        }

        if (charger->allow_charge && mppt->valid)
        {
            requested_power_mw = mppt->theoretical_power_mw;
            if (requested_power_mw > charger->power_limit_mw)
            {
                requested_power_mw = charger->power_limit_mw;
            }
            if (requested_power_mw > AURORA_RATED_POWER_MW)
            {
                requested_power_mw = AURORA_RATED_POWER_MW;
            }
            ctx->duty_q15 = power_to_duty(ctx, sample, requested_power_mw,
                                         AURORA_DUTY_MAX_Q15);
            command.pwm_enable = requested_power_mw != 0U;
        }
        else
        {
            ctx->duty_q15 = slew_duty(ctx->duty_q15, 0U);
        }
        break;

    case AURORA_POWER_NO_SUN:
        ctx->duty_q15 = 0U;
        ctx->relay_closed = false;
        if (sample->pv_power_mw > AURORA_NO_SUN_RECOVER_MW)
        {
            ctx->no_sun_since_ms = 0U;
            enter_state(ctx, AURORA_POWER_WAIT_BATTERY, now_ms);
        }
        break;

    case AURORA_POWER_FAULT:
        ctx->duty_q15 = 0U;
        if (elapsed_ms(now_ms, ctx->state_since_ms) >= AURORA_RELAY_FAULT_RELEASE_MS)
        {
            ctx->relay_closed = false;
        }
        break;

    case AURORA_POWER_OFF:
    default:
        ctx->duty_q15 = 0U;
        ctx->relay_closed = false;
        break;
    }

    command.duty_q15 = command.pwm_enable ? ctx->duty_q15 : 0U;
    command.relay_enable = ctx->relay_closed;
    command.state = ctx->state;
    return command;
}
