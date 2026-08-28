#include "power_stage.h"

#include "app_config.h"

#include <limits.h>
#include <string.h>

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
 * Name        : static int32_t abs_i32(int32_t value)
 * Input       : value - 32位有符号输入
 * Output      : 输入的非负绝对值；INT32_MIN时饱和为INT32_MAX
 * Description : 计算母线与电池压差时避免对INT32_MIN取负造成有符号溢出。
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
 * Input       : ctx - 功率级状态机上下文；state - 目标功率状态；
 *               now_ms - 当前毫秒时间戳
 * Output      : 无
 * Description : 统一记录状态进入时间并清理压差计时；进入安全状态时同步清零Duty与功率积分。
 *---------------------------------------------------------------------------*/
static void enter_state(aurora_power_stage_ctx_t *ctx,
                        aurora_power_state_t state,
                        uint32_t now_ms)
{
    ctx->state = state;
    ctx->state_since_ms = now_ms;
    ctx->delta_ok_since_ms = 0U;

    if ((state == AURORA_POWER_OFF) ||
        (state == AURORA_POWER_WAIT_BATTERY) ||
        (state == AURORA_POWER_FAULT))
    {
        /* 故障或重新等待电池后，下一次发波必须重新从0占空比起步。 */
        ctx->duty_q15 = 0U;
        ctx->power_integral = 0LL;
    }
}

/*---------------------------------------------------------------------------*
 * Name        : static uint16_t slew_duty(uint16_t previous, uint16_t target)
 * Input       : previous - 上一周期物理占空比，Q15；target - 本周期目标占空比，Q15
 * Output      : 按单步限制后的物理占空比，Q15
 * Description : 对升/降占空比使用同一最大步长，防止控制命令在相邻主循环周期发生突变。
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
 * Input       : ctx - 功率级上下文；sample - 最新测量；power_command_mw - 最终功率命令；
 *               maximum_duty_q15 - 当前状态允许的最大物理占空比
 * Output      : 经前馈、功率PI、限幅和斜率限制后的Q15占空比
 * Description : 把物理功率命令转换为Q6占空比；MPPT只提供功率和电压参考，不接触CCR编码。
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
        /*
         * 预充初期BST_U可能尚低于PV_U，理想Boost前馈公式不成立；
         * 从最小Duty受限爬升，避免因返回0而永远无法建立母线。
         */
        feedforward_q15 = AURORA_DUTY_MIN_Q15;
    }
    else
    {
        /* 理想Boost前馈：D≈1-Vpv/Vbus，仅作为PI附近的起点。 */
        feedforward_q15 =
            ((int64_t)(sample->bus_voltage_mv - sample->pv_voltage_mv) *
             AURORA_DUTY_Q15_ONE) /
            sample->bus_voltage_mv;
    }

    power_error_mw = (int32_t)power_command_mw - sample->pv_power_mw;

    /* 低带宽功率修正只改变物理Duty；最终CCR方向由目标驱动转换。 */
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
    correction_q15 += ctx->power_integral;
    target_q15 = feedforward_q15 + correction_q15;

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
 * Input       : ctx - 功率级状态机上下文；now_ms - 当前毫秒时间戳
 * Output      : 无
 * Description : 清零功率级动态状态并从WAIT_BATTERY启动，确保PWM和继电器默认均不放行。
 *---------------------------------------------------------------------------*/
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

/*---------------------------------------------------------------------------*
 * Name        : aurora_power_command_t aurora_power_stage_step(
 *               aurora_power_stage_ctx_t *ctx, const aurora_measurement_t *sample,
 *               const aurora_mppt_output_t *mppt, const aurora_charge_output_t *charger,
 *               bool protection_safe, uint32_t now_ms)
 * Input       : ctx - 功率级上下文；sample - 最新测量；mppt - MPPT物理量输出；
 *               charger - 充电许可与上限；protection_safe - 保护链安全许可；
 *               now_ms - 当前毫秒时间戳
 * Output      : PWM使能、继电器使能、Q15占空比和功率状态命令
 * Description : 推进预充/继电器/运行/无光/故障状态；故障恢复必须回到WAIT_BATTERY并重新预充。
 *---------------------------------------------------------------------------*/
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
    const uint32_t required_measurements = AURORA_MEAS_VALID_PV_V |
                                           AURORA_MEAS_VALID_PV_POWER |
                                           AURORA_MEAS_VALID_BAT_V |
                                           AURORA_MEAS_VALID_BUS_V;

    memset(&command, 0, sizeof(command));

    if ((ctx == NULL) || (sample == NULL) || (mppt == NULL) || (charger == NULL))
    {
        command.state = AURORA_POWER_FAULT;
        return command;
    }

    /*
     * switch不再依赖default兜底：若上下文状态越界，立即关闭继电器并锁入FAULT。
     * 这样即使RAM被破坏，也不会沿用故障前的relay/duty状态。
     */
    if ((uint32_t)ctx->state > (uint32_t)AURORA_POWER_FAULT)
    {
        ctx->relay_closed = false;
        enter_state(ctx, AURORA_POWER_FAULT, now_ms);
    }

    if (!protection_safe && (ctx->state != AURORA_POWER_FAULT))
    {
        /* 软件命令先撤销PWM；Service随后执行硬件关波并延时释放继电器。 */
        enter_state(ctx, AURORA_POWER_FAULT, now_ms);
    }

    // 非FAULT状态缺测时拒绝推进；FAULT必须继续执行定时放能释放，不能被早退挡住。
    if (((sample->valid_mask & required_measurements) != required_measurements) && (ctx->state != AURORA_POWER_FAULT))
    {
        /* 缺少任一功率级关键测量时，非FAULT状态只保留当前继电器状态并强制Duty为0。 */
        ctx->duty_q15 = 0U;
        command.state = ctx->state;
        command.relay_enable = ctx->relay_closed;
        return command;
    }

    relay_delta_mv =
        abs_i32(sample->bus_voltage_mv - sample->battery_voltage_mv);

    switch (ctx->state)
    {
    case AURORA_POWER_WAIT_BATTERY:
        ctx->relay_closed = false;
        if ((sample->battery_voltage_mv > AURORA_BATTERY_DETECT_MIN_MV) &&
            (sample->pv_voltage_mv >= AURORA_PV_START_MIN_MV))
        {
            enter_state(ctx, AURORA_POWER_PRECHARGE, now_ms);
        }
        break;

    case AURORA_POWER_PRECHARGE:
        ctx->relay_closed = false;

        if (elapsed_ms(now_ms, ctx->state_since_ms) >= AURORA_PRECHARGE_TIMEOUT_MS)
        {
            /* 规定时间内无法建立母线压差，判定预充失败。 */
            enter_state(ctx, AURORA_POWER_FAULT, now_ms);
        }
        else if (relay_delta_mv <= AURORA_RELAY_CLOSE_DELTA_MV)
        {
            /* 压差必须连续稳定，避免在纹波交越瞬间误吸合继电器。 */
            if (ctx->delta_ok_since_ms == 0U)
            {
                ctx->delta_ok_since_ms = now_ms;
            }
            else if (elapsed_ms(now_ms, ctx->delta_ok_since_ms) >=
                     AURORA_RELAY_DELTA_HOLD_MS)
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
        ctx->duty_q15 =
            power_to_duty(ctx,
                          sample,
                          requested_power_mw,
                          (uint16_t)(AURORA_DUTY_MAX_Q15 /
                                     AURORA_PRECHARGE_DUTY_LIMIT_DIVISOR));
        command.pwm_enable = true;
        break;

    case AURORA_POWER_RELAY_SETTLE:
        ctx->relay_closed = true;
        ctx->duty_q15 = 0U;

        if (elapsed_ms(now_ms, ctx->state_since_ms) >= AURORA_RELAY_SETTLE_MS)
        {
            /* 机械稳定后再次验证压差；失败则立即断继电器并进入FAULT。 */
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

        if ((sample->pv_power_mw < AURORA_NO_SUN_ENTER_MW) ||
            !charger->allow_charge)
        {
            if (ctx->no_sun_since_ms == 0U)
            {
                ctx->no_sun_since_ms = now_ms;
            }
            if (elapsed_ms(now_ms, ctx->no_sun_since_ms) >=
                AURORA_NO_SUN_OPEN_RELAY_MS)
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
            /* 电池状态机优先限制MPPT理论功率，额定功率再做最后上限。 */
            requested_power_mw = mppt->theoretical_power_mw;
            if (requested_power_mw > charger->power_limit_mw)
            {
                requested_power_mw = charger->power_limit_mw;
            }
            if (requested_power_mw > AURORA_RATED_POWER_MW)
            {
                requested_power_mw = AURORA_RATED_POWER_MW;
            }

            ctx->duty_q15 = power_to_duty(ctx,
                                         sample,
                                         requested_power_mw,
                                         AURORA_DUTY_MAX_Q15);
            command.pwm_enable = requested_power_mw != 0U;
        }
        else
        {
            /* 普通停充采用斜率下降；真正硬件关波仍由Service统一执行。 */
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
            /*
             * 无论故障信号多快恢复，都先满足最短放能时间再断继电器；
             * 只有保护链同时恢复安全后，下一拍才回WAIT_BATTERY重新预充。
             */
            ctx->relay_closed = false;
            if (protection_safe)
            {
                enter_state(ctx, AURORA_POWER_WAIT_BATTERY, now_ms);
            }
        }
        break;

    case AURORA_POWER_OFF:
        ctx->duty_q15 = 0U;
        ctx->relay_closed = false;
        break;
    }

    command.duty_q15 = command.pwm_enable ? ctx->duty_q15 : 0U;
    command.relay_enable = ctx->relay_closed;
    command.state = ctx->state;
    return command;
}
