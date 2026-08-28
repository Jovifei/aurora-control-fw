#include "power_stage.h"

#include "app_config.h"

#include <limits.h>
#include <string.h>

/*---------------------------------------------------------------------------*
 * Name        : static uint32_t elapsed_ms(uint32_t now_ms, uint32_t then_ms)
 * Input       : now_ms - 当前毫秒；then_ms - 起始毫秒
 * Output      : 无符号时间间隔，ms
 * Description : 使用无符号减法兼容32位毫秒计数器自然回绕。
 *---------------------------------------------------------------------------*/
static uint32_t elapsed_ms(uint32_t now_ms, uint32_t then_ms)
{
    return now_ms - then_ms;
}

/*---------------------------------------------------------------------------*
 * Name        : static int32_t abs_i32(int32_t value)
 * Input       : value - 32位有符号输入
 * Output      : 饱和绝对值
 * Description : 计算BST_U/BAT_U压差时避免INT32_MIN取负溢出。
 *---------------------------------------------------------------------------*/
static int32_t abs_i32(int32_t value)
{
    if (value == INT32_MIN)
    {
        return INT32_MAX;
    }
    return (value < 0) ? -value : value;
}

/*---------------------------------------------------------------------------*
 * Name        : static void enter_state(aurora_power_stage_ctx_t *ctx,
 *               aurora_power_state_t state, uint32_t now_ms)
 * Input       : ctx - 功率级上下文；state - 目标状态；now_ms - 当前毫秒
 * Output      : 无
 * Description : 统一切换状态并清理本状态的局部计时；非发波状态同步清零Duty和功率积分。
 *---------------------------------------------------------------------------*/
static void enter_state(aurora_power_stage_ctx_t *ctx,
                        aurora_power_state_t state,
                        uint32_t now_ms)
{
    ctx->state = state;
    ctx->state_since_ms = now_ms;
    ctx->delta_ok_since_ms = 0U;

    if ((state != AURORA_POWER_PRECHARGE) &&
        (state != AURORA_POWER_RUN))
    {
        ctx->duty_q15 = 0U;
        ctx->power_integral = 0LL;
    }

    if (state != AURORA_POWER_RUN)
    {
        ctx->start_success_since_ms = 0U;
        ctx->startup_success_recorded = false;
    }
}

/*---------------------------------------------------------------------------*
 * Name        : static void register_start_failure(aurora_power_stage_ctx_t *ctx,
 *               uint32_t now_ms)
 * Input       : ctx - 功率级上下文；now_ms - 当前毫秒
 * Output      : 无
 * Description : 本次预充/继电器/稳定性启动失败时把>15V动态延时增加1s，最大10s，并进入FAULT。
 *---------------------------------------------------------------------------*/
static void register_start_failure(aurora_power_stage_ctx_t *ctx, uint32_t now_ms)
{
    if (ctx->dynamic_start_delay_ms < AURORA_START_DELAY_MAX_MS)
    {
        ctx->dynamic_start_delay_ms += AURORA_START_DELAY_STEP_MS;
        if (ctx->dynamic_start_delay_ms > AURORA_START_DELAY_MAX_MS)
        {
            ctx->dynamic_start_delay_ms = AURORA_START_DELAY_MAX_MS;
        }
    }
    enter_state(ctx, AURORA_POWER_FAULT, now_ms);
}

/*---------------------------------------------------------------------------*
 * Name        : static uint16_t slew_duty(uint16_t previous, uint16_t target)
 * Input       : previous - 上一周期Q15；target - 目标Q15
 * Output      : 单步受限后的Q15
 * Description : 升降占空比均限制每次变化量，避免主循环命令突变。
 *---------------------------------------------------------------------------*/
static uint16_t slew_duty(uint16_t previous, uint16_t target)
{
    if (target > previous)
    {
        const uint32_t next = (uint32_t)previous + AURORA_DUTY_STEP_Q15;
        return (uint16_t)((next < target) ? next : target);
    }
    if (target < previous)
    {
        const uint32_t difference = (uint32_t)previous - target;
        return (difference > AURORA_DUTY_STEP_Q15) ?
                   (uint16_t)(previous - AURORA_DUTY_STEP_Q15) : target;
    }
    return previous;
}

/*---------------------------------------------------------------------------*
 * Name        : static uint16_t power_to_duty(aurora_power_stage_ctx_t *ctx,
 *               const aurora_measurement_t *sample, uint32_t power_command_mw,
 *               uint16_t maximum_duty_q15)
 * Input       : ctx - 功率级上下文；sample - 测量；power_command_mw - PV侧功率命令；maximum_duty_q15 - 状态上限
 * Output      : 前馈+PI+斜率限制后的Q15占空比
 * Description : 只负责Boost物理执行器，MPPT/Charger均不直接接触CCR。
 *---------------------------------------------------------------------------*/
static uint16_t power_to_duty(aurora_power_stage_ctx_t *ctx,
                              const aurora_measurement_t *sample,
                              uint32_t power_command_mw,
                              uint16_t maximum_duty_q15)
{
    int64_t feedforward_q15;
    int32_t power_error_mw;
    int64_t correction_q15;
    int64_t target_q15;

    if ((sample->pv_voltage_mv <= 0) || (power_command_mw == 0U))
    {
        return 0U;
    }

    if (sample->bus_voltage_mv <= sample->pv_voltage_mv)
    {
        /* 预充初期BST_U较低时从最小Duty爬升，避免理想Boost公式失效后永远不启动。 */
        feedforward_q15 = AURORA_DUTY_MIN_Q15;
    }
    else
    {
        feedforward_q15 =
            ((int64_t)(sample->bus_voltage_mv - sample->pv_voltage_mv) *
             AURORA_DUTY_Q15_ONE) /
            sample->bus_voltage_mv;
    }

    power_error_mw = (int32_t)power_command_mw - sample->pv_power_mw;
    ctx->power_integral +=
        (int64_t)power_error_mw / AURORA_POWER_PI_INTEGRAL_DIVISOR;
    if (ctx->power_integral > AURORA_POWER_PI_INTEGRAL_LIMIT_Q15)
    {
        ctx->power_integral = AURORA_POWER_PI_INTEGRAL_LIMIT_Q15;
    }
    if (ctx->power_integral < -AURORA_POWER_PI_INTEGRAL_LIMIT_Q15)
    {
        ctx->power_integral = -AURORA_POWER_PI_INTEGRAL_LIMIT_Q15;
    }

    correction_q15 =
        ((int64_t)power_error_mw * AURORA_POWER_PI_KP_NUMERATOR) /
        AURORA_POWER_PI_KP_DENOMINATOR_MW;
    target_q15 = feedforward_q15 + correction_q15 + ctx->power_integral;

    if (target_q15 < AURORA_DUTY_MIN_Q15)
    {
        target_q15 = AURORA_DUTY_MIN_Q15;
    }
    if (target_q15 > maximum_duty_q15)
    {
        target_q15 = maximum_duty_q15;
    }
    return slew_duty(ctx->duty_q15, (uint16_t)target_q15);
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_power_stage_init(aurora_power_stage_ctx_t *ctx,
 *               uint32_t now_ms)
 * Input       : ctx - 功率级上下文；now_ms - 当前毫秒
 * Output      : 无
 * Description : 从WAIT_PV安全态启动，动态启动延时初始化为V2.7最小1s，继电器和PWM均关闭。
 *---------------------------------------------------------------------------*/
void aurora_power_stage_init(aurora_power_stage_ctx_t *ctx, uint32_t now_ms)
{
    if (ctx == NULL)
    {
        return;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->state = AURORA_POWER_WAIT_PV;
    ctx->state_since_ms = now_ms;
    ctx->dynamic_start_delay_ms = AURORA_START_DELAY_MIN_MS;
}

/*---------------------------------------------------------------------------*
 * Name        : aurora_power_command_t aurora_power_stage_step(
 *               aurora_power_stage_ctx_t *ctx, const aurora_measurement_t *sample,
 *               const aurora_mppt_output_t *mppt, const aurora_charge_output_t *charger,
 *               bool protection_safe, bool zero_cal_ready, bool zero_cal_failed,
 *               uint32_t now_ms)
 * Input       : ctx/sample/mppt/charger - 控制上下文；protection_safe - 软件保护许可；
 *               zero_cal_ready/failed - PV_I运行时零点状态；now_ms - 当前毫秒
 * Output      : PWM/继电器/Duty命令和功率级状态
 * Description : 严格执行启动延时→零点校准→Boost预充BST_U→压差稳定→继电器→BAT_U稳定10s→RUN。
 *---------------------------------------------------------------------------*/
aurora_power_command_t aurora_power_stage_step(aurora_power_stage_ctx_t *ctx,
                                               const aurora_measurement_t *sample,
                                               const aurora_mppt_output_t *mppt,
                                               const aurora_charge_output_t *charger,
                                               bool protection_safe,
                                               bool zero_cal_ready,
                                               bool zero_cal_failed,
                                               uint32_t now_ms)
{
    aurora_power_command_t command;
    uint32_t requested_power_mw = 0U;
    int32_t relay_delta_mv = 0;
    const uint32_t required = AURORA_MEAS_VALID_PV_V |
                              AURORA_MEAS_VALID_PV_POWER |
                              AURORA_MEAS_VALID_BAT_V |
                              AURORA_MEAS_VALID_BUS_V;

    memset(&command, 0, sizeof(command));
    if ((ctx == NULL) || (sample == NULL) || (mppt == NULL) || (charger == NULL))
    {
        command.state = AURORA_POWER_FAULT;
        return command;
    }

    if ((uint32_t)ctx->state > (uint32_t)AURORA_POWER_FAULT)
    {
        ctx->relay_closed = false;
        enter_state(ctx, AURORA_POWER_FAULT, now_ms);
    }

    /* 严重保护触发时V2.7动态启动延时回到1s，恢复后必须重新完整启动。 */
    if (!protection_safe && (ctx->state != AURORA_POWER_FAULT))
    {
        ctx->dynamic_start_delay_ms = AURORA_START_DELAY_MIN_MS;
        enter_state(ctx, AURORA_POWER_FAULT, now_ms);
    }

    /*
     * 非FAULT状态缺少关键测量时立即拒绝推进。FAULT必须继续执行20ms放能释放，
     * 否则ADC故障与功率故障同时发生时可能让继电器一直保持吸合。
     */
    if (((sample->valid_mask & required) != required) &&
        (ctx->state != AURORA_POWER_FAULT))
    {
        ctx->duty_q15 = 0U;
        command.state = ctx->state;
        command.relay_enable = ctx->relay_closed;
        return command;
    }

    /* 弱光临界点先做连续资格确认；>=13V同时继续作为零点校准前2s稳定计时。 */
    if (sample->pv_voltage_mv >= AURORA_PV_START_MIN_MV)
    {
        if (ctx->pv_valid_since_ms == 0U)
        {
            ctx->pv_valid_since_ms = now_ms;
        }
    }
    else
    {
        ctx->pv_valid_since_ms = 0U;
    }

    if (sample->pv_voltage_mv >= AURORA_PV_FAST_START_MV)
    {
        if (ctx->pv_fast_valid_since_ms == 0U)
        {
            ctx->pv_fast_valid_since_ms = now_ms;
        }
    }
    else
    {
        ctx->pv_fast_valid_since_ms = 0U;
    }

    if ((sample->valid_mask & required) == required)
    {
        relay_delta_mv = abs_i32(sample->bus_voltage_mv - sample->battery_voltage_mv);
    }

    switch (ctx->state)
    {
    case AURORA_POWER_WAIT_PV:
        ctx->relay_closed = false;
        if ((ctx->pv_fast_valid_since_ms != 0U) &&
            (elapsed_ms(now_ms, ctx->pv_fast_valid_since_ms) >= AURORA_PV_START_QUALIFY_MS))
        {
            ctx->selected_start_delay_ms = ctx->dynamic_start_delay_ms;
            enter_state(ctx, AURORA_POWER_START_DELAY, now_ms);
        }
        else if ((ctx->pv_valid_since_ms != 0U) &&
                 (elapsed_ms(now_ms, ctx->pv_valid_since_ms) >= AURORA_PV_START_QUALIFY_MS))
        {
            ctx->selected_start_delay_ms = AURORA_START_MID_VOLTAGE_DELAY_MS;
            enter_state(ctx, AURORA_POWER_START_DELAY, now_ms);
        }
        break;

    case AURORA_POWER_START_DELAY:
        ctx->relay_closed = false;
        if (sample->pv_voltage_mv < AURORA_PV_START_MIN_MV)
        {
            enter_state(ctx, AURORA_POWER_WAIT_PV, now_ms);
        }
        else if (elapsed_ms(now_ms, ctx->state_since_ms) >= ctx->selected_start_delay_ms)
        {
            enter_state(ctx, AURORA_POWER_ZERO_CAL, now_ms);
        }
        break;

    case AURORA_POWER_ZERO_CAL:
        ctx->relay_closed = false;
        if (sample->pv_voltage_mv < AURORA_PV_START_MIN_MV)
        {
            enter_state(ctx, AURORA_POWER_WAIT_PV, now_ms);
        }
        else if (zero_cal_failed)
        {
            register_start_failure(ctx, now_ms);
        }
        else if (zero_cal_ready && (ctx->pv_valid_since_ms != 0U) &&
                 (elapsed_ms(now_ms, ctx->pv_valid_since_ms) >=
                  AURORA_ZERO_CAL_PV_STABLE_MS))
        {
            enter_state(ctx, AURORA_POWER_WAIT_BATTERY, now_ms);
        }
        break;

    case AURORA_POWER_WAIT_BATTERY:
        ctx->relay_closed = false;
        if (sample->pv_voltage_mv < AURORA_PV_START_MIN_MV)
        {
            enter_state(ctx, AURORA_POWER_WAIT_PV, now_ms);
        }
        else if (sample->battery_voltage_mv > AURORA_BATTERY_DETECT_MIN_MV)
        {
            enter_state(ctx, AURORA_POWER_PRECHARGE, now_ms);
        }
        break;

    case AURORA_POWER_PRECHARGE:
        /*
         * 硬性要求：PRECHARGE期间继电器始终断开，只允许受限Boost先把BST_U充高。
         * 只有|BST_U-BAT_U|<=1.5V连续1s，才撤销PWM并提出继电器闭合请求。
         */
        ctx->relay_closed = false;
        if (sample->pv_voltage_mv < AURORA_PV_START_MIN_MV)
        {
            enter_state(ctx, AURORA_POWER_WAIT_PV, now_ms);
            break;
        }
        if (elapsed_ms(now_ms, ctx->state_since_ms) >= AURORA_PRECHARGE_TIMEOUT_MS)
        {
            register_start_failure(ctx, now_ms);
            break;
        }
        if (relay_delta_mv <= AURORA_RELAY_CLOSE_DELTA_MV)
        {
            if (ctx->delta_ok_since_ms == 0U)
            {
                ctx->delta_ok_since_ms = now_ms;
            }
            else if (elapsed_ms(now_ms, ctx->delta_ok_since_ms) >=
                     AURORA_RELAY_DELTA_HOLD_MS)
            {
                /* 先把软件Duty清零；Service还会先物理关PWM，再真正吸合继电器。 */
                ctx->duty_q15 = 0U;
                ctx->power_integral = 0LL;
                ctx->relay_closed = true;
                enter_state(ctx, AURORA_POWER_RELAY_SETTLE, now_ms);
                break;
            }
        }
        else
        {
            ctx->delta_ok_since_ms = 0U;
        }

        requested_power_mw = AURORA_PRECHARGE_POWER_MW;
        ctx->duty_q15 = power_to_duty(
            ctx, sample, requested_power_mw,
            (uint16_t)(AURORA_DUTY_MAX_Q15 / AURORA_PRECHARGE_DUTY_LIMIT_DIVISOR));
        command.pwm_enable = true;
        break;

    case AURORA_POWER_RELAY_SETTLE:
        ctx->relay_closed = true;
        ctx->duty_q15 = 0U;
        if (elapsed_ms(now_ms, ctx->state_since_ms) >= AURORA_RELAY_SETTLE_MS)
        {
            if (relay_delta_mv <= AURORA_RELAY_VERIFY_DELTA_MV)
            {
                ctx->bat_stability_since_ms = now_ms;
                ctx->bat_stability_min_mv = sample->battery_voltage_mv;
                ctx->bat_stability_max_mv = sample->battery_voltage_mv;
                enter_state(ctx, AURORA_POWER_BAT_STABILITY, now_ms);
            }
            else
            {
                register_start_failure(ctx, now_ms);
            }
        }
        break;

    case AURORA_POWER_BAT_STABILITY:
        /* Relay已闭合但PWM继续关闭；完整观察10s，防止电池/接线不稳时直接进入MPPT。 */
        ctx->relay_closed = true;
        ctx->duty_q15 = 0U;
        if (sample->battery_voltage_mv < ctx->bat_stability_min_mv)
        {
            ctx->bat_stability_min_mv = sample->battery_voltage_mv;
        }
        if (sample->battery_voltage_mv > ctx->bat_stability_max_mv)
        {
            ctx->bat_stability_max_mv = sample->battery_voltage_mv;
        }
        if (elapsed_ms(now_ms, ctx->bat_stability_since_ms) >=
            AURORA_BAT_STABILITY_WINDOW_MS)
        {
            if ((ctx->bat_stability_max_mv - ctx->bat_stability_min_mv) <=
                AURORA_BAT_STABILITY_MAX_SPAN_MV)
            {
                enter_state(ctx, AURORA_POWER_RUN, now_ms);
            }
            else
            {
                register_start_failure(ctx, now_ms);
            }
        }
        break;

    case AURORA_POWER_RUN:
        ctx->relay_closed = true;

        /*
         * Complete复充或Float低压维持失败时，先立即撤销Duty并重新做BAT_U稳定资格。
         * 这属于正常充电会话重启：保持继电器闭合，不冒充PV_I传感器故障重新校零。
         */
        if (charger->restart_required)
        {
            ctx->duty_q15 = 0U;
            ctx->power_integral = 0LL;
            ctx->bat_stability_since_ms = now_ms;
            ctx->bat_stability_min_mv = sample->battery_voltage_mv;
            ctx->bat_stability_max_mv = sample->battery_voltage_mv;
            enter_state(ctx, AURORA_POWER_BAT_STABILITY, now_ms);
            break;
        }

        /* 只有PV<13V且MPPT当前未运行才累计“真正无发电”30min。电池充满不等于无太阳。 */
        if ((sample->pv_voltage_mv < AURORA_PV_START_MIN_MV) && !mppt->valid)
        {
            if (ctx->no_sun_since_ms == 0U)
            {
                ctx->no_sun_since_ms = now_ms;
            }
            if (elapsed_ms(now_ms, ctx->no_sun_since_ms) >=
                AURORA_NO_SUN_OPEN_RELAY_MS)
            {
                ctx->relay_closed = false;
                enter_state(ctx, AURORA_POWER_NO_SUN, now_ms);
                break;
            }
        }
        else
        {
            ctx->no_sun_since_ms = 0U;
        }

        /* 正常充电估算电流>=80mA稳定1s后，本轮只把下次动态启动延时减少一次。 */
        if (!ctx->startup_success_recorded &&
            ((sample->valid_mask & AURORA_MEAS_VALID_BAT_I_EST) != 0U) &&
            (sample->battery_current_est_ma >= AURORA_START_SUCCESS_CURRENT_MA))
        {
            if (ctx->start_success_since_ms == 0U)
            {
                ctx->start_success_since_ms = now_ms;
            }
            else if (elapsed_ms(now_ms, ctx->start_success_since_ms) >=
                     AURORA_START_SUCCESS_HOLD_MS)
            {
                if (ctx->dynamic_start_delay_ms > AURORA_START_DELAY_MIN_MS)
                {
                    ctx->dynamic_start_delay_ms -= AURORA_START_DELAY_STEP_MS;
                }
                ctx->startup_success_recorded = true;
            }
        }
        else if (!ctx->startup_success_recorded)
        {
            ctx->start_success_since_ms = 0U;
        }

        if (charger->allow_charge && mppt->valid)
        {
            requested_power_mw = mppt->theoretical_power_mw;
            if ((charger->pv_power_limit_mw != 0U) &&
                (requested_power_mw > charger->pv_power_limit_mw))
            {
                requested_power_mw = charger->pv_power_limit_mw;
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
        ctx->relay_closed = false;
        ctx->duty_q15 = 0U;
        if ((ctx->pv_valid_since_ms != 0U) &&
            (elapsed_ms(now_ms, ctx->pv_valid_since_ms) >= AURORA_PV_START_QUALIFY_MS))
        {
            enter_state(ctx, AURORA_POWER_WAIT_PV, now_ms);
        }
        break;

    case AURORA_POWER_FAULT:
        ctx->duty_q15 = 0U;
        if (elapsed_ms(now_ms, ctx->state_since_ms) >= AURORA_RELAY_FAULT_RELEASE_MS)
        {
            ctx->relay_closed = false;
            if (protection_safe)
            {
                enter_state(ctx, AURORA_POWER_WAIT_PV, now_ms);
            }
        }
        break;

    case AURORA_POWER_OFF:
        ctx->relay_closed = false;
        ctx->duty_q15 = 0U;
        break;
    }

    command.duty_q15 = command.pwm_enable ? ctx->duty_q15 : 0U;
    command.relay_enable = ctx->relay_closed;
    command.state = ctx->state;
    return command;
}
