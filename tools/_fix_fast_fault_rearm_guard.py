#!/usr/bin/env python3
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    (ROOT / path).write_text(text, encoding="utf-8", newline="\n")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one match, got {count}")
    return text.replace(old, new, 1)

# 1) Driver COMP exposes all fast comparator sources, distinct from physical ATMR Break source.
p = read("driver/inc/drv_comp.h")
p = replace_once(
    p,
    "bool drv_comp_init(void);\nuint32_t drv_comp_fault_mask(void);\nvoid drv_comp_irq_ack(void);\n",
    "bool drv_comp_init(void);\nuint32_t drv_comp_fault_mask(void);\n"
    "bool drv_comp_fast_fault_source_active(void);\n"
    "void drv_comp_irq_ack(void);\n",
    "drv_comp.h declaration",
)
write("driver/inc/drv_comp.h", p)

p = read("driver/src/drv_comp.c")
anchor = '''uint32_t drv_comp_fault_mask(void)\n{\n    uint32_t mask = 0U;\n    if ((DDL_COMP0_IsActiveFlag_IT(COMP0) != 0U) || (DDL_COMP0_ReadOutputLevel(COMP0) == 0U))\n    {\n        mask |= DRV_FAULT_MOS_OCP;\n    }\n    if ((DDL_COMP1_IsActiveFlag_IT(COMP2) != 0U) || (DDL_COMP1_ReadOutputLevel(COMP2) == 0U))\n    {\n        mask |= DRV_FAULT_PV_OCP;\n    }\n    return mask;\n}\n'''
insert = anchor + '''\n/*---------------------------------------------------------------------------*\n * Name        : bool drv_comp_fast_fault_source_active(void)\n * Input       : 无\n * Output      : true表示COMP0或COMP2任一路实时快速故障源仍有效\n * Description : 与ATMR物理Break源分离；COMP0进入硬件Break，COMP2虽只走软件快速故障桥，\n *               但持续有效时同样必须阻止PWM重新ARM和快速故障恢复。\n *---------------------------------------------------------------------------*/\nbool drv_comp_fast_fault_source_active(void)\n{\n    return (DDL_COMP0_ReadOutputLevel(COMP0) == 0U) ||\n           (DDL_COMP1_ReadOutputLevel(COMP2) == 0U);\n}\n'''
p = replace_once(p, anchor, insert, "drv_comp fast source function")
write("driver/src/drv_comp.c", p)

# 2) PWM arm must never clear Break evidence; it checks all fast sources before and after MOE.
p = read("driver/src/drv_pwm.c")
old = '''bool drv_pwm_arm(void)\n{\n    if (drv_pwm_break_source_active())\n    {\n        return false;\n    }\n\n    /* 源已释放时，允许清理历史粘滞位；历史位不能替代实时故障源判定。 */\n    (void)drv_pwm_clear_break_latch();\n\n    BSP_PWM_Start();\n\n    if ((drv_pwm_break_source_active() != false) || (drv_pwm_break_latched() != false) ||\n        (drv_pwm_output_active() == false))\n    {\n        BSP_PWM_Stop();\n        return false;\n    }\n    return DDL_ATMR_IsEnabledAllOutputs(ATMR) != 0U;\n}\n'''
new = '''bool drv_pwm_arm(void)\n{\n    /*\n     * arm()只负责“验证并放行”，绝不在这里清Break锁存。\n     * COMP2不是ATMR物理Break源，但作为软件快速过流源持续有效时同样禁止重新发波。\n     */\n    if (drv_comp_fast_fault_source_active() || drv_pwm_break_latched())\n    {\n        return false;\n    }\n\n    BSP_PWM_Start();\n\n    if (drv_comp_fast_fault_source_active() || drv_pwm_break_latched() ||\n        (drv_pwm_output_active() == false))\n    {\n        BSP_PWM_Stop();\n        return false;\n    }\n    return DDL_ATMR_IsEnabledAllOutputs(ATMR) != 0U;\n}\n'''
p = replace_once(p, old, new, "drv_pwm_arm")
write("driver/src/drv_pwm.c", p)

# 3) Runtime safety/recovery uses all fast comparator sources, not only physical ATMR Break source.
p = read("app/src/main.c")
p = replace_once(
    p,
    "           aurora_protection_is_safe(&runtime->app.protection) && !drv_pwm_break_source_active() &&\n           !drv_pwm_break_latched() && drv_board_power_gate_open() &&\n",
    "           aurora_protection_is_safe(&runtime->app.protection) &&\n           !drv_comp_fast_fault_source_active() && !drv_pwm_break_latched() &&\n           drv_board_power_gate_open() &&\n",
    "safety_still_clear all fast sources",
)
p = replace_once(
    p,
    "    if (drv_pwm_break_source_active())\n    {\n        runtime->fast_ocp_recover_since_ms = 0U;\n        return;\n    }\n",
    "    if (drv_comp_fast_fault_source_active())\n    {\n        /* COMP0或COMP2任一路仍有效都不得启动30s恢复计时。 */\n        runtime->fast_ocp_recover_since_ms = 0U;\n        return;\n    }\n",
    "fast ocp recovery all sources",
)
write("app/src/main.c", p)

# 4) Host mock models COMP0 physical Break and COMP2 software-only source separately.
p = read("tests/mock_driver.h")
p = replace_once(
    p,
    "void mock_set_break(bool active);\nvoid mock_apply_uev(void);\n",
    "void mock_set_break(bool active);\n"
    "void mock_set_comp2_fault(bool active);\n"
    "void mock_apply_uev(void);\n",
    "mock_driver.h comp2 setter",
)
write("tests/mock_driver.h", p)

p = read("tests/mock_driver.c")
p = p.replace("static bool g_break_source;\n", "static bool g_break_source;\nstatic bool g_comp2_source;\n", 1)
p = p.replace("    g_break_source = false;\n    g_break_latched = false;\n", "    g_break_source = false;\n    g_comp2_source = false;\n    g_break_latched = false;\n", 1)
anchor = '''void mock_set_break(bool active)\n{\n    g_break_source = active;\n\n    if (active) {\n        g_break_latched = true;\n        g_pwm_active = false;\n    }\n}\n'''
insert = anchor + '''\n/*---------------------------------------------------------------------------*\n * Name        : void mock_set_comp2_fault(bool active)\n * Input       : active - true表示COMP2/PV快速过流源持续有效\n * Output      : 无\n * Description : COMP2不直接产生ATMR Break锁存，但会作为软件快速故障源阻止PWM重新ARM和恢复。\n *---------------------------------------------------------------------------*/\nvoid mock_set_comp2_fault(bool active)\n{\n    g_comp2_source = active;\n}\n'''
p = replace_once(p, anchor, insert, "mock comp2 setter")
p = replace_once(
    p,
    "    if (g_break_source || g_break_latched) {\n        return false;\n    }\n\n    g_pwm_active = true;\n",
    "    if (g_break_source || g_comp2_source || g_break_latched) {\n        return false;\n    }\n\n    g_pwm_active = true;\n",
    "mock pwm arm all fast sources",
)
p = replace_once(
    p,
    "uint32_t drv_comp_fault_mask(void)\n{\n    return g_break_source ? DRV_FAULT_MOS_OCP : 0U;\n}\n",
    "uint32_t drv_comp_fault_mask(void)\n{\n    uint32_t mask = 0U;\n    if (g_break_source)\n    {\n        mask |= DRV_FAULT_MOS_OCP;\n    }\n    if (g_comp2_source)\n    {\n        mask |= DRV_FAULT_PV_OCP;\n    }\n    return mask;\n}\n\n"
    "bool drv_comp_fast_fault_source_active(void)\n{\n    return g_break_source || g_comp2_source;\n}\n",
    "mock comp fault mask and all-source API",
)
write("tests/mock_driver.c", p)

# 5) Add behavior regressions for persistent COMP2 and transient Break latch race.
p = read("tests/test_v0103.c")
anchor = '''/*---------------------------------------------------------------------------*\n * Name        : static void test_runtime_captures_post_pwm_off_baseline(void)'''
insert_tests = '''/*---------------------------------------------------------------------------*\n * Name        : static void test_comp2_persistent_source_blocks_rearm_and_recovery(void)\n * Input       : 无\n * Output      : 无\n * Description : COMP2虽不直连ATMR Break，但持续低时必须禁止PWM重新ARM，并阻止FAST_PV_OCP的30s恢复计时。\n *---------------------------------------------------------------------------*/\nstatic void test_comp2_persistent_source_blocks_rearm_and_recovery(void)\n{\n    aurora_runtime_t runtime;\n    uint32_t sequence;\n\n    mock_reset();\n    CHECK(aurora_runtime_init(&runtime));\n    CHECK(drv_pwm_prepare_arm_zero(&sequence));\n    mock_apply_uev();\n    CHECK(drv_pwm_zero_duty_applied());\n\n    mock_set_comp2_fault(true);\n    CHECK(!drv_pwm_arm());\n\n    /* 模拟运行期COMP2快速故障已锁存；持续源存在时超过30s也不得恢复。 */\n    runtime.pwm_arm_state = AURORA_RUNTIME_PWM_ARM_ACTIVE;\n    aurora_runtime_isr_comparator_fault(&runtime, AURORA_FAULT_FAST_PV_OCP);\n    aurora_runtime_poll(&runtime);\n    CHECK((aurora_protection_fault_mask(&runtime.app.protection) & AURORA_FAULT_FAST_PV_OCP) != 0U);\n    mock_advance_ms(AURORA_FAST_OCP_RECOVER_DELAY_MS + 1000U);\n    aurora_runtime_poll(&runtime);\n    CHECK((aurora_protection_fault_mask(&runtime.app.protection) & AURORA_FAULT_FAST_PV_OCP) != 0U);\n    CHECK(runtime.fast_ocp_recover_since_ms == 0U);\n\n    mock_set_comp2_fault(false);\n}\n\n/*---------------------------------------------------------------------------*\n * Name        : static void test_pwm_arm_preserves_transient_break_latch(void)\n * Input       : 无\n * Output      : 无\n * Description : 模拟COMP0短脉冲已释放但BRK锁存尚未消费，arm必须拒绝且不得主动清除该硬件证据。\n *---------------------------------------------------------------------------*/\nstatic void test_pwm_arm_preserves_transient_break_latch(void)\n{\n    aurora_runtime_t runtime;\n    uint32_t sequence;\n\n    mock_reset();\n    CHECK(aurora_runtime_init(&runtime));\n    CHECK(drv_pwm_prepare_arm_zero(&sequence));\n    mock_apply_uev();\n    CHECK(drv_pwm_zero_duty_applied());\n\n    mock_set_break(true);\n    mock_set_break(false);\n    CHECK(drv_pwm_break_latched());\n    CHECK(!drv_pwm_arm());\n    CHECK(drv_pwm_break_latched());\n    CHECK(!mock_pwm_active());\n}\n\n'''
if anchor not in p:
    raise SystemExit("test insertion anchor missing")
p = p.replace(anchor, insert_tests + anchor, 1)
p = replace_once(
    p,
    "    test_break_uses_software_arm_state();\n",
    "    test_break_uses_software_arm_state();\n"
    "    test_comp2_persistent_source_blocks_rearm_and_recovery();\n"
    "    test_pwm_arm_preserves_transient_break_latch();\n",
    "test main new fast-fault tests",
)
write("tests/test_v0103.c", p)

# 6) Strengthen static contracts so target/mock cannot silently diverge again.
p = read("tests/test_v0103_contract.py")
needle = "class V0103ContractTests(unittest.TestCase):\n"
if needle not in p:
    raise SystemExit("contract class anchor missing")
method = '''    def test_fast_sources_gate_rearm_and_arm_does_not_clear_break(self):\n        main = (ROOT / "app/src/main.c").read_text(encoding="utf-8")\n        pwm = (ROOT / "driver/src/drv_pwm.c").read_text(encoding="utf-8")\n        comp_h = (ROOT / "driver/inc/drv_comp.h").read_text(encoding="utf-8")\n        mock = (ROOT / "tests/mock_driver.c").read_text(encoding="utf-8")\n        self.assertIn("drv_comp_fast_fault_source_active", comp_h)\n        self.assertIn("!drv_comp_fast_fault_source_active()", main)\n        arm = pwm[pwm.index("bool drv_pwm_arm(void)"):pwm.index("bool drv_pwm_output_active(void)")]\n        self.assertIn("drv_comp_fast_fault_source_active()", arm)\n        self.assertIn("drv_pwm_break_latched()", arm)\n        self.assertNotIn("drv_pwm_clear_break_latch", arm)\n        self.assertIn("g_comp2_source", mock)\n        self.assertIn("drv_comp_fast_fault_source_active", mock)\n\n'''
p = p.replace(needle, needle + method, 1)
write("tests/test_v0103_contract.py", p)

# Remove temporary carrier before checking/committing.
for rel in ("docs/.tmp-fast-fault-fix-note", "tools/_fix_fast_fault_rearm_guard.py", ".github/workflows/_fix_fast_fault_rearm_guard.yml"):
    target = ROOT / rel
    if target.exists():
        target.unlink()

subprocess.run(["python", "tools/run_checks.py"], cwd=ROOT, check=True)
subprocess.run(["git", "config", "user.name", "github-actions[bot]"], cwd=ROOT, check=True)
subprocess.run(["git", "config", "user.email", "41898282+github-actions[bot]@users.noreply.github.com"], cwd=ROOT, check=True)
subprocess.run(["git", "add", "-A"], cwd=ROOT, check=True)
subprocess.run(["git", "commit", "-m", "修复：阻止持续快速故障源误重启并保留Break证据"], cwd=ROOT, check=True)
subprocess.run(["git", "push", "origin", "HEAD:codex/fix-fast-fault-rearm-guard"], cwd=ROOT, check=True)
