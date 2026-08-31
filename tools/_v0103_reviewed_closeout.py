#!/usr/bin/env python3
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    (ROOT / path).write_text(text, encoding="utf-8", newline="\n")


def replace_once(path: str, old: str, new: str) -> None:
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected 1 literal match, got {count}: {old[:80]!r}")
    write(path, text.replace(old, new, 1))


def regex_once(path: str, pattern: str, replacement: str) -> None:
    text = read(path)
    new_text, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
    if count != 1:
        raise SystemExit(f"{path}: expected 1 regex match, got {count}: {pattern[:100]!r}")
    write(path, new_text)


# 1) Host-only power-gate override for the dedicated v0.10.3 behavior target.
replace_once(
    "CMakeLists.txt",
    """target_compile_definitions(aurora_v0103_tests PRIVATE
    AURORA_HOST_TEST=1
)""",
    """target_compile_definitions(aurora_v0103_tests PRIVATE
    AURORA_HOST_TEST=1
    AURORA_HOST_TEST_POWER_GATES_OPEN=1
)""",
)

# 2) Constants for strict post-off evidence, bounded holdoff and Demo low-risk close.
replace_once(
    "app/inc/app_config.h",
    """#define AURORA_RELAY_PWM_OFF_DECAY_MS               (20U)
/* Runtime未在100ms内落实Relay GPIO，按闭合验证失败处理。 */
#define AURORA_RELAY_APPLY_TIMEOUT_MS               (100U)""",
    """#define AURORA_RELAY_PWM_OFF_DECAY_MS               (20U)
// 至少跨过两个完整DMA发布代次，避免接受横跨关PWM边沿的混合采样块。
#define AURORA_RELAY_POST_OFF_MIN_BLOCKS            (2U)
// 关波后500ms仍不能建立安全闭合条件，本次启动按有限重试失败处理。
#define AURORA_RELAY_HOLDOFF_TIMEOUT_MS             (500U)
/* Runtime未在100ms内落实Relay GPIO，按闭合验证失败处理。 */
#define AURORA_RELAY_APPLY_TIMEOUT_MS               (100U)""",
)
replace_once(
    "app/inc/app_config.h",
    "#define AURORA_PRECHARGE_TIMEOUT_MS                 (30000U)",
    """#define AURORA_PRECHARGE_TIMEOUT_MS                 (30000U)
// 低功率必须同时伴随明显PV压降并持续2s，才归类为真实弱光。
#define AURORA_PRECHARGE_WEAK_POWER_MW              (3000U)
#define AURORA_PRECHARGE_WEAK_PV_DROOP_MV           (1000L)
#define AURORA_PRECHARGE_WEAK_HOLD_MS               (2000U)""",
)
replace_once(
    "app/inc/app_config.h",
    "#define AURORA_DEMO_EXTERNAL_SOURCE_MAX_MV          (5000L)",
    """#define AURORA_DEMO_EXTERNAL_SOURCE_MAX_MV          (5000L)
// Demo机械合闸前BST_U也必须处于低残压区，运行目标电压不能作为合闸许可。
#define AURORA_DEMO_RELAY_CLOSE_BUS_MAX_MV          (5000L)""",
)

# 3) Relay generation travels with the hardware-independent power command.
replace_once(
    "app/inc/app_types.h",
    """typedef struct
{
    uint16_t duty_q15;                               /* 低侧MOS物理占空比，Q15。 */
    aurora_power_state_t state;                      /* 命令对应的功率级状态。 */
    bool pwm_enable;                                 /* true表示请求发波。 */
    bool relay_enable;                               /* true表示请求闭合继电器。 */
    uint8_t state_reserved[3];                       /* 显式补齐功率状态字段。 */
} aurora_power_command_t;""",
    """typedef struct
{
    uint32_t relay_generation;                       // Relay闭合事务代次；0表示当前没有有效闭合事务。
    uint16_t duty_q15;                               /* 低侧MOS物理占空比，Q15。 */
    aurora_power_state_t state;                      /* 命令对应的功率级状态。 */
    bool pwm_enable;                                 /* true表示请求发波。 */
    bool relay_enable;                               /* true表示请求闭合继电器。 */
    uint8_t state_reserved[3];                       /* 显式补齐功率状态字段。 */
} aurora_power_command_t;""",
)

# 4) App/runtime state required for generation feedback, physical-off capture and ADC timebase.
replace_once(
    "app/inc/main.h",
    """    uint32_t last_energy_history_ms;                 /* 上一次能量持久化请求时间；30min相位保存在Flash v3。 */
    uint32_t telemetry_message_id;                   /* 主动遥测消息序号。 */""",
    """    uint32_t last_energy_history_ms;                 /* 上一次能量持久化请求时间；30min相位保存在Flash v3。 */
    uint32_t last_energy_sample_sequence;            // 上次参与能量时间轴的ADC发布序号。
    uint32_t last_energy_sample_timestamp_ms;        // 上次参与能量时间轴的ADC时间戳。
    uint32_t relay_applied_generation_feedback;      // Runtime最近一次实际写出Relay GPIO对应的事务代次。
    uint32_t telemetry_message_id;                   /* 主动遥测消息序号。 */""",
)
replace_once(
    "app/inc/main.h",
    """    uint32_t fast_ocp_recover_since_ms;              /* 快速OCP硬件源消失后的恢复计时。 */
    volatile uint32_t adc_overrun_count;""",
    """    uint32_t fast_ocp_recover_since_ms;              /* 快速OCP硬件源消失后的恢复计时。 */
    uint32_t relay_applied_generation;               // 当前物理Relay GPIO状态对应的闭合事务代次。
    volatile uint32_t adc_overrun_count;""",
)
replace_once(
    "app/inc/main.h",
    """    bool relay_applied;                              /* 物理继电器最近实际状态。 */
    bool initialized;                                /* 完整运行初始化完成。 */
    uint8_t layout_reserved[2];""",
    """    bool relay_applied;                              /* 物理继电器最近实际状态。 */
    bool relay_holdoff_baseline_captured;            // Runtime已在物理关PWM后记录ADC基准序号。
    bool initialized;                                /* 完整运行初始化完成。 */
    uint8_t layout_reserved[1];""",
)

replace_once(
    "app/inc/power_stage.h",
    """    uint32_t dynamic_start_delay_ms;                 /* >15V启动的自适应1~10s延时。 */
    uint32_t relay_holdoff_sequence;                 /* 进入20ms关波放能窗口时的ADC快照序号。 */
    int32_t bat_stability_min_mv;""",
    """    uint32_t dynamic_start_delay_ms;                 /* >15V启动的自适应1~10s延时。 */
    uint32_t relay_generation;                       // 当前Relay闭合事务代次，0保留为无事务。
    uint32_t relay_holdoff_sequence;                 // Runtime确认物理关PWM时记录的ADC发布序号。
    uint32_t precharge_low_power_since_ms;           // 低功率且PV明显压降的连续起点。
    int32_t precharge_pv_entry_mv;                   // 本轮PRECHARGE入口PV电压，mV。
    int32_t precharge_pv_min_mv;                     // 本轮PRECHARGE最低PV电压，mV。
    int32_t precharge_bus_start_mv;                  // 本轮PRECHARGE入口BST_U，mV。
    int32_t precharge_bus_max_mv;                    // 本轮PRECHARGE最高BST_U，mV。
    int32_t bat_stability_min_mv;""",
)
replace_once(
    "app/inc/power_stage.h",
    """                                                  bool zero_cal_failed,
                                                  bool relay_applied,
                                                  aurora_operating_mode_t operating_mode,""",
    """                                                  bool zero_cal_failed,
                                                  bool relay_applied,
                                                  uint32_t relay_applied_generation,
                                                  aurora_operating_mode_t operating_mode,""",
)
replace_once(
    "app/inc/power_stage.h",
    "uint32_t now_ms); // 支持Battery/Demo和Relay执行反馈的扩展入口",
    "uint32_t now_ms); // 支持Battery/Demo和Relay代次反馈的扩展入口",
)

# 5) Host-only board gate override. Production macros remain zero and production code remains fail-closed.
regex_once(
    "driver/src/drv_board.c",
    r"bool drv_board_power_gate_open\(void\)\n\{.*?\n\}\n\n/\*---------------------------------------------------------------------------\*\n \* Name        : bool drv_board_demo_load_gate_open\(void\).*?bool drv_board_demo_load_gate_open\(void\)\n\{.*?\n\}",
    """bool drv_board_power_gate_open(void)
{
#if defined(AURORA_HOST_TEST_POWER_GATES_OPEN)
    // 仅v0.10.3端到端Host目标使用；生产目标仍完全由BOARD_GATE_*与总门控制。
    return true;
#else
    return (BOARD_POWER_OUTPUT_ALLOWED != 0U) && (BOARD_GATE_PINMAP_REVIEWED != 0U) &&
           (BOARD_GATE_COMP_ROUTE_VALIDATED != 0U) && (BOARD_GATE_ANALOG_CALIBRATED != 0U) &&
           (BOARD_GATE_KEIL_LINKED != 0U) && (BOARD_GATE_LOW_VOLTAGE_BENCH != 0U);
#endif
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_board_demo_load_gate_open(void)
 * Input       : 无
 * Output      : true表示Demo负载/空载/短路/外部电源台架已人工验收
 * Description : 仅供Demo模式PWM和Relay闭合复核；Host覆盖只允许存在于专用测试目标。
 *---------------------------------------------------------------------------*/
bool drv_board_demo_load_gate_open(void)
{
#if defined(AURORA_HOST_TEST_POWER_GATES_OPEN)
    return true;
#else
    return (BOARD_GATE_DEMO_LOAD_VALIDATED != 0U);
#endif
}""",
)

# 6) PowerStage: generation helper and signature.
replace_once(
    "app/src/power_stage.c",
    """static uint32_t min_u32(uint32_t a, uint32_t b)
{
    return (a < b) ? a : b;
}
""",
    """static uint32_t min_u32(uint32_t a, uint32_t b)
{
    return (a < b) ? a : b;
}

/*---------------------------------------------------------------------------*
 * Name        : static uint32_t next_relay_generation(uint32_t current)
 * Input       : current - 当前事务代次
 * Output      : 下一非0事务代次
 * Description : Relay反馈必须绑定当前闭合事务；自然回绕到0时跳过0避免无事务歧义。
 *---------------------------------------------------------------------------*/
static uint32_t next_relay_generation(uint32_t current)
{
    current++;
    return (current == 0U) ? 1U : current;
}
""",
)
replace_once(
    "app/src/power_stage.c",
    " *               bool relay_applied, aurora_operating_mode_t operating_mode,",
    " *               bool relay_applied, uint32_t relay_applied_generation,\n *               aurora_operating_mode_t operating_mode,",
)
replace_once(
    "app/src/power_stage.c",
    """ *               zero_cal_ready/failed - PV_I运行时零点状态；relay_applied - Runtime已写Relay GPIO；
 *               operating_mode - Battery/Demo；Demo限制；now_ms - 当前毫秒""",
    """ *               zero_cal_ready/failed - PV_I运行时零点状态；relay_applied - Runtime已写Relay GPIO；
 *               relay_applied_generation - 该GPIO状态对应的事务代次；operating_mode - Battery/Demo；
 *               Demo限制；now_ms - 当前毫秒""",
)
replace_once(
    "app/src/power_stage.c",
    """                           bool protection_safe, bool zero_cal_ready, bool zero_cal_failed,
                           bool relay_applied, aurora_operating_mode_t operating_mode,""",
    """                           bool protection_safe, bool zero_cal_ready, bool zero_cal_failed,
                           bool relay_applied, uint32_t relay_applied_generation,
                           aurora_operating_mode_t operating_mode,""",
)

# PRECHARGE entry evidence.
replace_once(
    "app/src/power_stage.c",
    """        else if (sample->battery_voltage_mv > AURORA_BATTERY_DETECT_MIN_MV)
        {
            enter_state(ctx, AURORA_POWER_PRECHARGE, now_ms);
        }""",
    """        else if (sample->battery_voltage_mv > AURORA_BATTERY_DETECT_MIN_MV)
        {
            ctx->precharge_pv_entry_mv = sample->pv_voltage_mv;
            ctx->precharge_pv_min_mv = sample->pv_voltage_mv;
            ctx->precharge_bus_start_mv = sample->bus_voltage_mv;
            ctx->precharge_bus_max_mv = sample->bus_voltage_mv;
            ctx->precharge_low_power_since_ms = 0U;
            enter_state(ctx, AURORA_POWER_PRECHARGE, now_ms);
        }""",
)

# Weak source must show actual PV droop, otherwise high Voc/no BUS progress remains a hardware-path timeout.
replace_once(
    "app/src/power_stage.c",
    """        if (elapsed_ms(now_ms, ctx->state_since_ms) >= AURORA_PRECHARGE_TIMEOUT_MS)
        {
            /* 已进入PRECHARGE且PV电压仍合格，低Ppv也不能掩盖Boost功率路径失效。 */
            register_start_failure(ctx, AURORA_START_FAIL_BUS_PRECHARGE_TIMEOUT, now_ms);
            break;
        }""",
    """        if (sample->pv_voltage_mv < ctx->precharge_pv_min_mv)
        {
            ctx->precharge_pv_min_mv = sample->pv_voltage_mv;
        }
        if (sample->bus_voltage_mv > ctx->precharge_bus_max_mv)
        {
            ctx->precharge_bus_max_mv = sample->bus_voltage_mv;
        }

        // 低Ppv本身不能证明弱光；还必须相对PRECHARGE入口出现明显PV压降并持续成立。
        if ((sample->pv_power_mw >= 0) &&
            ((uint32_t)sample->pv_power_mw < AURORA_PRECHARGE_WEAK_POWER_MW) &&
            (ctx->precharge_pv_entry_mv > sample->pv_voltage_mv) &&
            ((ctx->precharge_pv_entry_mv - sample->pv_voltage_mv) >=
             AURORA_PRECHARGE_WEAK_PV_DROOP_MV))
        {
            if (ctx->precharge_low_power_since_ms == 0U)
            {
                ctx->precharge_low_power_since_ms = now_ms;
            }
            else if (elapsed_ms(now_ms, ctx->precharge_low_power_since_ms) >=
                     AURORA_PRECHARGE_WEAK_HOLD_MS)
            {
                register_start_failure(ctx, AURORA_START_FAIL_PV_WEAK, now_ms);
                break;
            }
        }
        else
        {
            ctx->precharge_low_power_since_ms = 0U;
        }

        if (elapsed_ms(now_ms, ctx->state_since_ms) >= AURORA_PRECHARGE_TIMEOUT_MS)
        {
            register_start_failure(ctx, AURORA_START_FAIL_BUS_PRECHARGE_TIMEOUT, now_ms);
            break;
        }""",
)

# Runtime owns physical-off baseline, so PowerStage enters HOLD_OFF with sequence=0.
text = read("app/src/power_stage.c")
text = text.replace("ctx->relay_holdoff_sequence = sample->sequence;", "ctx->relay_holdoff_sequence = 0U;")
if text.count("ctx->relay_holdoff_sequence = 0U;") < 2:
    raise SystemExit("power_stage.c: expected Battery and Demo HOLD_OFF entries")
write("app/src/power_stage.c", text)

# Strict HOLD_OFF with two newer blocks, bounded failure, Demo low residual voltage and generation issuance.
regex_once(
    "app/src/power_stage.c",
    r"    case AURORA_POWER_RELAY_HOLD_OFF:\n.*?\n        break;\n\n    case AURORA_POWER_RELAY_SETTLE:",
    """    case AURORA_POWER_RELAY_HOLD_OFF:
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
        if ((ctx->relay_holdoff_sequence == 0U) ||
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
                ctx->relay_generation = next_relay_generation(ctx->relay_generation);
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
            ctx->relay_generation = next_relay_generation(ctx->relay_generation);
            ctx->relay_closed = true;
            enter_state(ctx, AURORA_POWER_DEMO_RELAY_SETTLE, now_ms);
        }
        else if (elapsed_ms(now_ms, ctx->state_since_ms) >= AURORA_RELAY_HOLDOFF_TIMEOUT_MS)
        {
            // Demo内部母线残压过高时绝不把高压电容直接接到低压负载。
            register_start_failure(ctx, AURORA_START_FAIL_RELAY_CLOSE_VERIFY, now_ms);
        }
        break;

    case AURORA_POWER_RELAY_SETTLE:""",
)

# All Relay-dependent states require the matching generation, not a stale bool.
text = read("app/src/power_stage.c")
text = text.replace(
    "if (!relay_applied)\n        {\n            ctx->delta_ok_since_ms = 0U;",
    "if (!relay_applied || (relay_applied_generation != ctx->relay_generation))\n        {\n            ctx->delta_ok_since_ms = 0U;",
)
text = text.replace(
    "if (!relay_applied)\n        {\n            register_start_failure(ctx, AURORA_START_FAIL_RELAY_CLOSE_VERIFY, now_ms);",
    "if (!relay_applied || (relay_applied_generation != ctx->relay_generation))\n        {\n            register_start_failure(ctx, AURORA_START_FAIL_RELAY_CLOSE_VERIFY, now_ms);",
)
text = text.replace(
    "if ((operating_mode != AURORA_MODE_BATTERY) || !relay_applied)",
    "if ((operating_mode != AURORA_MODE_BATTERY) || !relay_applied ||\n            (relay_applied_generation != ctx->relay_generation))",
)
text = text.replace(
    "if ((operating_mode != AURORA_MODE_DEMO_LOAD) || !relay_applied)",
    "if ((operating_mode != AURORA_MODE_DEMO_LOAD) || !relay_applied ||\n            (relay_applied_generation != ctx->relay_generation))",
)
write("app/src/power_stage.c", text)

replace_once(
    "app/src/power_stage.c",
    """    command.duty_q15 = command.pwm_enable ? ctx->duty_q15 : 0U;
    command.relay_enable = ctx->relay_closed;
    command.state = ctx->state;""",
    """    command.relay_generation = ctx->relay_generation;
    command.duty_q15 = command.pwm_enable ? ctx->duty_q15 : 0U;
    command.relay_enable = ctx->relay_closed;
    command.state = ctx->state;""",
)
replace_once(
    "app/src/power_stage.c",
    """        ctx, sample, mppt, charger, protection_safe, zero_cal_ready, zero_cal_failed, true,
        AURORA_MODE_BATTERY, AURORA_DEMO_TARGET_VOLTAGE_MV, AURORA_DEMO_POWER_LIMIT_MW, now_ms);""",
    """        ctx, sample, mppt, charger, protection_safe, zero_cal_ready, zero_cal_failed, true,
        ctx->relay_generation, AURORA_MODE_BATTERY, AURORA_DEMO_TARGET_VOLTAGE_MV,
        AURORA_DEMO_POWER_LIMIT_MW, now_ms);""",
)

# 7) main.c: stricter energy, Relay gates, physical-off capture, generation and pending-fault Break guard.
replace_once(
    "app/src/main.c",
    """#define RUNTIME_ADC_ENERGY_FAULT_MASK                                                              \\
    (AURORA_FAULT_ADC_STALE | AURORA_FAULT_ADC_DMA | AURORA_FAULT_ADC_OVERRUN)""",
    """#define RUNTIME_ADC_ENERGY_FAULT_MASK                                                              \\
    (AURORA_FAULT_ADC_STALE | AURORA_FAULT_ADC_DMA | AURORA_FAULT_ADC_OVERRUN |                    \\
     AURORA_FAULT_PV_CURRENT_PLAUSIBILITY)""",
)
regex_once(
    "app/src/main.c",
    r"static bool pv_energy_sample_qualified\(const aurora_app_t \*app, uint32_t now_ms,\n                                       bool boost_output_active\)\n\{.*?\n\}",
    """static bool pv_energy_sample_qualified(const aurora_app_t *app, uint32_t now_ms,
                                       bool boost_output_active)
{
    const uint32_t faults = aurora_protection_fault_mask(&app->protection);
    const aurora_measurement_t *sample = &app->sample;
    const aurora_power_state_t state = app->power_stage.state;
    const bool state_allows_energy =
        (state == AURORA_POWER_PRECHARGE) || (state == AURORA_POWER_RUN) ||
        (state == AURORA_POWER_DEMO_PROBE) || (state == AURORA_POWER_DEMO_RUN);

    return boost_output_active && state_allows_energy && aurora_protection_is_safe(&app->protection) &&
           (sample->sequence != 0U) && ((sample->valid_mask & AURORA_MEAS_VALID_PV_POWER) != 0U) &&
           ((now_ms - sample->timestamp_ms) <= AURORA_MEASUREMENT_STALE_MS) &&
           ((faults & RUNTIME_ADC_ENERGY_FAULT_MASK) == 0U) && (sample->pv_power_mw > 0);
}""",
)
regex_once(
    "app/src/main.c",
    r"static bool relay_close_still_safe\(const aurora_runtime_t \*runtime\)\n\{.*?\n\}",
    """static bool relay_close_still_safe(const aurora_runtime_t *runtime)
{
    const aurora_measurement_t *sample = &runtime->app.sample;
    const aurora_operating_mode_t mode = runtime->app.storage.settings.operating_mode;
    const aurora_power_state_t state = runtime->app.power_command.state;
    const bool battery_request =
        (mode == AURORA_MODE_BATTERY) && (state == AURORA_POWER_RELAY_SETTLE);
    const bool demo_request =
        (mode == AURORA_MODE_DEMO_LOAD) && (state == AURORA_POWER_DEMO_RELAY_SETTLE);
    const uint32_t required = AURORA_MEAS_VALID_BAT_V | AURORA_MEAS_VALID_BUS_V;
    const uint32_t now_ms = drv_time_now_ms();
    int64_t delta_mv;

    if ((!battery_request && !demo_request) || runtime->app.power_command.pwm_enable ||
        (runtime->pending_fault_mask != 0U) || !aurora_protection_is_safe(&runtime->app.protection) ||
        drv_pwm_output_active() || !drv_board_power_gate_open() ||
        (demo_request && !drv_board_demo_load_gate_open()) ||
        ((sample->valid_mask & required) != required) || (sample->sequence == 0U) ||
        ((now_ms - sample->timestamp_ms) > AURORA_MEASUREMENT_STALE_MS) ||
        ((sample->diagnostic_mask & AURORA_MEAS_DIAG_BUS_ADC_SATURATED) != 0U))
    {
        return false;
    }

    if (battery_request)
    {
        delta_mv = (int64_t)sample->bus_voltage_mv - sample->battery_voltage_mv;
        if (delta_mv < 0LL)
        {
            delta_mv = -delta_mv;
        }
        return delta_mv <= AURORA_RELAY_CLOSE_DELTA_MV;
    }

    return (sample->battery_voltage_mv >= 0) &&
           (sample->battery_voltage_mv <= AURORA_DEMO_EXTERNAL_SOURCE_MAX_MV) &&
           (sample->bus_voltage_mv >= 0) &&
           (sample->bus_voltage_mv <= AURORA_DEMO_RELAY_CLOSE_BUS_MAX_MV);
}""",
)
replace_once(
    "app/src/main.c",
    """    if ((runtime->pwm_arm_state != AURORA_RUNTIME_PWM_ARM_ACTIVE) &&
        !drv_pwm_output_active() && drv_pwm_break_latched() && !drv_pwm_break_source_active() &&
        ((faults & RUNTIME_FAST_OCP_MASK) == 0U))""",
    """    if ((runtime->pwm_arm_state != AURORA_RUNTIME_PWM_ARM_ACTIVE) &&
        (runtime->pending_fault_mask == 0U) && !drv_pwm_output_active() &&
        drv_pwm_break_latched() && !drv_pwm_break_source_active() &&
        ((faults & RUNTIME_FAST_OCP_MASK) == 0U))""",
)

regex_once(
    "app/src/main.c",
    r"static void apply_power_command\(aurora_runtime_t \*runtime\)\n\{.*?\n\}\n\n/\*---------------------------------------------------------------------------\*\n \* Name        : static void process_adc",
    """static void apply_power_command(aurora_runtime_t *runtime)
{
    const aurora_power_command_t *command = &runtime->app.power_command;
    uint32_t token;
    aurora_irq_state_t irq;
    bool arm_ok;

    // HOLD_OFF基准由Runtime在物理关PWM之后记录，PowerStage不得提前使用关波前样本。
    if (command->state == AURORA_POWER_RELAY_HOLD_OFF)
    {
        force_safe_off(runtime);
        if (runtime->relay_applied)
        {
            drv_io_set_relay(false);
            runtime->relay_applied = false;
            runtime->relay_applied_generation = 0U;
        }
        if (!runtime->relay_holdoff_baseline_captured)
        {
            runtime->app.power_stage.relay_holdoff_sequence = runtime->app.sample.sequence;
            runtime->app.power_stage.state_since_ms = drv_time_now_ms();
            runtime->relay_holdoff_baseline_captured = true;
        }
        return;
    }
    runtime->relay_holdoff_baseline_captured = false;

    // 当前物理Relay属于旧事务时先断开，禁止旧true反馈授权新请求。
    if (command->relay_enable && runtime->relay_applied &&
        (command->relay_generation != runtime->relay_applied_generation))
    {
        force_safe_off(runtime);
        drv_io_set_relay(false);
        runtime->relay_applied = false;
        runtime->relay_applied_generation = 0U;
        return;
    }

    if (command->relay_enable != runtime->relay_applied)
    {
        force_safe_off(runtime);
        if (command->relay_enable && !relay_close_still_safe(runtime))
        {
            drv_io_set_relay(false);
            runtime->relay_applied = false;
            runtime->relay_applied_generation = 0U;
            return;
        }
        drv_io_set_relay(command->relay_enable);
        runtime->relay_applied = command->relay_enable;
        runtime->relay_applied_generation = command->relay_enable ? command->relay_generation : 0U;
        return;
    }

    if (!command->pwm_enable || !aurora_protection_is_safe(&runtime->app.protection))
    {
        force_safe_off(runtime);
        return;
    }

    clear_startup_break_if_safe(runtime);
    if (runtime->pwm_arm_state == AURORA_RUNTIME_PWM_ARM_OFF)
    {
        drv_pwm_disarm();
        if (drv_pwm_prepare_arm_zero(&runtime->pwm_zero_sequence))
        {
            runtime->pwm_arm_state = AURORA_RUNTIME_PWM_ARM_WAIT_ZERO;
        }
        return;
    }

    if (runtime->pwm_arm_state == AURORA_RUNTIME_PWM_ARM_WAIT_ZERO)
    {
        if (drv_pwm_applied_sequence() < runtime->pwm_zero_sequence)
        {
            return;
        }
        token = runtime->safety_epoch;
        irq = drv_irq_save();
        if (!safety_still_clear(runtime, token))
        {
            drv_irq_restore(irq);
            force_safe_off(runtime);
            return;
        }
        runtime->pwm_arm_state = AURORA_RUNTIME_PWM_ARM_ACTIVE;
        arm_ok = drv_pwm_arm();
        drv_irq_restore(irq);
        if (!arm_ok)
        {
            if (runtime->pwm_arm_state == AURORA_RUNTIME_PWM_ARM_ACTIVE)
            {
                aurora_runtime_isr_comparator_fault(runtime, AURORA_FAULT_FAST_BREAK);
            }
            force_safe_off(runtime);
            return;
        }
        if (!safety_still_clear(runtime, token) || !drv_pwm_output_active())
        {
            if ((runtime->pwm_arm_state == AURORA_RUNTIME_PWM_ARM_ACTIVE) &&
                (drv_pwm_break_source_active() || drv_pwm_break_latched()))
            {
                aurora_runtime_isr_comparator_fault(runtime, AURORA_FAULT_FAST_BREAK);
            }
            force_safe_off(runtime);
            return;
        }
        return;
    }

    token = runtime->safety_epoch;
    if (!safety_still_clear(runtime, token))
    {
        force_safe_off(runtime);
        return;
    }
    if (!drv_pwm_stage_duty(command->duty_q15, NULL) || !safety_still_clear(runtime, token))
    {
        force_safe_off(runtime);
    }
}

/*---------------------------------------------------------------------------*
 * Name        : static void process_adc""",
)

# App init/settings/reset: reset hardware feedback and energy timebase.
replace_once(
    "app/src/main.c",
    """    app->last_energy_history_ms = now_ms;
    app->relay_applied_feedback = false;""",
    """    app->last_energy_history_ms = now_ms;
    app->last_energy_sample_sequence = 0U;
    app->last_energy_sample_timestamp_ms = 0U;
    app->relay_applied_generation_feedback = 0U;
    app->relay_applied_feedback = false;""",
)
replace_once(
    "app/src/main.c",
    """    app->actual_power_transfer = false;
    app->relay_applied_feedback = false;""",
    """    app->actual_power_transfer = false;
    app->last_energy_sample_sequence = 0U;
    app->last_energy_sample_timestamp_ms = 0U;
    app->relay_applied_generation_feedback = 0U;
    app->relay_applied_feedback = false;""",
)
replace_once(
    "app/src/main.c",
    """        app->energy_accumulator_mw_ms = 0U;
        app->charge_energy_accumulator_mw_ms = 0U;
        app->last_energy_history_ms = now_ms;""",
    """        app->energy_accumulator_mw_ms = 0U;
        app->charge_energy_accumulator_mw_ms = 0U;
        app->last_energy_sample_sequence = 0U;
        app->last_energy_sample_timestamp_ms = 0U;
        app->last_energy_history_ms = now_ms;""",
)

# App step uses ADC timestamp deltas and current Protection result before accounting.
replace_once(
    "app/src/main.c",
    """    uint32_t elapsed_step_ms;
    aurora_power_state_t previous_power_state;""",
    """    uint32_t elapsed_step_ms;
    uint32_t sample_elapsed_ms;
    aurora_power_state_t previous_power_state;""",
)
regex_once(
    "app/src/main.c",
    r"    \(void\)aurora_measurement_read\(&app->measurement, &app->sample\);\n.*?    app->storage.settings.charge_est_energy_remainder_mw_ms = app->charge_energy_accumulator_mw_ms;\n",
    """    (void)aurora_measurement_read(&app->measurement, &app->sample);

    // 先更新电池估算和Protection，再决定本轮时间/能量是否可信。
    aurora_measurement_estimate_battery_current(
        &app->sample, estimate_efficiency_q15(&app->sample), app->relay_applied_feedback,
        (app->power_stage.state == AURORA_POWER_RELAY_SETTLE) ||
            (app->power_stage.state == AURORA_POWER_BAT_STABILITY));
    aurora_protection_step_ex(
        &app->protection, &app->sample, &app->charger.profile, app->storage.settings.operating_mode,
        aurora_measurement_zero_cal_ready(&app->measurement), boost_output_active, now_ms);

    sample_elapsed_ms = 0U;
    if ((app->sample.sequence != 0U) &&
        (app->sample.sequence != app->last_energy_sample_sequence))
    {
        if (app->last_energy_sample_sequence != 0U)
        {
            const uint32_t measured_delta_ms =
                app->sample.timestamp_ms - app->last_energy_sample_timestamp_ms;
            if (measured_delta_ms <= AURORA_MEASUREMENT_STALE_MS)
            {
                sample_elapsed_ms = measured_delta_ms;
            }
        }
        app->last_energy_sample_sequence = app->sample.sequence;
        app->last_energy_sample_timestamp_ms = app->sample.timestamp_ms;
    }

    pv_energy_qualified = pv_energy_sample_qualified(app, now_ms, boost_output_active);
    app->actual_power_transfer =
        (app->storage.settings.operating_mode == AURORA_MODE_BATTERY) &&
        (app->power_stage.state == AURORA_POWER_RUN) && app->relay_applied_feedback &&
        (app->relay_applied_generation_feedback == app->power_command.relay_generation) &&
        boost_output_active && app->charge_output.allow_charge && app->power_command.pwm_enable &&
        pv_energy_qualified &&
        (app->sample.pv_power_mw >= (int32_t)AURORA_ACTUAL_TRANSFER_MIN_POWER_MW);
    aurora_charger_account_active_time(&app->charger, app->actual_power_transfer, sample_elapsed_ms);

    if (pv_energy_qualified && (sample_elapsed_ms != 0U))
    {
        app->energy_accumulator_mw_ms +=
            (uint64_t)(uint32_t)app->sample.pv_power_mw * sample_elapsed_ms;
        while (app->energy_accumulator_mw_ms >= AURORA_ONE_WH_MW_MS)
        {
            app->energy_accumulator_mw_ms -= AURORA_ONE_WH_MW_MS;
            app->storage.settings.lifetime_energy_wh++;
            aurora_storage_energy_history_update(&app->storage.settings);
            aurora_storage_mark_dirty(&app->storage, now_ms);
        }
    }

    if (app->actual_power_transfer && (sample_elapsed_ms != 0U))
    {
        const uint64_t charge_power_mw =
            ((uint64_t)(uint32_t)app->sample.pv_power_mw * estimate_efficiency_q15(&app->sample)) /
            AURORA_DUTY_Q15_ONE;
        app->charge_energy_accumulator_mw_ms += charge_power_mw * sample_elapsed_ms;
        while (app->charge_energy_accumulator_mw_ms >= AURORA_ONE_WH_MW_MS)
        {
            app->charge_energy_accumulator_mw_ms -= AURORA_ONE_WH_MW_MS;
            app->storage.settings.charge_est_lifetime_energy_wh++;
            aurora_storage_energy_history_update(&app->storage.settings);
            aurora_storage_mark_dirty(&app->storage, now_ms);
        }
    }
    app->storage.settings.pv_energy_remainder_mw_ms = app->energy_accumulator_mw_ms;
    app->storage.settings.charge_est_energy_remainder_mw_ms = app->charge_energy_accumulator_mw_ms;
""",
)
# Remove old duplicate estimate/protection block later in the function.
old_dup = """    aurora_measurement_estimate_battery_current(
        &app->sample, estimate_efficiency_q15(&app->sample), app->relay_applied_feedback,
        (app->power_stage.state == AURORA_POWER_RELAY_SETTLE) ||
            (app->power_stage.state == AURORA_POWER_BAT_STABILITY));

    aurora_protection_step_ex(
        &app->protection, &app->sample, &app->charger.profile, app->storage.settings.operating_mode,
        aurora_measurement_zero_cal_ready(&app->measurement), boost_output_active, now_ms);

"""
text = read("app/src/main.c")
if text.count(old_dup) != 1:
    raise SystemExit(f"main.c: expected one duplicate estimate/protection block, got {text.count(old_dup)}")
write("app/src/main.c", text.replace(old_dup, "", 1))

replace_once(
    "app/src/main.c",
    """        aurora_measurement_zero_cal_failed(&app->measurement), app->relay_applied_feedback,
        app->storage.settings.operating_mode, app->storage.settings.demo_target_voltage_mv,""",
    """        aurora_measurement_zero_cal_failed(&app->measurement), app->relay_applied_feedback,
        app->relay_applied_generation_feedback, app->storage.settings.operating_mode,
        app->storage.settings.demo_target_voltage_mv,""",
)
replace_once(
    "app/src/main.c",
    """    runtime->relay_applied = false;

    now_ms = drv_time_now_ms();""",
    """    runtime->relay_applied = false;
    runtime->relay_applied_generation = 0U;
    runtime->relay_holdoff_baseline_captured = false;

    now_ms = drv_time_now_ms();""",
)
replace_once(
    "app/src/main.c",
    """        runtime->app.relay_applied_feedback = runtime->relay_applied;
        aurora_app_step_1ms(&runtime->app, now_ms, drv_pwm_output_active());""",
    """        runtime->app.relay_applied_feedback = runtime->relay_applied;
        runtime->app.relay_applied_generation_feedback = runtime->relay_applied_generation;
        aurora_app_step_1ms(&runtime->app, now_ms, drv_pwm_output_active());""",
)

# 8) Old 30-byte energy fields keep 120W-compatible charge-side semantics.
text = read("app/src/protocol.c")
text = text.replace(
    "put_u16_le(&frame->data[TELEMETRY_OFFSET_DAILY_ENERGY_0], (uint16_t)settings->daily_energy_wh);",
    "put_u16_le(&frame->data[TELEMETRY_OFFSET_DAILY_ENERGY_0],\n               (uint16_t)settings->charge_est_daily_energy_wh);",
)
text = text.replace(
    "put_u32_le(&frame->data[TELEMETRY_OFFSET_LIFETIME_ENERGY], settings->lifetime_energy_wh);",
    "put_u32_le(&frame->data[TELEMETRY_OFFSET_LIFETIME_ENERGY],\n               settings->charge_est_lifetime_energy_wh);",
)
text = text.replace(
    "put_u16_le(&frame->data[TELEMETRY_OFFSET_DAILY_ENERGY_1], (uint16_t)settings->daily_energy_wh);",
    "put_u16_le(&frame->data[TELEMETRY_OFFSET_DAILY_ENERGY_1],\n               (uint16_t)settings->charge_est_daily_energy_wh);",
)
write("app/src/protocol.c", text)

# 9) Update every step_ex call site to the new generation argument.
for path in ["app/src/main.c", "tests/test_main.c", "tests/test_v0103.c", "tests/app.h"]:
    text = read(path)
    # Calls with relay_applied=false gain a zero generation before operating_mode.
    text = re.sub(
        r"(aurora_power_stage_step_ex\([^;]*?zero_cal_failed[^;]*?|aurora_power_stage_step_ex\([^;]*?)(false,\s*AURORA_MODE_)",
        lambda m: m.group(1) + "false, 0U, AURORA_MODE_",
        text,
        flags=re.S,
    )
    write(path, text)

# Explicitly repair production call, which is not matched by the generic test normalization above.
text = read("app/src/main.c")
text = text.replace(
    "aurora_measurement_zero_cal_failed(&app->measurement), app->relay_applied_feedback,\n        app->storage.settings.operating_mode",
    "aurora_measurement_zero_cal_failed(&app->measurement), app->relay_applied_feedback,\n        app->relay_applied_generation_feedback, app->storage.settings.operating_mode",
)
write("app/src/main.c", text)

# Compatibility wrapper in tests/app.h passes current generation when it assumes Relay is applied.
text = read("tests/app.h")
text = text.replace(
    "zero_cal_failed, true,\n        AURORA_MODE_BATTERY",
    "zero_cal_failed, true, ctx->relay_generation,\n        AURORA_MODE_BATTERY",
)
write("tests/app.h", text)

# 10) Replace v0.10.3 tests with a focused suite that covers the reviewed blockers.
test_file = r'''#include "main.h"

#include "driver.h"
#include "mock_driver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned g_assertions;

#define CHECK(x) do { g_assertions++; if (!(x)) { \
    fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static aurora_measurement_calibration_t unit_calibration(void)
{
    aurora_measurement_calibration_t calibration;
    size_t index;
    memset(&calibration, 0, sizeof(calibration));
    for (index = 0U; index < AURORA_ADC_CHANNEL_COUNT; ++index)
    {
        calibration.channel[index].gain_num = 1;
        calibration.channel[index].gain_den = 1;
        calibration.channel[index].polarity = 1;
        calibration.channel[index].valid = true;
    }
    return calibration;
}

static aurora_measurement_t valid_sample(uint32_t sequence, uint32_t now_ms)
{
    aurora_measurement_t sample;
    memset(&sample, 0, sizeof(sample));
    sample.sequence = sequence;
    sample.timestamp_ms = now_ms;
    sample.valid_mask = AURORA_MEAS_VALID_PV_V | AURORA_MEAS_VALID_PV_I |
                        AURORA_MEAS_VALID_BAT_V | AURORA_MEAS_VALID_BUS_V |
                        AURORA_MEAS_VALID_PV_POWER | AURORA_MEAS_VALID_MOS_TEMP |
                        AURORA_MEAS_VALID_AMB_TEMP;
    sample.pv_voltage_mv = 16000;
    sample.pv_current_ma = 1000;
    sample.pv_power_mw = 16000;
    sample.battery_voltage_mv = 48000;
    sample.bus_voltage_mv = 47500;
    sample.mos_temp_dC = 250;
    sample.ambient_temp_dC = 250;
    sample.mos_ntc_status = AURORA_NTC_STATUS_OK;
    sample.ambient_ntc_status = AURORA_NTC_STATUS_OK;
    return sample;
}

static void test_break_uses_software_arm_state(void)
{
    aurora_runtime_t runtime;
    uint32_t sequence;
    mock_reset();
    CHECK(aurora_runtime_init(&runtime));
    CHECK(drv_pwm_prepare_arm_zero(&sequence));
    mock_apply_uev();
    CHECK(drv_pwm_arm());
    runtime.pwm_arm_state = AURORA_RUNTIME_PWM_ARM_ACTIVE;
    mock_set_break(true);
    CHECK(!mock_pwm_active());
    aurora_runtime_isr_comparator_fault(&runtime, AURORA_FAULT_FAST_MOS_OCP);
    CHECK((runtime.pending_fault_mask & AURORA_FAULT_FAST_MOS_OCP) != 0U);
    CHECK(runtime.startup_comp_ignored_count == 0U);

    mock_reset();
    CHECK(aurora_runtime_init(&runtime));
    runtime.pwm_arm_state = AURORA_RUNTIME_PWM_ARM_WAIT_ZERO;
    aurora_runtime_isr_comparator_fault(&runtime, AURORA_FAULT_FAST_MOS_OCP);
    CHECK(runtime.pending_fault_mask == 0U);
    CHECK(runtime.startup_comp_ignored_count == 1U);
}

static void test_runtime_captures_post_pwm_off_baseline(void)
{
    aurora_runtime_t runtime;
    mock_reset();
    CHECK(aurora_runtime_init(&runtime));
    runtime.app.sample = valid_sample(9U, 0U);
    runtime.app.power_stage.state = AURORA_POWER_RELAY_HOLD_OFF;
    runtime.app.power_stage.relay_holdoff_sequence = 0U;
    runtime.app.power_command.state = AURORA_POWER_RELAY_HOLD_OFF;
    runtime.app.power_command.pwm_enable = false;
    runtime.app.power_command.relay_enable = false;
    aurora_runtime_poll(&runtime);
    CHECK(runtime.relay_holdoff_baseline_captured);
    CHECK(runtime.app.power_stage.relay_holdoff_sequence == 9U);
    CHECK(!mock_pwm_active());
    CHECK(!mock_relay());
}

static void test_holdoff_requires_two_new_blocks_and_matching_generation(void)
{
    aurora_power_stage_ctx_t ctx;
    aurora_measurement_t sample = valid_sample(1U, 1U);
    aurora_mppt_output_t mppt = {0};
    aurora_charge_output_t charger = {0};
    aurora_power_command_t command;
    charger.allow_charge = true;
    charger.pv_power_limit_mw = 100000U;
    charger.voltage_target_mv = 50000U;

    aurora_power_stage_init(&ctx, 0U);
    ctx.state = AURORA_POWER_RELAY_HOLD_OFF;
    ctx.state_since_ms = 1000U;
    ctx.relay_holdoff_sequence = 1U;

    sample.sequence = 2U;
    sample.timestamp_ms = 1021U;
    command = aurora_power_stage_step_ex(&ctx, &sample, &mppt, &charger, true, true, false,
                                         false, 0U, AURORA_MODE_BATTERY, 48000U, 30000U, 1021U);
    CHECK(command.state == AURORA_POWER_RELAY_HOLD_OFF);
    CHECK(!command.relay_enable);

    sample.sequence = 3U;
    sample.timestamp_ms = 1022U;
    command = aurora_power_stage_step_ex(&ctx, &sample, &mppt, &charger, true, true, false,
                                         false, 0U, AURORA_MODE_BATTERY, 48000U, 30000U, 1022U);
    CHECK(command.state == AURORA_POWER_RELAY_SETTLE);
    CHECK(command.relay_enable);
    CHECK(command.relay_generation != 0U);

    sample.timestamp_ms = 1122U;
    command = aurora_power_stage_step_ex(&ctx, &sample, &mppt, &charger, true, true, false,
                                         true, command.relay_generation - 1U,
                                         AURORA_MODE_BATTERY, 48000U, 30000U, 1122U);
    CHECK(command.state == AURORA_POWER_RELAY_SETTLE);
    CHECK(ctx.delta_ok_since_ms == 0U);
}

static void test_holdoff_delta_loss_is_bounded(void)
{
    aurora_power_stage_ctx_t ctx;
    aurora_measurement_t sample = valid_sample(12U, 600U);
    aurora_mppt_output_t mppt = {0};
    aurora_charge_output_t charger = {0};
    aurora_power_command_t command;
    aurora_power_stage_init(&ctx, 0U);
    ctx.state = AURORA_POWER_RELAY_HOLD_OFF;
    ctx.state_since_ms = 0U;
    ctx.relay_holdoff_sequence = 10U;
    sample.bus_voltage_mv = sample.battery_voltage_mv - AURORA_RELAY_CLOSE_DELTA_MV - 1000L;
    command = aurora_power_stage_step_ex(&ctx, &sample, &mppt, &charger, true, true, false,
                                         false, 0U, AURORA_MODE_BATTERY, 48000U, 30000U, 600U);
    CHECK(command.state == AURORA_POWER_FAULT);
    CHECK(ctx.last_failure_reason == AURORA_START_FAIL_BUS_PRECHARGE_TIMEOUT);
    CHECK(ctx.precharge_failure_count == 1U);
}

static void test_weak_light_requires_voltage_droop(void)
{
    aurora_power_stage_ctx_t ctx;
    aurora_measurement_t sample = valid_sample(1U, 1U);
    aurora_mppt_output_t mppt = {0};
    aurora_charge_output_t charger = {0};
    aurora_power_command_t command;
    aurora_power_stage_init(&ctx, 0U);
    ctx.state = AURORA_POWER_PRECHARGE;
    ctx.state_since_ms = 0U;
    ctx.precharge_pv_entry_mv = 16000;
    ctx.precharge_pv_min_mv = 16000;
    ctx.precharge_bus_start_mv = 30000;
    ctx.precharge_bus_max_mv = 30000;
    sample.bus_voltage_mv = 30000;
    sample.pv_power_mw = 1000;

    sample.pv_voltage_mv = 15800;
    command = aurora_power_stage_step_ex(&ctx, &sample, &mppt, &charger, true, true, false,
                                         false, 0U, AURORA_MODE_BATTERY, 48000U, 30000U, 2500U);
    CHECK(command.state == AURORA_POWER_PRECHARGE);
    CHECK(ctx.precharge_low_power_since_ms == 0U);

    sample.pv_voltage_mv = 14000;
    command = aurora_power_stage_step_ex(&ctx, &sample, &mppt, &charger, true, true, false,
                                         false, 0U, AURORA_MODE_BATTERY, 48000U, 30000U, 3000U);
    CHECK(command.state == AURORA_POWER_PRECHARGE);
    command = aurora_power_stage_step_ex(&ctx, &sample, &mppt, &charger, true, true, false,
                                         false, 0U, AURORA_MODE_BATTERY, 48000U, 30000U,
                                         3000U + AURORA_PRECHARGE_WEAK_HOLD_MS);
    CHECK(command.state == AURORA_POWER_WAIT_PV);
    CHECK(ctx.precharge_failure_count == 0U);
}

static void test_demo_low_residual_bus_and_gate_path(void)
{
    aurora_runtime_t runtime;
    mock_reset();
    CHECK(aurora_runtime_init(&runtime));
    runtime.app.storage.settings.operating_mode = AURORA_MODE_DEMO_LOAD;
    runtime.app.sample = valid_sample(1U, 0U);
    runtime.app.sample.battery_voltage_mv = 0;
    runtime.app.sample.bus_voltage_mv = AURORA_DEMO_RELAY_CLOSE_BUS_MAX_MV + 1L;
    runtime.app.power_command.state = AURORA_POWER_DEMO_RELAY_SETTLE;
    runtime.app.power_command.relay_enable = true;
    runtime.app.power_command.relay_generation = 1U;
    aurora_runtime_poll(&runtime);
    CHECK(!runtime.relay_applied);
    CHECK(!mock_relay());

    runtime.app.sample.bus_voltage_mv = AURORA_DEMO_RELAY_CLOSE_BUS_MAX_MV;
    runtime.app.sample.timestamp_ms = drv_time_now_ms();
    aurora_runtime_poll(&runtime);
    CHECK(runtime.relay_applied);
    CHECK(runtime.relay_applied_generation == 1U);
    CHECK(mock_relay());
}

static void test_pending_fault_keeps_break_latched(void)
{
    aurora_runtime_t runtime;
    mock_reset();
    CHECK(aurora_runtime_init(&runtime));
    mock_set_break(true);
    mock_set_break(false);
    CHECK(drv_pwm_break_latched());
    runtime.pending_fault_mask = AURORA_FAULT_FAST_MOS_OCP;
    runtime.app.power_command.pwm_enable = true;
    aurora_runtime_poll(&runtime);
    CHECK(drv_pwm_break_latched());
    CHECK(!mock_pwm_active());
}

static void test_stale_energy_and_adc_timebase(void)
{
    aurora_app_t app;
    aurora_measurement_calibration_t calibration = unit_calibration();
    aurora_measurement_t sample;
    aurora_app_init(&app, &calibration, 0U);
    app.power_stage.state = AURORA_POWER_PRECHARGE;

    sample = valid_sample(1U, 10U);
    app.measurement.latest = sample;
    app.measurement.publish_sequence = sample.sequence;
    aurora_app_step_1ms(&app, 10U, true);
    CHECK(app.energy_accumulator_mw_ms == 0ULL);

    sample.sequence = 2U;
    sample.timestamp_ms = 20U;
    app.measurement.latest = sample;
    app.measurement.publish_sequence = sample.sequence;
    aurora_app_step_1ms(&app, 20U, true);
    CHECK(app.energy_accumulator_mw_ms == 160000ULL);

    sample.sequence = 3U;
    sample.timestamp_ms = 20U + AURORA_MEASUREMENT_STALE_MS + 1U;
    app.measurement.latest = sample;
    app.measurement.publish_sequence = sample.sequence;
    aurora_app_step_1ms(&app, sample.timestamp_ms, true);
    CHECK(app.energy_accumulator_mw_ms == 160000ULL);
}

static void test_legacy_energy_fields_keep_charge_semantics(void)
{
    aurora_protocol_frame_t frame;
    aurora_measurement_t sample = valid_sample(1U, 1U);
    aurora_persistent_settings_t settings;
    memset(&settings, 0, sizeof(settings));
    settings.daily_energy_wh = 111U;
    settings.lifetime_energy_wh = 222U;
    settings.charge_est_daily_energy_wh = 33U;
    settings.charge_est_lifetime_energy_wh = 44U;
    aurora_protocol_fill_telemetry_ex(&frame, 1U, &sample, AURORA_CHARGE_CC, true, 0U, &settings);
    CHECK(frame.data[4] == 33U);
    CHECK(frame.data[5] == 0U);
    CHECK(frame.data[15] == 44U);
    CHECK(frame.data[16] == 0U);
    CHECK(frame.data[17] == 0U);
    CHECK(frame.data[18] == 0U);
    CHECK(frame.data[20] == 33U);
    CHECK(frame.data[21] == 0U);
}

int main(void)
{
    test_break_uses_software_arm_state();
    test_runtime_captures_post_pwm_off_baseline();
    test_holdoff_requires_two_new_blocks_and_matching_generation();
    test_holdoff_delta_loss_is_bounded();
    test_weak_light_requires_voltage_droop();
    test_demo_low_residual_bus_and_gate_path();
    test_pending_fault_keeps_break_latched();
    test_stale_energy_and_adc_timebase();
    test_legacy_energy_fields_keep_charge_semantics();
    printf("Aurora v0.10.3 reviewed closeout tests: %u assertions passed.\n", g_assertions);
    return 0;
}
'''
write("tests/test_v0103.c", test_file)

# Contract coverage for the newly reviewed safety properties.
path = "tests/test_v0103_contract.py"
text = read(path)
insert = '''
    def test_reviewed_closeout_contracts(self):
        main_c = (ROOT / "app/src/main.c").read_text(encoding="utf-8")
        power_c = (ROOT / "app/src/power_stage.c").read_text(encoding="utf-8")
        config = (ROOT / "app/inc/app_config.h").read_text(encoding="utf-8")
        protocol = (ROOT / "app/src/protocol.c").read_text(encoding="utf-8")
        self.assertIn("drv_board_power_gate_open()", main_c)
        self.assertIn("drv_board_demo_load_gate_open()", main_c)
        self.assertIn("AURORA_DEMO_RELAY_CLOSE_BUS_MAX_MV", main_c)
        self.assertIn("runtime->pending_fault_mask == 0U", main_c)
        self.assertIn("relay_applied_generation", main_c)
        self.assertIn("AURORA_RELAY_POST_OFF_MIN_BLOCKS", power_c)
        self.assertIn("AURORA_PRECHARGE_WEAK_PV_DROOP_MV", power_c)
        self.assertIn("AURORA_RELAY_HOLDOFF_TIMEOUT_MS", power_c)
        self.assertIn("AURORA_DEMO_RELAY_CLOSE_BUS_MAX_MV          (5000L)", config)
        self.assertIn("settings->charge_est_lifetime_energy_wh", protocol)
'''
if "def test_reviewed_closeout_contracts" not in text:
    text = text.replace('\n\nif __name__ == "__main__":', insert + '\n\nif __name__ == "__main__":', 1)
write(path, text)

# 11) Permanent UTF-8/mojibake check and fail-closed tool discovery.
write(
    "tools/check_text_encoding.py",
    '''#!/usr/bin/env python3
"""严格检查工程文本编码，防止UTF-8/GBK误转和常见乱码进入仓库。"""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
SUFFIXES = {".c", ".h", ".py", ".md", ".yml", ".yaml", ".cmake", ".txt", ".sct", ".uvprojx", ".svg"}
SKIP = {".git", "build-gcc", "build-clang", "build-sanitize", "__pycache__"}
BAD = ("\ufffd", "锟斤拷", "烫烫烫", "屯屯屯", "ï¿½", "â€™")
errors = []
for path in ROOT.rglob("*"):
    if not path.is_file() or path.suffix.lower() not in SUFFIXES or any(part in SKIP for part in path.parts):
        continue
    raw = path.read_bytes()
    if raw.startswith(b"\xef\xbb\xbf"):
        errors.append(f"{path.relative_to(ROOT)}: UTF-8 BOM")
        continue
    if b"\x00" in raw:
        errors.append(f"{path.relative_to(ROOT)}: NUL byte")
        continue
    try:
        text = raw.decode("utf-8", errors="strict")
    except UnicodeDecodeError as exc:
        errors.append(f"{path.relative_to(ROOT)}: invalid UTF-8: {exc}")
        continue
    for token in BAD:
        if token in text:
            errors.append(f"{path.relative_to(ROOT)}: mojibake token {token!r}")
if errors:
    print("TEXT ENCODING CHECK: FAIL")
    print("\n".join(errors))
    sys.exit(1)
print("TEXT ENCODING CHECK: PASS")
''',
)
write(
    "tools/run_checks.py",
    '''#!/usr/bin/env python3
"""Fail-closed运行架构、编码、Host编译/测试、Sanitizer和目标端语法门禁。"""
from pathlib import Path
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[1]

def run(args: list[str]) -> None:
    print("+", " ".join(args), flush=True)
    subprocess.run(args, cwd=root, check=True)

def require(tool: str) -> None:
    if shutil.which(tool) is None:
        raise SystemExit(f"required tool missing: {tool}")

for tool in ("cmake", "ninja", "gcc", "clang"):
    require(tool)
run([sys.executable, "tools/check_architecture.py"])
run([sys.executable, "tools/check_code_style.py"])
run([sys.executable, "tools/check_text_encoding.py"])
run([sys.executable, "-m", "unittest", "discover", "-s", "tests", "-p", "test_*.py", "-v"])
for compiler, build in (("gcc", "build-gcc"), ("clang", "build-clang")):
    run(["cmake", "-S", ".", "-B", build, "-G", "Ninja", f"-DCMAKE_C_COMPILER={compiler}"])
    run(["cmake", "--build", build])
    run(["ctest", "--test-dir", build, "--output-on-failure"])
run(["cmake", "-S", ".", "-B", "build-sanitize", "-G", "Ninja", "-DCMAKE_C_COMPILER=clang",
     "-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer"])
run(["cmake", "--build", "build-sanitize"])
run(["ctest", "--test-dir", "build-sanitize", "--output-on-failure"])
run([sys.executable, "tools/check_target_syntax.py"])
print("ALL CHECKS PASSED")
''',
)

# 12) Docs current-state corrections.
write(
    "docs/46-v0.10.3-审阅问题补强与验证记录.md",
    '''# 46 · v0.10.3 审阅问题补强与验证记录

本轮关闭 `d967915...` 二次审核中剩余的软件安全缺口，不移植 OTA/IAP，不修改 3A CC、12A PV 限流、BST_U 分压 BOM，也不打开任何生产功率门禁。

## Relay 与 Demo

- `RELAY_HOLD_OFF` 的 ADC 基准由 Runtime 在物理 `drv_pwm_disarm()` 后记录，不再由 PowerStage 在关波前记录。
- 至少跨过两个新的 DMA 发布代次并满足 20ms 放能，才允许重新复核。
- Relay 反馈增加 generation；旧 `true` 不能授权新事务。
- Battery 仍要求压差 <=1.5V；Demo 额外要求 BAT_U<=5V、BST_U<=5V、公共功率门和 Demo 台架门均放行。
- 关波后压差丢失超过500ms按有限预充失败处理，不再 PRECHARGE/HOLD_OFF 无限循环。

## 预充与快速故障

- 低 Ppv 只有同时伴随相对 PRECHARGE 入口至少1V的PV压降并持续2s，才归类为 `PV_WEAK`；高Voc无BUS进展仍按功率路径失败有限重试。
- `clear_startup_break_if_safe()` 增加 `pending_fault_mask==0`，避免ISR已投递但Protection尚未锁存时提前清Break证据。

## 能量与协议

- Protection 在能量资格判断前更新；PV能量只在 PRECHARGE / Battery RUN / Demo Probe/Run 且物理PWM输出、样本新鲜有效时累计。
- 能量时间使用 ADC `sequence/timestamp_ms` 差值；首个样本只建立时间基准，不用单点功率外推长时间。
- 旧30字节帧布局不变，daily/lifetime energy恢复120W兼容的电池侧充电量语义，发送 `charge_est_*`，质量明确为 ESTIMATED；PV实测账本继续保存在Flash v3。

## 发布边界

Host/GCC/Clang/Sanitizer/目标语法通过后仍只能作为低压台架候选。Windows Keil AC6 v0.10.3 链接/MAP、PV_I/OPA、COMP→Break→Vgs、Relay拉弧、Demo负载矩阵、BST_U量程与300W SOA仍需目标工具链或实板验证。
''',
)
for path in [
    "docs/42-v0.10.2-Demo无电池带载模式与安全边界.md",
    "docs/45-v0.10.3-安全握手与快故障修复说明.md",
]:
    text = read(path)
    text = text.replace("BST_U 有效、未饱和、未超过 Demo 目标加安全裕量", "BST_U 有效、未饱和且闭合前不高于 5V 低风险阈值")
    text = text.replace("BST_U有效、未饱和且不超过Demo目标加安全裕量", "BST_U有效、未饱和且闭合前不高于5V低风险阈值")
    write(path, text)

path = "docs/45-v0.10.3-安全握手与快故障修复说明.md"
text = read(path)
text = text.replace(
    "本版本不修改既有30字节遥测字段布局或含义。旧字段继续上报当前PV实测能量；电池侧估算账本继续保存在Flash v3中。该兼容选择必须由上位机/产品文档明确，不应在安全修复版本中静默改变线上字段语义。",
    "本版本不修改既有30字节遥测布局；为保持120W产品语义，旧daily/lifetime energy字段发送Flash v3中的电池侧估算充电量。该值没有BAT_I实测证据，必须标记为ESTIMATED；PV输入侧实测能量继续独立保存在Flash v3。",
)
write(path, text)

path = "docs/09-内部Flash参数保存.md"
text = read(path)
text = text.replace(
    "v0.10.3不改变既有30字节遥测布局和当前字段映射，避免安全修复版本静默破坏已有工具。Flash内部继续同时保存PV实测账本与电池侧估算账本；二者物理语义和质量必须在上位机/产品文档中明确。",
    "v0.10.3不改变既有30字节遥测布局。为兼容120W充电量语义，旧daily/lifetime energy字段映射到 `charge_est_*`，质量为ESTIMATED；PV实测账本继续独立保存在Flash v3。",
)
write(path, text)

path = "docs/00-文档索引.md"
text = read(path)
if "46-v0.10.3-审阅问题补强与验证记录.md" not in text:
    text = text.rstrip() + "\n- `docs/46-v0.10.3-审阅问题补强与验证记录.md`：二次审核发现的Relay/ADC/能量与预充问题闭环。\n"
write(path, text)

# Final sanity.
for path in [
    "app/src/main.c", "app/src/power_stage.c", "app/inc/main.h", "app/inc/power_stage.h",
    "app/inc/app_types.h", "app/inc/app_config.h", "app/src/protocol.c", "driver/src/drv_board.c",
    "tests/test_v0103.c",
]:
    data = read(path)
    if "\ufffd" in data or "锟斤拷" in data:
        raise SystemExit(f"{path}: encoding corruption detected")

print("v0.10.3 reviewed closeout patch applied")
