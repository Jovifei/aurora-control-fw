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
 * Name        : static uint32_t min_u32(uint32_t a, uint32_t b)
 * Input       : a/b - 无符号输入
 * Output      : 较小值
 * Description : 避免母线绝对上限和模式目标上限的重复三目表达式。
 *---------------------------------------------------------------------------*/
static uint32_t min_u32(uint32_t a, uint32_t b)
{
    return (a < b) ? a : b;
}

/*---------------------------------------------------------------------------*
 * Name        : static void enter_state(aurora_power_stage_ctx_t *ctx,
 *               aurora_power_state_t state, uint32_t now_ms)
 * Input       : ctx - 功率级上下文；state - 目标状态；now_ms - 当前毫秒
 * Output      : 无
 * Description : 统一切换状态并清理本状态局部计时；非发波状态同步清零Duty和功率积分。
 *---------------------------------------------------------------------------*/
static void enter_state(aurora_power_stage_ctx_t *ctx, aurora_power_state_t state, uint32_t now_ms)
{
    ctx->state = state;
    ctx->state_since_ms = now_ms;
    ctx->delta_ok_since_ms = 0U;
    ctx->demo_probe_since_ms = 0U;
    ctx->demo_no_load_since_ms = 0U;

    if (state == AURORA_POWER_RELAY_HOLD_OFF)
    {
        /* 0是32位ADC发布序号的合法回绕值；有效性必须由独立标志表示。 */
        ctx->relay_holdoff_sequence = 0U;
        ctx->relay_holdoff_sequence_valid = false;
    }

    if ((state != AURORA_POWER_PRECHARGE) && (state != AURORA_POWER_RUN) &&
        (state != AURORA_POWER_DEMO_PROBE) && (state != AURORA_POWER_DEMO_RUN))
    {
        ctx->duty_q15 = 0U;
        ctx->power_integral = 0LL;
    }

    if ((state != AURORA_POWER_RUN) && (state != AURORA_POWER_DEMO_RUN))
    {
        ctx->start_success_since_ms = 0U;
        ctx->startup_success_recorded = false;
    }
}

/*---------------------------------------------------------------------------*
 * Name        : static void increase_dynamic_start_delay(
 *               aurora_power_stage_ctx_t *ctx)
 * Input       : ctx - 功率级上下文
 * Output      : 无
 * Description : 启动失败后按V2.7思想增加>15V动态等待，最大10s；弱光不调用本函数。
 *---------------------------------------------------------------------------*/
static void increase_dynamic_start_delay(aurora_power_stage_ctx_t *ctx)
{
    if (ctx->dynamic_start_delay_ms < AURORA_START_DELAY_MAX_MS)
    {
        ctx->dynamic_start_delay_ms += AURORA_START_DELAY_STEP_MS;
        if (ctx->dynamic_start_delay_ms > AURORA_START_DELAY_MAX_MS)
        {
            ctx->dynamic_start_delay_ms = AURORA_START_DELAY_MAX_MS;
        }
    }
}

/*---------------------------------------------------------------------------*
 * Name        : static void register_start_failure(aurora_power_stage_ctx_t *ctx,
 *               aurora_start_failure_reason_t reason, uint32_t now_ms)
 * Input       : ctx - 功率级上下文；reason - 分类失败原因；now_ms - 当前毫秒
 * Output      : 无
 * Description : 按原因区分弱光、可重试异常和必须立即锁存的母线/Demo异常，禁止无限驱动Relay。
 *---------------------------------------------------------------------------*/
static void register_start_failure(aurora_power_stage_ctx_t *ctx,
                                   aurora_start_failure_reason_t reason, uint32_t now_ms)
{
    ctx->last_failure_reason = reason;

    if (reason == AURORA_START_FAIL_PV_WEAK)
    {
        /* 弱光不是硬件故障，不消耗零点、母线或继电器重试额度。 */
        ctx->relay_closed = false;
        enter_state(ctx, AURORA_POWER_WAIT_PV, now_ms);
        return;
    }

    increase_dynamic_start_delay(ctx);
    switch (reason)
    {
    case AURORA_START_FAIL_ZERO_CAL:
        if (ctx->zero_cal_failure_count < UINT8_MAX)
        {
            ctx->zero_cal_failure_count++;
        }
        ctx->startup_locked = ctx->zero_cal_failure_count >= AURORA_ZERO_CAL_SESSION_RETRY_MAX;
        break;

    case AURORA_START_FAIL_BUS_PRECHARGE_TIMEOUT:
        if (ctx->precharge_failure_count < UINT8_MAX)
        {
            ctx->precharge_failure_count++;
        }
        ctx->startup_locked = ctx->precharge_failure_count >= AURORA_PRECHARGE_RETRY_MAX;
        break;

    case AURORA_START_FAIL_RELAY_CLOSE_VERIFY:
        if (ctx->relay_failure_count < UINT8_MAX)
        {
            ctx->relay_failure_count++;
        }
        ctx->startup_locked = ctx->relay_failure_count >= AURORA_RELAY_VERIFY_RETRY_MAX;
        break;

    case AURORA_START_FAIL_BAT_STABILITY:
        if (ctx->bat_stability_failure_count < UINT8_MAX)
        {
            ctx->bat_stability_failure_count++;
        }
        ctx->startup_locked = ctx->bat_stability_failure_count >= AURORA_BAT_STABILITY_RETRY_MAX;
        break;

    case AURORA_START_FAIL_BUS_OVERSHOOT:
    case AURORA_START_FAIL_BUS_MEAS_INVALID:
    case AURORA_START_FAIL_DEMO_EXTERNAL_SOURCE:
    case AURORA_START_FAIL_DEMO_OVERLOAD:
        /* 过压、量程不可信、外部有源电压和过载不能通过“多试几次”解决。 */
        ctx->startup_locked = true;
        break;

    case AURORA_START_FAIL_DEMO_NO_LOAD:
        /* 无负载是正常停止条件，不锁死硬件；保留原因供调试和协议扩展使用。 */
        ctx->startup_locked = false;
        ctx->relay_closed = false;
        enter_state(ctx, AURORA_POWER_NO_SUN, now_ms);
        return;

    case AURORA_START_FAIL_NONE:
    case AURORA_START_FAIL_PV_WEAK:
    default:
        ctx->startup_locked = true;
        break;
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
        return (difference > AURORA_DUTY_STEP_Q15) ? (uint16_t)(previous - AURORA_DUTY_STEP_Q15)
                                                   : target;
    }
    return previous;
}

/*---------------------------------------------------------------------------*
 * Name        : static uint16_t power_to_duty(aurora_power_stage_ctx_t *ctx,
 *               const aurora_measurement_t *sample, uint32_t power_command_mw,
 *               uint16_t maximum_duty_q15)
 * Input       : ctx - 功率级上下文；sample - 测量；
 *               power_command_mw - PV侧功率命令；maximum_duty_q15 - 状态上限
 * Output      : 前馈+PI+斜率限制后的Q15占空比
 * Description : 只负责Boost物理执行器，MPPT/Charger均不直接接触CCR。
 *---------------------------------------------------------------------------*/
static uint16_t power_to_duty(aurora_power_stage_ctx_t *ctx, const aurora_measurement_t *sample,
                              uint32_t power_command_mw, uint16_t maximum_duty_q15)
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
            ((int64_t)(sample->bus_voltage_mv - sample->pv_voltage_mv) * AURORA_DUTY_Q15_ONE) /
            sample->bus_voltage_mv;
    }

    power_error_mw = (int32_t)power_command_mw - sample->pv_power_mw;
    ctx->power_integral += (int64_t)power_error_mw / AURORA_POWER_PI_INTEGRAL_DIVISOR;
    if (ctx->power_integral > AURORA_POWER_PI_INTEGRAL_LIMIT_Q15)
    {
        ctx->power_integral = AURORA_POWER_PI_INTEGRAL_LIMIT_Q15;
    }
    if (ctx->power_integral < -AURORA_POWER_PI_INTEGRAL_LIMIT_Q15)
    {
        ctx->power_integral = -AURORA_POWER_PI_INTEGRAL_LIMIT_Q15;
    }

    correction_q15 = ((int64_t)power_error_mw * AURORA_POWER_PI_KP_NUMERATOR) /
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
 * Name        : static uint32_t bus_absolute_limit_mv(
 *               aurora_operating_mode_t operating_mode,
 *               const aurora_measurement_t *sample,
 *               const aurora_charge_output_t *charger,
 *               uint32_t demo_target_voltage_mv)
 * Input       : operating_mode - Battery/Demo；sample - 最新BAT_U；charger - Battery目标；
 *               demo_target_voltage_mv - Demo目标
 * Output      : 当前模式可见的保守BST_U绝对上限，mV
 * Description : 软件目标加裕量后仍受现有26:1分压约84V保守上限限制；不能用软件掩盖硬件量程问题。
 *---------------------------------------------------------------------------*/
static uint32_t bus_absolute_limit_mv(aurora_operating_mode_t operating_mode,
                                      const aurora_measurement_t *sample,
                                      const aurora_charge_output_t *charger,
                                      uint32_t demo_target_voltage_mv)
{
    uint32_t target_mv = demo_target_voltage_mv;

    if (operating_mode == AURORA_MODE_BATTERY)
    {
        target_mv = charger->voltage_target_mv;
        /* 准入早期Charger可能尚未发布CV目标，至少以实测BAT_U作为母线安全参考。 */
        if ((sample->battery_voltage_mv > 0) && ((uint32_t)sample->battery_voltage_mv > target_mv))
        {
            target_mv = (uint32_t)sample->battery_voltage_mv;
        }
    }
    if (target_mv > (UINT32_MAX - AURORA_BUS_TARGET_OV_MARGIN_MV))
    {
        return AURORA_BUS_ABSOLUTE_MAX_MV;
    }
    return min_u32(target_mv + AURORA_BUS_TARGET_OV_MARGIN_MV, AURORA_BUS_ABSOLUTE_MAX_MV);
}

/*---------------------------------------------------------------------------*
 * Name        : static bool bus_measurement_invalid(const aurora_measurement_t *sample)
 * Input       : sample - 最新测量
 * Output      : true表示BST_U接近ADC满量程或无有效位
 * Description : 饱和值不能参与Relay均压，也不能被当成Demo输出电压证据。
 *---------------------------------------------------------------------------*/
static bool bus_measurement_invalid(const aurora_measurement_t *sample)
{
    return ((sample->valid_mask & AURORA_MEAS_VALID_BUS_V) == 0U) ||
           ((sample->diagnostic_mask & AURORA_MEAS_DIAG_BUS_ADC_SATURATED) != 0U);
}

/*---------------------------------------------------------------------------*
 * Name        : static bool bus_overvoltage(const aurora_measurement_t *sample,
 *               uint32_t absolute_limit_mv, bool compare_to_battery)
 * Input       : sample - 最新测量；absolute_limit_mv - 绝对上限；
 *               compare_to_battery - 是否检查相对BAT过冲
 * Output      : true表示母线已超过当前安全边界
 * Description : Battery预充同时检查相对过冲和绝对上限；Demo只检查绝对上限。
 *---------------------------------------------------------------------------*/
static bool bus_overvoltage(const aurora_measurement_t *sample, uint32_t absolute_limit_mv,
                            bool compare_to_battery)
{
    if ((sample->bus_voltage_mv < 0) || ((uint32_t)sample->bus_voltage_mv > absolute_limit_mv))
    {
        return true;
    }
    return compare_to_battery && (sample->bus_voltage_mv >
                                  (sample->battery_voltage_mv + AURORA_BUS_RELATIVE_OVERSHOOT_MV));
}

/*---------------------------------------------------------------------------*
 * Name        : static uint32_t demo_power_target_mw(
 *               const aurora_measurement_t *sample, uint32_t target_voltage_mv,
 *               uint32_t power_limit_mw)
 * Input       : sample - 最新测量；target_voltage_mv - Demo目标电压；power_limit_mw - 最大输入功率
 * Output      : Demo受限CV所需的PV功率命令，mW
 * Description : 没有输出电流传感器，Demo只做电压误差比例功率控制并受输入功率上限约束，不宣称恒流。
 *---------------------------------------------------------------------------*/
static uint32_t demo_power_target_mw(const aurora_measurement_t *sample, uint32_t target_voltage_mv,
                                     uint32_t power_limit_mw)
{
    uint64_t requested;

    if ((sample->battery_voltage_mv < 0) ||
        ((uint32_t)sample->battery_voltage_mv >= target_voltage_mv))
    {
        return 0U;
    }
    requested = (uint64_t)(target_voltage_mv - (uint32_t)sample->battery_voltage_mv) *
                AURORA_DEMO_CV_POWER_MW_PER_MV;
    return (requested > power_limit_mw) ? power_limit_mw : (uint32_t)requested;
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_power_stage_init(aurora_power_stage_ctx_t *ctx,
 *               uint32_t now_ms)
 * Input       : ctx - 功率级上下文；now_ms - 当前毫秒
 * Output      : 无
 * Description : 从WAIT_PV安全态启动，动态启动延时初始化为V2.7最小1s，Relay和PWM均关闭。
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
    ctx->last_failure_reason = AURORA_START_FAIL_NONE;
}

/*---------------------------------------------------------------------------*
 * Name        : aurora_power_command_t aurora_power_stage_step_ex(
 *               aurora_power_stage_ctx_t *ctx, const aurora_measurement_t *sample,
 *               const aurora_mppt_output_t *mppt, const aurora_charge_output_t *charger,
 *               bool protection_safe, bool zero_cal_ready, bool zero_cal_failed,
 *               bool relay_applied,
 *               aurora_operating_mode_t operating_mode,
 *               uint32_t demo_target_voltage_mv, uint32_t demo_power_limit_mw,
 *               uint32_t now_ms)
 * Input       : ctx/sample/mppt/charger - 控制上下文；protection_safe - 软件保护许可；
 *               zero_cal_ready/failed - PV_I运行时零点状态；relay_applied - Runtime已写Relay GPIO；
 *               operating_mode - Battery/Demo；
 *               Demo限制；now_ms - 当前毫秒
 * Output      : PWM/Relay/Duty命令和功率级状态
 * Description : Battery严格执行均压→关波20ms→新ADC→Relay→BAT稳定；Demo使用独立闭合条件。
 *---------------------------------------------------------------------------*/
aurora_power_command_t
aurora_power_stage_step_ex(aurora_power_stage_ctx_t *ctx, const aurora_measurement_t *sample,
                           const aurora_mppt_output_t *mppt, const aurora_charge_output_t *charger,
                           bool protection_safe, bool zero_cal_ready, bool zero_cal_failed,
                           bool relay_applied, aurora_operating_mode_t operating_mode,
                           uint32_t demo_target_voltage_mv, uint32_t demo_power_limit_mw,
                           uint32_t now_ms)
{
    aurora_power_command_t command;
    uint32_t requested_power_mw = 0U;
    uint32_t absolute_bus_limit_mv;
    int32_t relay_delta_mv = 0;
    const uint32_t required = AURORA_MEAS_VALID_PV_V | AURORA_MEAS_VALID_PV_POWER |
                              AURORA_MEAS_VALID_BAT_V | AURORA_MEAS_VALID_BUS_V;

    memset(&command, 0, sizeof(command));
    if ((ctx == NULL) || (sample == NULL) || (mppt == NULL) || (charger == NULL) ||
        (operating_mode >= AURORA_MODE_COUNT))
    {
        command.state = AURORA_POWER_FAULT;
        return command;
    }

    if ((demo_target_voltage_mv == 0U) ||
        (demo_target_voltage_mv > AURORA_DEMO_MAX_TARGET_VOLTAGE_MV))
    {
        demo_target_voltage_mv = AURORA_DEMO_TARGET_VOLTAGE_MV;
    }
    if ((demo_power_limit_mw == 0U) ||
        (demo_power_limit_mw > AURORA_DEMO_HARD_POWER_CAP_MW))
    {
        demo_power_limit_mw = AURORA_DEMO_POWER_LIMIT_MW;
    }
    absolute_bus_limit_mv =
        bus_absolute_limit_mv(operating_mode, sample, charger, demo_target_voltage_mv);

    if ((uint32_t)ctx->state > (uint32_t)AURORA_POWER_FAULT)
    {
        ctx->relay_closed = false;
        ctx->startup_locked = true;
        enter_state(ctx, AURORA_POWER_FAULT, now_ms);
    }

    /* 严重保护触发时动态启动延时回到1s，恢复后必须重新完整启动。 */
    if (!protection_safe && (ctx->state != AURORA_POWER_FAULT))
    {
        ctx->dynamic_start_delay_ms = AURORA_START_DELAY_MIN_MS;
        enter_state(ctx, AURORA_POWER_FAULT, now_ms);
    }

    if (((sample->valid_mask & required) != required) && (ctx->state != AURORA_POWER_FAULT))
    {
        ctx->duty_q15 = 0U;
        command.state = ctx->state;
        command.relay_enable = ctx->relay_closed;
        return command;
    }

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

    relay_delta_mv = abs_i32(sample->bus_voltage_mv - sample->battery_voltage_mv);

    switch (ctx->state)
    {
    case AURORA_POWER_WAIT_PV:
        ctx->relay_closed = false;
        ctx->demo_load_confirmed = false;
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
            register_start_failure(ctx, AURORA_START_FAIL_PV_WEAK, now_ms);
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
            register_start_failure(ctx, AURORA_START_FAIL_PV_WEAK, now_ms);
        }
        else if (zero_cal_failed)
        {
            register_start_failure(ctx, AURORA_START_FAIL_ZERO_CAL, now_ms);
        }
        else if (zero_cal_ready && (ctx->pv_valid_since_ms != 0U) &&
                 (elapsed_ms(now_ms, ctx->pv_valid_since_ms) >= AURORA_ZERO_CAL_PV_STABLE_MS))
        {
            ctx->zero_cal_failure_count = 0U;
            enter_state(ctx,
                        (operating_mode == AURORA_MODE_BATTERY) ? AURORA_POWER_WAIT_BATTERY
                                                                : AURORA_POWER_DEMO_OUTPUT_CHECK,
                        now_ms);
        }
        break;

    case AURORA_POWER_WAIT_BATTERY:
        ctx->relay_closed = false;
        if (operating_mode != AURORA_MODE_BATTERY)
        {
            enter_state(ctx, AURORA_POWER_DEMO_OUTPUT_CHECK, now_ms);
        }
        else if (sample->pv_voltage_mv < AURORA_PV_START_MIN_MV)
        {
            register_start_failure(ctx, AURORA_START_FAIL_PV_WEAK, now_ms);
        }
        else if (sample->battery_voltage_mv > AURORA_BATTERY_DETECT_MIN_MV)
        {
            ctx->precharge_pv_entry_mv = sample->pv_voltage_mv;
            ctx->precharge_pv_min_mv = sample->pv_voltage_mv;
            ctx->precharge_bus_start_mv = sample->bus_voltage_mv;
            ctx->precharge_bus_max_mv = sample->bus_voltage_mv;
            enter_state(ctx, AURORA_POWER_PRECHARGE, now_ms);
        }
        break;

    case AURORA_POWER_PRECHARGE:
        ctx->relay_closed = false;
        if (operating_mode != AURORA_MODE_BATTERY)
        {
            enter_state(ctx, AURORA_POWER_DEMO_OUTPUT_CHECK, now_ms);
            break;
        }
        if (sample->pv_voltage_mv < AURORA_PV_START_MIN_MV)
        {
            register_start_failure(ctx, AURORA_START_FAIL_PV_WEAK, now_ms);
            break;
        }
        if (bus_measurement_invalid(sample))
        {
            register_start_failure(ctx, AURORA_START_FAIL_BUS_MEAS_INVALID, now_ms);
            break;
        }
        if (bus_overvoltage(sample, absolute_bus_limit_mv, true))
        {
            register_start_failure(ctx, AURORA_START_FAIL_BUS_OVERSHOOT, now_ms);
            break;
        }
        if (sample->pv_voltage_mv < ctx->precharge_pv_min_mv)
        {
            ctx->precharge_pv_min_mv = sample->pv_voltage_mv;
        }
        if (sample->bus_voltage_mv > ctx->precharge_bus_max_mv)
        {
            ctx->precharge_bus_max_mv = sample->bus_voltage_mv;
        }

        if (elapsed_ms(now_ms, ctx->state_since_ms) >= AURORA_PRECHARGE_TIMEOUT_MS)
        {
            register_start_failure(ctx, AURORA_START_FAIL_BUS_PRECHARGE_TIMEOUT, now_ms);
            break;
        }
        if (relay_delta_mv <= AURORA_RELAY_CLOSE_DELTA_MV)
        {
            if (ctx->delta_ok_since_ms == 0U)
            {
                ctx->delta_ok_since_ms = now_ms;
            }
            else if (elapsed_ms(now_ms, ctx->delta_ok_since_ms) >= AURORA_RELAY_DELTA_HOLD_MS)
            {
                ctx->duty_q15 = 0U;
                ctx->power_integral = 0LL;
                ctx->relay_closed = false;
                enter_state(ctx, AURORA_POWER_RELAY_HOLD_OFF, now_ms);
                break;
            }
        }
        else
        {
            ctx->delta_ok_since_ms = 0U;
        }

        requested_power_mw = AURORA_PRECHARGE_POWER_MW;
        ctx->duty_q15 =
            power_to_duty(ctx, sample, requested_power_mw,
                          (uint16_t)(AURORA_DUTY_MAX_Q15 / AURORA_PRECHARGE_DUTY_LIMIT_DIVISOR));
        command.pwm_enable = true;
        break;

    case AURORA_POWER_RELAY_HOLD_OFF:
        ctx->relay_closed = false;
        ctx->duty_q15 = 0U;
        if (sample->pv_voltage_mv < AURORA_PV_START_MIN_MV)
        {
            register_start_failure(ctx, AURORA_START_FAIL_PV_WEAK, now_ms);
            break;
        }
        if (bus_measurement_invalid(sample))
        {
            register_start_failure(ctx, AURORA_START_FAIL_BUS_MEAS_INVALID, now_ms);
            break;
        }
        if (bus_overvoltage(sample, absolute_bus_limit_mv,
                            operating_mode == AURORA_MODE_BATTERY))
        {
            register_start_failure(ctx, AURORA_START_FAIL_BUS_OVERSHOOT, now_ms);
            break;
        }

        // Runtime在物理关PWM后写入基准；至少跨两个发布代次，排除横跨关波边沿的混合块。
        if (!ctx->relay_holdoff_sequence_valid ||
            (elapsed_ms(now_ms, ctx->state_since_ms) < AURORA_RELAY_PWM_OFF_DECAY_MS) ||
            ((uint32_t)(sample->sequence - ctx->relay_holdoff_sequence) <
             AURORA_RELAY_POST_OFF_MIN_BLOCKS))
        {
            break;
        }

        if (operating_mode == AURORA_MODE_BATTERY)
        {
            if (relay_delta_mv <= AURORA_RELAY_CLOSE_DELTA_MV)
            {
                ctx->relay_closed = true;
                enter_state(ctx, AURORA_POWER_RELAY_SETTLE, now_ms);
            }
            else if (elapsed_ms(now_ms, ctx->state_since_ms) >= AURORA_RELAY_HOLDOFF_TIMEOUT_MS)
            {
                // 关波后均压无法保持属于预充失败，禁止PRECHARGE与HOLD_OFF无限循环。
                register_start_failure(ctx, AURORA_START_FAIL_BUS_PRECHARGE_TIMEOUT, now_ms);
            }
        }
        else if (sample->battery_voltage_mv > AURORA_DEMO_EXTERNAL_SOURCE_MAX_MV)
        {
            register_start_failure(ctx, AURORA_START_FAIL_DEMO_EXTERNAL_SOURCE, now_ms);
        }
        else if (sample->bus_voltage_mv <= AURORA_DEMO_RELAY_CLOSE_BUS_MAX_MV)
        {
            ctx->relay_closed = true;
            enter_state(ctx, AURORA_POWER_DEMO_RELAY_SETTLE, now_ms);
        }
        else if (elapsed_ms(now_ms, ctx->state_since_ms) >= AURORA_RELAY_HOLDOFF_TIMEOUT_MS)
        {
            // Demo内部母线残压过高时绝不把高压电容直接接到低压负载。
            register_start_failure(ctx, AURORA_START_FAIL_RELAY_CLOSE_VERIFY, now_ms);
        }
        break;

    case AURORA_POWER_RELAY_SETTLE:
        ctx->relay_closed = true;
        ctx->duty_q15 = 0U;
        if (!relay_applied)
        {
            ctx->delta_ok_since_ms = 0U;
            if (elapsed_ms(now_ms, ctx->state_since_ms) >= AURORA_RELAY_APPLY_TIMEOUT_MS)
            {
                register_start_failure(ctx, AURORA_START_FAIL_RELAY_CLOSE_VERIFY, now_ms);
            }
            break;
        }
        if (ctx->delta_ok_since_ms == 0U)
        {
            ctx->delta_ok_since_ms = now_ms;
        }
        else if (elapsed_ms(now_ms, ctx->delta_ok_since_ms) >= AURORA_RELAY_SETTLE_MS)
        {
            if (relay_delta_mv <= AURORA_RELAY_VERIFY_DELTA_MV)
            {
                /* 2.5V闭合后复核证明预充和Relay阶段均成功，本会话失败计数在此清零。 */
                ctx->precharge_failure_count = 0U;
                ctx->relay_failure_count = 0U;
                ctx->bat_stability_since_ms = now_ms;
                ctx->bat_stability_min_mv = sample->battery_voltage_mv;
                ctx->bat_stability_max_mv = sample->battery_voltage_mv;
                enter_state(ctx, AURORA_POWER_BAT_STABILITY, now_ms);
            }
            else
            {
                register_start_failure(ctx, AURORA_START_FAIL_RELAY_CLOSE_VERIFY, now_ms);
            }
        }
        break;

    case AURORA_POWER_BAT_STABILITY:
        ctx->relay_closed = true;
        ctx->duty_q15 = 0U;
        if (!relay_applied)
        {
            register_start_failure(ctx, AURORA_START_FAIL_RELAY_CLOSE_VERIFY, now_ms);
            break;
        }
        if (sample->battery_voltage_mv < ctx->bat_stability_min_mv)
        {
            ctx->bat_stability_min_mv = sample->battery_voltage_mv;
        }
        if (sample->battery_voltage_mv > ctx->bat_stability_max_mv)
        {
            ctx->bat_stability_max_mv = sample->battery_voltage_mv;
        }
        if (elapsed_ms(now_ms, ctx->bat_stability_since_ms) >= AURORA_BAT_STABILITY_WINDOW_MS)
        {
            if ((ctx->bat_stability_max_mv - ctx->bat_stability_min_mv) <=
                AURORA_BAT_STABILITY_MAX_SPAN_MV)
            {
                ctx->bat_stability_failure_count = 0U;
                ctx->last_failure_reason = AURORA_START_FAIL_NONE;
                enter_state(ctx, AURORA_POWER_RUN, now_ms);
            }
            else
            {
                register_start_failure(ctx, AURORA_START_FAIL_BAT_STABILITY, now_ms);
            }
        }
        break;

    case AURORA_POWER_RUN:
        ctx->relay_closed = true;
        if ((operating_mode != AURORA_MODE_BATTERY) || !relay_applied)
        {
            register_start_failure(ctx, AURORA_START_FAIL_RELAY_CLOSE_VERIFY, now_ms);
            break;
        }
        if (bus_measurement_invalid(sample))
        {
            register_start_failure(ctx, AURORA_START_FAIL_BUS_MEAS_INVALID, now_ms);
            break;
        }
        if (bus_overvoltage(sample, absolute_bus_limit_mv, false))
        {
            register_start_failure(ctx, AURORA_START_FAIL_BUS_OVERSHOOT, now_ms);
            break;
        }
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

        if ((sample->pv_voltage_mv < AURORA_PV_START_MIN_MV) && !mppt->valid)
        {
            if (ctx->no_sun_since_ms == 0U)
            {
                ctx->no_sun_since_ms = now_ms;
            }
            if (elapsed_ms(now_ms, ctx->no_sun_since_ms) >= AURORA_NO_SUN_OPEN_RELAY_MS)
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
            requested_power_mw = min_u32(requested_power_mw, AURORA_RATED_POWER_MW);
            ctx->duty_q15 = power_to_duty(ctx, sample, requested_power_mw, AURORA_DUTY_MAX_Q15);
            command.pwm_enable = requested_power_mw != 0U;
        }
        else
        {
            ctx->duty_q15 = slew_duty(ctx->duty_q15, 0U);
        }
        break;

    case AURORA_POWER_DEMO_OUTPUT_CHECK:
        ctx->relay_closed = false;
        ctx->duty_q15 = 0U;
        if (operating_mode != AURORA_MODE_DEMO_LOAD)
        {
            enter_state(ctx, AURORA_POWER_WAIT_BATTERY, now_ms);
        }
        else if (sample->battery_voltage_mv > AURORA_DEMO_EXTERNAL_SOURCE_MAX_MV)
        {
            register_start_failure(ctx, AURORA_START_FAIL_DEMO_EXTERNAL_SOURCE, now_ms);
        }
        else
        {
            /* 先进入共享关波放能状态；Demo闭合条件不复用Battery均压判据。 */
            enter_state(ctx, AURORA_POWER_RELAY_HOLD_OFF, now_ms);
        }
        break;

    case AURORA_POWER_DEMO_RELAY_SETTLE:
        ctx->relay_closed = true;
        ctx->duty_q15 = 0U;
        if (!relay_applied)
        {
            ctx->delta_ok_since_ms = 0U;
            if (elapsed_ms(now_ms, ctx->state_since_ms) >= AURORA_RELAY_APPLY_TIMEOUT_MS)
            {
                register_start_failure(ctx, AURORA_START_FAIL_RELAY_CLOSE_VERIFY, now_ms);
            }
            break;
        }
        if (ctx->delta_ok_since_ms == 0U)
        {
            ctx->delta_ok_since_ms = now_ms;
        }
        else if (elapsed_ms(now_ms, ctx->delta_ok_since_ms) >= AURORA_RELAY_SETTLE_MS)
        {
            ctx->demo_probe_since_ms = now_ms;
            enter_state(ctx, AURORA_POWER_DEMO_PROBE, now_ms);
        }
        break;

    case AURORA_POWER_DEMO_PROBE:
        ctx->relay_closed = true;
        if ((operating_mode != AURORA_MODE_DEMO_LOAD) || !relay_applied)
        {
            register_start_failure(ctx, AURORA_START_FAIL_RELAY_CLOSE_VERIFY, now_ms);
            break;
        }
        if (bus_measurement_invalid(sample) ||
            bus_overvoltage(sample, absolute_bus_limit_mv, false))
        {
            register_start_failure(ctx,
                                   bus_measurement_invalid(sample)
                                       ? AURORA_START_FAIL_BUS_MEAS_INVALID
                                       : AURORA_START_FAIL_BUS_OVERSHOOT,
                                   now_ms);
            break;
        }
        requested_power_mw = min_u32(AURORA_DEMO_PROBE_POWER_MW, demo_power_limit_mw);
        ctx->duty_q15 =
            power_to_duty(ctx, sample, requested_power_mw,
                          (uint16_t)(AURORA_DUTY_MAX_Q15 / AURORA_PRECHARGE_DUTY_LIMIT_DIVISOR));
        command.pwm_enable = requested_power_mw != 0U;

        if (sample->pv_power_mw >= (int32_t)AURORA_DEMO_LOAD_MIN_POWER_MW)
        {
            if (ctx->delta_ok_since_ms == 0U)
            {
                ctx->delta_ok_since_ms = now_ms;
            }
            else if (elapsed_ms(now_ms, ctx->delta_ok_since_ms) >= AURORA_DEMO_PROBE_HOLD_MS)
            {
                ctx->demo_load_confirmed = true;
                enter_state(ctx, AURORA_POWER_DEMO_RUN, now_ms);
            }
        }
        else
        {
            ctx->delta_ok_since_ms = 0U;
        }
        if (elapsed_ms(now_ms, ctx->state_since_ms) >= AURORA_DEMO_PROBE_TIMEOUT_MS)
        {
            register_start_failure(ctx, AURORA_START_FAIL_DEMO_NO_LOAD, now_ms);
        }
        break;

    case AURORA_POWER_DEMO_RUN:
        ctx->relay_closed = true;
        if ((operating_mode != AURORA_MODE_DEMO_LOAD) || !relay_applied)
        {
            register_start_failure(ctx, AURORA_START_FAIL_RELAY_CLOSE_VERIFY, now_ms);
            break;
        }
        if (bus_measurement_invalid(sample) ||
            bus_overvoltage(sample, absolute_bus_limit_mv, false))
        {
            register_start_failure(ctx,
                                   bus_measurement_invalid(sample)
                                       ? AURORA_START_FAIL_BUS_MEAS_INVALID
                                       : AURORA_START_FAIL_BUS_OVERSHOOT,
                                   now_ms);
            break;
        }
        if (sample->pv_current_ma > AURORA_PV_CURRENT_LIMIT_MA)
        {
            if (ctx->demo_probe_since_ms == 0U)
            {
                ctx->demo_probe_since_ms = now_ms;
            }
            else if (elapsed_ms(now_ms, ctx->demo_probe_since_ms) >= AURORA_DEMO_OVERLOAD_HOLD_MS)
            {
                register_start_failure(ctx, AURORA_START_FAIL_DEMO_OVERLOAD, now_ms);
                break;
            }
        }
        else
        {
            ctx->demo_probe_since_ms = 0U;
        }

        requested_power_mw =
            demo_power_target_mw(sample, demo_target_voltage_mv, demo_power_limit_mw);
        ctx->duty_q15 = power_to_duty(ctx, sample, requested_power_mw, AURORA_DUTY_MAX_Q15);
        command.pwm_enable = requested_power_mw != 0U;

        if (((uint32_t)sample->battery_voltage_mv + 500U >= demo_target_voltage_mv) &&
            (sample->pv_power_mw < (int32_t)AURORA_DEMO_LOAD_MIN_POWER_MW))
        {
            if (ctx->demo_no_load_since_ms == 0U)
            {
                ctx->demo_no_load_since_ms = now_ms;
            }
            else if (elapsed_ms(now_ms, ctx->demo_no_load_since_ms) >= AURORA_DEMO_NO_LOAD_HOLD_MS)
            {
                register_start_failure(ctx, AURORA_START_FAIL_DEMO_NO_LOAD, now_ms);
            }
        }
        else
        {
            ctx->demo_no_load_since_ms = 0U;
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
            if (protection_safe && !ctx->startup_locked)
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

/*---------------------------------------------------------------------------*
 * Name        : aurora_power_command_t aurora_power_stage_step(
 *               aurora_power_stage_ctx_t *ctx, const aurora_measurement_t *sample,
 *               const aurora_mppt_output_t *mppt, const aurora_charge_output_t *charger,
 *               bool protection_safe, bool zero_cal_ready, bool zero_cal_failed,
 *               uint32_t now_ms)
 * Input       : 与历史Battery模式接口一致
 * Output      : PWM、Relay、Duty命令和功率级状态
 * Description : 保留旧调用方和Host回归兼容；直接单测视为Relay执行已确认；生产路径由Runtime提供真实bool反馈。
 *---------------------------------------------------------------------------*/
aurora_power_command_t aurora_power_stage_step(aurora_power_stage_ctx_t *ctx,
                                               const aurora_measurement_t *sample,
                                               const aurora_mppt_output_t *mppt,
                                               const aurora_charge_output_t *charger,
                                               bool protection_safe, bool zero_cal_ready,
                                               bool zero_cal_failed, uint32_t now_ms)
{
    return aurora_power_stage_step_ex(
        ctx, sample, mppt, charger, protection_safe, zero_cal_ready, zero_cal_failed, true,
        AURORA_MODE_BATTERY, AURORA_DEMO_TARGET_VOLTAGE_MV,
        AURORA_DEMO_POWER_LIMIT_MW, now_ms);
}
