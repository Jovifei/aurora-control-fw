#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    target = ROOT / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text, encoding="utf-8", newline="\n")


def replace_once(path: str, old: str, new: str) -> None:
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected exactly one match, got {count}: {old[:100]!r}")
    write(path, text.replace(old, new, 1))


# ---------------------------------------------------------------------------
# 1. Relay HOLD_OFF：32位ADC发布序号0是合法回绕值，不能作为“未捕获”哨兵。
# ---------------------------------------------------------------------------
replace_once(
    "app/inc/power_stage.h",
    """    bool relay_closed;                               /* 软件期望继电器状态。 */
    bool startup_success_recorded;                   /* 本轮成功后只减少一次启动延时。 */
    bool startup_locked;                             /* 有限重试耗尽或严重过压后锁存。 */
    bool demo_load_confirmed;                        /* Demo探测得到持续负载证据。 */
    uint8_t state_reserved[3];                       /* 显式补齐结构尾部。 */""",
    """    bool relay_closed;                               /* 软件期望继电器状态。 */
    bool relay_holdoff_sequence_valid;               // true表示Runtime已在物理关PWM后捕获基准；sequence=0同样合法。
    bool startup_success_recorded;                   /* 本轮成功后只减少一次启动延时。 */
    bool startup_locked;                             /* 有限重试耗尽或严重过压后锁存。 */
    bool demo_load_confirmed;                        /* Demo探测得到持续负载证据。 */
    uint8_t state_reserved[2];                       /* 显式补齐结构尾部。 */""",
)

replace_once(
    "app/src/power_stage.c",
    """    ctx->demo_probe_since_ms = 0U;
    ctx->demo_no_load_since_ms = 0U;

    if ((state != AURORA_POWER_PRECHARGE) && (state != AURORA_POWER_RUN) &&""",
    """    ctx->demo_probe_since_ms = 0U;
    ctx->demo_no_load_since_ms = 0U;

    if (state == AURORA_POWER_RELAY_HOLD_OFF)
    {
        /* 0是32位ADC发布序号的合法回绕值；有效性必须由独立标志表示。 */
        ctx->relay_holdoff_sequence = 0U;
        ctx->relay_holdoff_sequence_valid = false;
    }

    if ((state != AURORA_POWER_PRECHARGE) && (state != AURORA_POWER_RUN) &&""",
)

replace_once(
    "app/src/power_stage.c",
    """                ctx->duty_q15 = 0U;
                ctx->power_integral = 0LL;
                ctx->relay_closed = false;
                ctx->relay_holdoff_sequence = 0U;
                enter_state(ctx, AURORA_POWER_RELAY_HOLD_OFF, now_ms);""",
    """                ctx->duty_q15 = 0U;
                ctx->power_integral = 0LL;
                ctx->relay_closed = false;
                enter_state(ctx, AURORA_POWER_RELAY_HOLD_OFF, now_ms);""",
)

replace_once(
    "app/src/power_stage.c",
    """        if ((ctx->relay_holdoff_sequence == 0U) ||
            (elapsed_ms(now_ms, ctx->state_since_ms) < AURORA_RELAY_PWM_OFF_DECAY_MS) ||""",
    """        if (!ctx->relay_holdoff_sequence_valid ||
            (elapsed_ms(now_ms, ctx->state_since_ms) < AURORA_RELAY_PWM_OFF_DECAY_MS) ||""",
)

replace_once(
    "app/src/power_stage.c",
    """            /* 先进入共享关波放能状态；Demo闭合条件不复用Battery均压判据。 */
            ctx->relay_holdoff_sequence = 0U;
            enter_state(ctx, AURORA_POWER_RELAY_HOLD_OFF, now_ms);""",
    """            /* 先进入共享关波放能状态；Demo闭合条件不复用Battery均压判据。 */
            enter_state(ctx, AURORA_POWER_RELAY_HOLD_OFF, now_ms);""",
)

replace_once(
    "app/src/main.c",
    """            runtime->app.power_stage.relay_holdoff_sequence = runtime->app.sample.sequence;
            runtime->app.power_stage.state_since_ms = drv_time_now_ms();
            runtime->relay_holdoff_baseline_captured = true;""",
    """            runtime->app.power_stage.relay_holdoff_sequence = runtime->app.sample.sequence;
            runtime->app.power_stage.relay_holdoff_sequence_valid = true;
            runtime->app.power_stage.state_since_ms = drv_time_now_ms();
            runtime->relay_holdoff_baseline_captured = true;""",
)


# ---------------------------------------------------------------------------
# 2. Host专用功率门覆盖：目标固件误配宏必须在编译期直接失败。
# ---------------------------------------------------------------------------
replace_once(
    "driver/src/drv_board.c",
    """#include "board_config.h"

/*---------------------------------------------------------------------------*""",
    """#include "board_config.h"

#if defined(AURORA_HOST_TEST_POWER_GATES_OPEN) && !defined(AURORA_HOST_TEST)
#error "AURORA_HOST_TEST_POWER_GATES_OPEN is forbidden in target firmware"
#endif

/*---------------------------------------------------------------------------*""",
)

replace_once(
    "driver/src/drv_board.c",
    """#if defined(AURORA_HOST_TEST_POWER_GATES_OPEN)
    // 仅v0.10.3端到端Host目标使用；生产目标仍完全由BOARD_GATE_*与总门控制。""",
    """#if defined(AURORA_HOST_TEST) && defined(AURORA_HOST_TEST_POWER_GATES_OPEN)
    // 仅v0.10.3端到端Host目标使用；生产目标仍完全由BOARD_GATE_*与总门控制。""",
)

replace_once(
    "driver/src/drv_board.c",
    """#if defined(AURORA_HOST_TEST_POWER_GATES_OPEN)
    return true;
#else
    return (BOARD_GATE_DEMO_LOAD_VALIDATED != 0U);""",
    """#if defined(AURORA_HOST_TEST) && defined(AURORA_HOST_TEST_POWER_GATES_OPEN)
    return true;
#else
    return (BOARD_GATE_DEMO_LOAD_VALIDATED != 0U);""",
)

replace_once(
    "driver/inc/board_config.h",
    """/* PinMap已经人工复核。 */
#define BOARD_GATE_PINMAP_REVIEWED                  (1U)""",
    """/*
 * 以下BOARD_GATE_*与BOARD_POWER_OUTPUT_ALLOWED是最终验收证据门，不是Bring-up阶段使能开关。
 * 低压/分阶段台架必须使用独立受限Bring-up构建，只开放当阶段最小能力；
 * 禁止为了“能跑台架”提前把尚未完成的最终验收Gate置1。
 */
/* PinMap已经人工复核。 */
#define BOARD_GATE_PINMAP_REVIEWED                  (1U)""",
)


# ---------------------------------------------------------------------------
# 3. 行为/合同测试：锁定sequence=0回绕和Host宏边界。
# ---------------------------------------------------------------------------
replace_once(
    "tests/test_v0103.c",
    """    runtime.app.sample = valid_sample(9U, 0U);
    runtime.app.power_stage.state = AURORA_POWER_RELAY_HOLD_OFF;
    runtime.app.power_stage.relay_holdoff_sequence = 0U;""",
    """    /* 模拟32位ADC发布序号恰好回绕到0；0不能再被当成“未捕获”哨兵。 */
    runtime.app.sample = valid_sample(0U, 0U);
    runtime.app.power_stage.state = AURORA_POWER_RELAY_HOLD_OFF;
    runtime.app.power_stage.relay_holdoff_sequence = 123U;
    runtime.app.power_stage.relay_holdoff_sequence_valid = false;""",
)

replace_once(
    "tests/test_v0103.c",
    """    CHECK(runtime.relay_holdoff_baseline_captured);
    CHECK(runtime.app.power_stage.relay_holdoff_sequence == 9U);
    CHECK(!mock_pwm_active());""",
    """    CHECK(runtime.relay_holdoff_baseline_captured);
    CHECK(runtime.app.power_stage.relay_holdoff_sequence_valid);
    CHECK(runtime.app.power_stage.relay_holdoff_sequence == 0U);
    CHECK(!mock_pwm_active());""",
)

replace_once(
    "tests/test_v0103.c",
    """    ctx.state_since_ms = 1000U;
    ctx.relay_holdoff_sequence = 1U;

    sample.sequence = 2U;""",
    """    ctx.state_since_ms = 1000U;
    ctx.relay_holdoff_sequence = 1U;
    ctx.relay_holdoff_sequence_valid = true;

    sample.sequence = 2U;""",
)

replace_once(
    "tests/test_v0103.c",
    """    ctx.state_since_ms = 0U;
    ctx.relay_holdoff_sequence = 10U;
    sample.bus_voltage_mv = sample.battery_voltage_mv - AURORA_RELAY_CLOSE_DELTA_MV - 1000L;""",
    """    ctx.state_since_ms = 0U;
    ctx.relay_holdoff_sequence = 10U;
    ctx.relay_holdoff_sequence_valid = true;
    sample.bus_voltage_mv = sample.battery_voltage_mv - AURORA_RELAY_CLOSE_DELTA_MV - 1000L;""",
)

marker = """/*---------------------------------------------------------------------------*
 * Name        : static void test_holdoff_delta_loss_is_bounded(void)"""
wrap_test = r'''/*---------------------------------------------------------------------------*
 * Name        : static void test_holdoff_sequence_wrap_zero_is_valid(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证关波基准恰好为sequence=0时仍按无符号回绕等待两个新DMA块，不会永久卡死。
 *---------------------------------------------------------------------------*/
static void test_holdoff_sequence_wrap_zero_is_valid(void)
{
    aurora_power_stage_ctx_t ctx;
    aurora_measurement_t sample = valid_sample(1U, 1021U);
    aurora_mppt_output_t mppt = {0};
    aurora_charge_output_t charger = {0};
    aurora_power_command_t command;

    charger.voltage_target_mv = 50000U;
    aurora_power_stage_init(&ctx, 0U);
    ctx.state = AURORA_POWER_RELAY_HOLD_OFF;
    ctx.state_since_ms = 1000U;
    ctx.relay_holdoff_sequence = 0U;
    ctx.relay_holdoff_sequence_valid = true;

    command = aurora_power_stage_step_ex(&ctx, &sample, &mppt, &charger, true, true, false,
                                         false, 0U, AURORA_MODE_BATTERY, 48000U, 30000U, 1021U);
    CHECK(command.state == AURORA_POWER_RELAY_HOLD_OFF);
    CHECK(!command.relay_enable);

    sample.sequence = 2U;
    sample.timestamp_ms = 1022U;
    command = aurora_power_stage_step_ex(&ctx, &sample, &mppt, &charger, true, true, false,
                                         false, 0U, AURORA_MODE_BATTERY, 48000U, 30000U, 1022U);
    CHECK(command.state == AURORA_POWER_RELAY_SETTLE);
    CHECK(command.relay_enable);
}

'''
text = read("tests/test_v0103.c")
if text.count(marker) != 1:
    raise SystemExit("tests/test_v0103.c: holdoff insertion marker mismatch")
write("tests/test_v0103.c", text.replace(marker, wrap_test + marker, 1))

replace_once(
    "tests/test_v0103.c",
    """    test_holdoff_requires_two_new_blocks_and_matching_generation();
    test_holdoff_delta_loss_is_bounded();""",
    """    test_holdoff_requires_two_new_blocks_and_matching_generation();
    test_holdoff_sequence_wrap_zero_is_valid();
    test_holdoff_delta_loss_is_bounded();""",
)

replace_once(
    "tests/test_v0103_contract.py",
    """        self.assertIn("relay_holdoff_sequence", power)
        self.assertIn("AURORA_RELAY_POST_OFF_MIN_BLOCKS", power)""",
    """        self.assertIn("relay_holdoff_sequence", power)
        self.assertIn("relay_holdoff_sequence_valid", power)
        self.assertIn("!ctx->relay_holdoff_sequence_valid", power)
        self.assertIn("AURORA_RELAY_POST_OFF_MIN_BLOCKS", power)""",
)

replace_once(
    "tests/test_v0103_contract.py",
    """        protocol = (ROOT / "app/src/protocol.c").read_text(encoding="utf-8")
        self.assertIn("drv_board_power_gate_open()", main_c)""",
    """        protocol = (ROOT / "app/src/protocol.c").read_text(encoding="utf-8")
        board_driver = (ROOT / "driver/src/drv_board.c").read_text(encoding="utf-8")
        self.assertIn("drv_board_power_gate_open()", main_c)""",
)

replace_once(
    "tests/test_v0103_contract.py",
    """        self.assertIn("settings->charge_est_lifetime_energy_wh", protocol)""",
    """        self.assertIn("settings->charge_est_lifetime_energy_wh", protocol)
        self.assertIn(
            "#if defined(AURORA_HOST_TEST_POWER_GATES_OPEN) && !defined(AURORA_HOST_TEST)",
            board_driver,
        )
        self.assertIn(
            "#if defined(AURORA_HOST_TEST) && defined(AURORA_HOST_TEST_POWER_GATES_OPEN)",
            board_driver,
        )""",
)


# ---------------------------------------------------------------------------
# 4. 文档：修正fail-closed、30字节能量语义、Bring-up门禁策略和编号冲突。
# ---------------------------------------------------------------------------
replace_once(
    "README.md",
    """- [v0.10.3安全握手与快故障修复](docs/45-v0.10.3-安全握手与快故障修复说明.md)""",
    """- [v0.10.3安全握手与快故障修复](docs/45-v0.10.3-安全握手与快故障修复说明.md)
- [新工程分阶段移植与板级验证路线](docs/46-v0.10.3-新工程分阶段移植与板级验证路线.md)
- [v0.10.3审阅问题补强与验证记录](docs/47-v0.10.3-审阅问题补强与验证记录.md)""",
)

replace_once(
    "README.md",
    """本地若缺少某个编译器，脚本会在输出中明确显示 `skip`；正式发布结论应以工具齐全的 GitHub Actions、Keil ARM Compiler 6日志和MAP审计共同为准。Host通过不等于功率板验收通过。""",
    """`run_checks.py`采用fail-closed：缺少CMake、Ninja、GCC或Clang任一必需工具时直接失败，不输出完整PASS结论。正式发布结论仍必须结合工具齐全的GitHub Actions、Keil ARM Compiler 6日志和MAP审计；Host通过不等于功率板验收通过。""",
)

replace_once(
    "README.md",
    """SOFTWARE SAFETY FIXED
READY FOR LOW-VOLTAGE BENCH
POWER GATES STILL LOCKED
OTA/IAP OUT OF SCOPE""",
    """SOFTWARE SAFETY FIXED
SOFTWARE CANDIDATE FOR BRING-UP INTEGRATION
PRODUCTION BUILD POWER GATES STILL LOCKED
OTA/IAP OUT OF SCOPE""",
)

replace_once(
    "README.md",
    """本版本不修改3A CC、12A PV限流、BST_U分压BOM或既有30字节遥测字段语义；也不新增OTA/IAP代码。""",
    """本版本不修改3A CC、12A PV限流和BST_U分压BOM，也不新增OTA/IAP代码。旧30字节遥测**布局不变**；daily/lifetime能量字段恢复120W兼容的电池侧充电量语义，当前硬件无BAT_I，因此该值明确为ESTIMATED。

当前`BOARD_GATE_*`与`BOARD_POWER_OUTPUT_ALLOWED`是最终验收证据门，不作为低压Bring-up的临时使能开关。当前生产配置本身不会直接解锁Relay/PWM；低压验证应按`docs/46-v0.10.3-新工程分阶段移植与板级验证路线.md`使用独立受限Bring-up构建，只开放当前阶段所需的最小能力，不能提前把尚未验收的最终Gate置1。""",
)

replace_once(
    "docs/README.md",
    """16. [46-新工程分阶段移植与板级验证路线](46-v0.10.3-新工程分阶段移植与板级验证路线.md)
17. [程序代码分析阅读索引](程序代码分析/00-阅读索引.md)""",
    """16. [46-新工程分阶段移植与板级验证路线](46-v0.10.3-新工程分阶段移植与板级验证路线.md)
17. [47-v0.10.3审阅问题补强与验证记录](47-v0.10.3-审阅问题补强与验证记录.md)
18. [程序代码分析阅读索引](程序代码分析/00-阅读索引.md)""",
)

replace_once(
    "docs/README.md",
    """当前版本只具备进入低压限流台架的**软件候选资格**。Keil v0.10.3重新构建、模拟标定、COMP→Break→Vgs、Relay拉弧、Demo真实负载和300W SOA仍需单独留证据。""",
    """当前版本只具备集成到受限Bring-up构建的**软件候选资格**。当前生产配置的最终验收Gate全部保持fail-closed，不能直接作为“解门台架固件”；低压验证应按46号路线使用独立Bring-up构建，只开放当阶段最小能力。Keil v0.10.3重新构建、模拟标定、COMP→Break→Vgs、Relay拉弧、Demo真实负载和300W SOA仍需单独留证据。""",
)

replace_once(
    "docs/46-v0.10.3-新工程分阶段移植与板级验证路线.md",
    """> **OTA / IAP**：当前产品决策为**不移植**，不属于本路线任何 Gate 的前置条件。

---""",
    """> **OTA / IAP**：当前产品决策为**不移植**，不属于本路线任何 Gate 的前置条件。
>
> **Gate与构建边界**：当前参考/生产候选中的`BOARD_GATE_*`和`BOARD_POWER_OUTPUT_ALLOWED`表示最终验收证据，不是Bring-up临时开关。G0~G15必须使用独立受限Bring-up构建，仅开放当前Gate所需的最小硬件能力；严禁为了进入某个台架阶段提前把尚未完成的最终验收Gate置1。

---""",
)

old_doc = ROOT / "docs/46-v0.10.3-审阅问题补强与验证记录.md"
new_doc = ROOT / "docs/47-v0.10.3-审阅问题补强与验证记录.md"
if not old_doc.exists() or new_doc.exists():
    raise SystemExit("审阅记录重编号前置条件不满足")
review_text = old_doc.read_text(encoding="utf-8")
review_text = review_text.replace(
    "# 46 · v0.10.3 审阅问题补强与验证记录",
    "# 47 · v0.10.3 审阅问题补强与验证记录",
    1,
)
review_text += """

## 最终收尾补强

- `relay_holdoff_sequence`不再使用`0`作为“未捕获”哨兵；新增独立`relay_holdoff_sequence_valid`。即使32位ADC发布序号长期运行后回绕到0，HOLD_OFF仍可按无符号差值等待两个新DMA发布块，不会永久卡死。
- `AURORA_HOST_TEST_POWER_GATES_OPEN`增加编译期保险：只有同时定义`AURORA_HOST_TEST`的Host目标才能启用；目标固件误配该宏会直接`#error`。
- 当前仓库的`BOARD_GATE_*`与`BOARD_POWER_OUTPUT_ALLOWED`明确定义为**最终验收证据门**，不是Bring-up阶段的临时开关。低压/分阶段板测使用46号路线中的独立受限Bring-up构建，不通过提前置1最终Gate来绕过验收。
- README同步修正`run_checks.py`的fail-closed行为，以及旧30字节能量字段“布局不变、语义恢复120W电池侧充电量、当前质量为ESTIMATED”的事实。

因此当前发布措辞统一为：`SOFTWARE CANDIDATE FOR BRING-UP INTEGRATION / PRODUCTION BUILD POWER GATES STILL LOCKED`。
"""
new_doc.write_text(review_text, encoding="utf-8", newline="\n")
old_doc.unlink()

replace_once(
    "docs/00-文档索引.md",
    """| 46 | [v0.10.3-新工程分阶段移植与板级验证路线](46-v0.10.3-新工程分阶段移植与板级验证路线.md) | 从空工程按G0~G15逐级移植，包含抑制项、测试步骤、仪器、PASS门槛、故障注入和额定功率放行规则 |
| 程序代码分析 | [阅读索引](程序代码分析/00-阅读索引.md) | 按模块学习300W源码、调用关系和调试切入点 |

`docs/reference/`保存120W Checklist和派生迁移表等证据文件；不保存旧HT32源码、闭源MPPT库或原始录音。
- `docs/46-v0.10.3-审阅问题补强与验证记录.md`：二次审核发现的Relay/ADC/能量与预充问题闭环。""",
    """| 46 | [v0.10.3-新工程分阶段移植与板级验证路线](46-v0.10.3-新工程分阶段移植与板级验证路线.md) | 从空工程按G0~G15逐级移植，包含抑制项、测试步骤、仪器、PASS门槛、故障注入和额定功率放行规则 |
| 47 | [v0.10.3-审阅问题补强与验证记录](47-v0.10.3-审阅问题补强与验证记录.md) | 二次审核发现的Relay/ADC/能量、序号回绕、Host门禁与Bring-up边界闭环 |
| 程序代码分析 | [阅读索引](程序代码分析/00-阅读索引.md) | 按模块学习300W源码、调用关系和调试切入点 |

`docs/reference/`保存120W Checklist和派生迁移表等证据文件；不保存旧HT32源码、闭源MPPT库或原始录音。""",
)

print("v0.10.3最终收尾修改已应用")
