#!/usr/bin/env python3
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    (ROOT / path).write_text(text, encoding="utf-8", newline="\n")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if text.count(old) != 1:
        raise SystemExit(f"{label}: expected exactly one match, got {text.count(old)}")
    return text.replace(old, new, 1)


def sub_once(text: str, pattern: str, repl: str, label: str, flags: int = 0) -> str:
    out, count = re.subn(pattern, repl, text, count=1, flags=flags)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one regex match, got {count}")
    return out

# 1) Remove Relay generation transaction machinery while retaining mandatory HOLD_OFF + applied feedback.
p = read("app/inc/app_types.h")
p = replace_once(p,
    "    uint32_t relay_generation;                       // Relay闭合事务代次；0表示当前没有有效闭合事务。\n",
    "", "app_types relay_generation")
write("app/inc/app_types.h", p)

p = read("app/inc/power_stage.h")
p = replace_once(p,
    "    uint32_t relay_generation;                       // 当前Relay闭合事务代次，0保留为无事务。\n",
    "", "power_stage.h relay_generation")
p = replace_once(p,
    "    uint32_t precharge_low_power_since_ms;           // 低功率且PV明显压降的连续起点。\n",
    "", "power_stage.h low power timer")
p = p.replace("本轮PRECHARGE入口PV电压，mV。", "PRECHARGE入口PV电压，仅诊断，等待G8台架冻结规则。")
p = p.replace("本轮PRECHARGE最低PV电压，mV。", "PRECHARGE最低PV电压，仅诊断。")
p = p.replace("本轮PRECHARGE入口BST_U，mV。", "PRECHARGE入口BST_U，仅诊断。")
p = p.replace("本轮PRECHARGE最高BST_U，mV。", "PRECHARGE最高BST_U，仅诊断。")
p = replace_once(p,
    "                                                  bool relay_applied,\n                                                  uint32_t relay_applied_generation,\n                                                  aurora_operating_mode_t operating_mode,",
    "                                                  bool relay_applied,\n                                                  aurora_operating_mode_t operating_mode,",
    "power_stage.h step_ex generation arg")
p = p.replace("// 支持Battery/Demo和Relay代次反馈的扩展入口", "/* Battery/Demo扩展入口；Relay执行反馈使用单一bool。 */")
write("app/inc/power_stage.h", p)

p = read("app/inc/main.h")
p = replace_once(p,
    "    uint32_t relay_applied_generation_feedback;      // Runtime最近一次实际写出Relay GPIO对应的事务代次。\n",
    "", "main.h app generation feedback")
p = replace_once(p,
    "    uint32_t relay_applied_generation;               // 当前物理Relay GPIO状态对应的闭合事务代次。\n",
    "", "main.h runtime generation")
p = p.replace("// 上次参与能量时间轴的ADC发布序号。", "/* 上次参与能量时间轴的ADC发布序号。 */")
p = p.replace("// 上次参与能量时间轴的ADC时间戳。", "/* 上次参与能量时间轴的ADC时间戳。 */")
p = p.replace("// Runtime已在物理关PWM后记录ADC基准序号。", "/* Runtime已在物理关PWM后记录ADC基准序号。 */")
write("app/inc/main.h", p)

p = read("app/inc/app_config.h")
old = """// 低功率必须同时伴随明显PV压降并持续2s，才归类为真实弱光。\n#define AURORA_PRECHARGE_WEAK_POWER_MW              (3000U)\n#define AURORA_PRECHARGE_WEAK_PV_DROOP_MV           (1000L)\n#define AURORA_PRECHARGE_WEAK_HOLD_MS               (2000U)\n"""
p = replace_once(p, old, "", "app_config weak precharge heuristic")
p = p.replace("/* 关PWM后沿用现有20ms故障放能窗口，并等待至少一份更新的ADC快照。 */",
              "/* 关PWM后沿用现有20ms故障放能窗口，并至少跨两个新的完整DMA发布代次。 */")
write("app/inc/app_config.h", p)

p = read("app/src/power_stage.c")
p = sub_once(p,
    r"/\*-{75}\*\n \* Name        : static uint32_t next_relay_generation\(uint32_t current\).*?\n}\n\n",
    "", "remove next_relay_generation", re.S)
p = p.replace(" *               bool relay_applied, uint32_t relay_applied_generation,\n", " *               bool relay_applied,\n")
p = p.replace(" *               zero_cal_ready/failed - PV_I运行时零点状态；relay_applied - Runtime已写Relay GPIO；\n *               relay_applied_generation - 该GPIO状态对应的事务代次；operating_mode - Battery/Demo；\n",
              " *               zero_cal_ready/failed - PV_I运行时零点状态；relay_applied - Runtime已写Relay GPIO；\n *               operating_mode - Battery/Demo；\n")
p = replace_once(p,
    "                           bool relay_applied, uint32_t relay_applied_generation,\n                           aurora_operating_mode_t operating_mode,",
    "                           bool relay_applied, aurora_operating_mode_t operating_mode,",
    "power_stage.c step_ex generation arg")
p = replace_once(p, "            ctx->precharge_low_power_since_ms = 0U;\n", "", "precharge low timer init")
heuristic = re.compile(r"\n        // 低Ppv本身不能证明弱光；还必须相对PRECHARGE入口出现明显PV压降并持续成立。\n        if \(\(sample->pv_power_mw >= 0\).*?\n        }\n\n        if \(elapsed_ms\(now_ms, ctx->state_since_ms\) >= AURORA_PRECHARGE_TIMEOUT_MS\)", re.S)
m = heuristic.search(p)
if not m:
    raise SystemExit("power_stage precharge heuristic block not found")
p = p[:m.start()] + "\n        if (elapsed_ms(now_ms, ctx->state_since_ms) >= AURORA_PRECHARGE_TIMEOUT_MS)" + p[m.end():]
p = p.replace("                ctx->relay_generation = next_relay_generation(ctx->relay_generation);\n", "")
p = p.replace("            ctx->relay_generation = next_relay_generation(ctx->relay_generation);\n", "")
p = p.replace("if (!relay_applied || (relay_applied_generation != ctx->relay_generation))", "if (!relay_applied)")
p = p.replace("if ((operating_mode != AURORA_MODE_BATTERY) || !relay_applied ||\n            (relay_applied_generation != ctx->relay_generation))",
              "if ((operating_mode != AURORA_MODE_BATTERY) || !relay_applied)")
p = p.replace("if ((operating_mode != AURORA_MODE_DEMO_LOAD) || !relay_applied ||\n            (relay_applied_generation != ctx->relay_generation))",
              "if ((operating_mode != AURORA_MODE_DEMO_LOAD) || !relay_applied)")
p = replace_once(p, "    command.relay_generation = ctx->relay_generation;\n", "", "command generation assignment")
p = replace_once(p,
    "        ctx, sample, mppt, charger, protection_safe, zero_cal_ready, zero_cal_failed, true,\n        ctx->relay_generation, AURORA_MODE_BATTERY, AURORA_DEMO_TARGET_VOLTAGE_MV,",
    "        ctx, sample, mppt, charger, protection_safe, zero_cal_ready, zero_cal_failed, true,\n        AURORA_MODE_BATTERY, AURORA_DEMO_TARGET_VOLTAGE_MV,",
    "compat wrapper generation")
p = p.replace("直接单测视为Relay执行已确认。", "直接单测视为Relay执行已确认；生产路径由Runtime提供真实bool反馈。")
if "relay_generation" in p or "relay_applied_generation" in p or "AURORA_PRECHARGE_WEAK_" in p:
    raise SystemExit("power_stage.c: generation or heuristic residue remains")
write("app/src/power_stage.c", p)

p = read("app/src/main.c")
p = p.replace("            runtime->relay_applied_generation = 0U;\n", "")
# Remove stale-generation disconnect branch entirely.
p = sub_once(p,
    r"\n    // 当前物理Relay属于旧事务时先断开，禁止旧true反馈授权新请求。\n    if \(command->relay_enable && runtime->relay_applied &&\n        \(command->relay_generation != runtime->relay_applied_generation\)\)\n    \{.*?\n    \}\n",
    "\n", "main generation mismatch block", re.S)
p = p.replace("            runtime->relay_applied_generation = 0U;\n", "")
p = replace_once(p,
    "        runtime->relay_applied_generation = command->relay_enable ? command->relay_generation : 0U;\n",
    "", "main generation assign on relay write")
p = p.replace("    app->relay_applied_generation_feedback = 0U;\n", "")
p = p.replace("    runtime->relay_applied_generation = 0U;\n", "")
p = p.replace("        runtime->app.relay_applied_generation_feedback = runtime->relay_applied_generation;\n", "")
p = replace_once(p,
    "    // Battery实际传能必须同时匹配当前Relay事务，不能让上一次闭合反馈继续授权新的会话。\n    app->actual_power_transfer =\n        (app->storage.settings.operating_mode == AURORA_MODE_BATTERY) &&\n        (app->power_stage.state == AURORA_POWER_RUN) && app->relay_applied_feedback &&\n        (app->relay_applied_generation_feedback == app->power_command.relay_generation) &&\n        boost_output_active && app->charge_output.allow_charge && app->power_command.pwm_enable &&",
    "    // HOLD_OFF会在任何新闭合请求前物理断Relay；单线程Runtime因此只需确认当前GPIO已落实。\n    app->actual_power_transfer =\n        (app->storage.settings.operating_mode == AURORA_MODE_BATTERY) &&\n        (app->power_stage.state == AURORA_POWER_RUN) && app->relay_applied_feedback &&\n        boost_output_active && app->charge_output.allow_charge && app->power_command.pwm_enable &&",
    "main actual transfer generation")
p = replace_once(p,
    "        aurora_measurement_zero_cal_failed(&app->measurement), app->relay_applied_feedback,\n        app->relay_applied_generation_feedback, app->storage.settings.operating_mode,",
    "        aurora_measurement_zero_cal_failed(&app->measurement), app->relay_applied_feedback,\n        app->storage.settings.operating_mode,",
    "main step_ex generation arg")
if "relay_generation" in p or "relay_applied_generation" in p:
    raise SystemExit("main.c: generation residue remains")
write("app/src/main.c", p)

# 2) Keep v0.10.2 legacy 30-byte telemetry semantics (PV measured ledger). Charge-est remains internal Flash v3 data.
p = read("app/src/protocol.c")
p = p.replace("settings->charge_est_daily_energy_wh", "settings->daily_energy_wh")
p = p.replace("settings->charge_est_lifetime_energy_wh", "settings->lifetime_energy_wh")
write("app/src/protocol.c", p)

# 3) Update tests for bool-only Relay feedback, simple PRECHARGE classification, and PV legacy telemetry semantics.
p = read("tests/app.h")
p = p.replace(", true, ctx->relay_generation,\n        AURORA_MODE_BATTERY", ", true,\n        AURORA_MODE_BATTERY")
p = p.replace(", true, ctx->relay_generation,\n            AURORA_MODE_BATTERY", ", true,\n            AURORA_MODE_BATTERY")
p = p.replace("20ms和一个新序号", "20ms和两个新发布代次")
if "relay_generation" in p:
    raise SystemExit("tests/app.h: generation residue remains")
write("tests/app.h", p)

p = read("tests/test_v0103.c")
# Generic calls with false/true + literal generation 0.
p = re.sub(r",\s*(false|true),\s*0U,\s*(AURORA_MODE_(?:BATTERY|DEMO_LOAD))", r", \1, \2", p)
# Replace the generation-specific post-settle check with bool-only applied feedback behavior.
old = """    CHECK(command.relay_generation != 0U);\n\n    sample.timestamp_ms = 1070U;\n    command = aurora_power_stage_step_ex(&ctx, &sample, &mppt, &charger, true, true, false,\n                                         true, command.relay_generation - 1U,\n                                         AURORA_MODE_BATTERY, 48000U, 30000U, 1070U);\n    CHECK(command.state == AURORA_POWER_RELAY_SETTLE);\n    CHECK(ctx.delta_ok_since_ms == 0U);\n"""
new = """\n    sample.timestamp_ms = 1070U;\n    command = aurora_power_stage_step_ex(&ctx, &sample, &mppt, &charger, true, true, false,\n                                         false, AURORA_MODE_BATTERY, 48000U, 30000U, 1070U);\n    CHECK(command.state == AURORA_POWER_RELAY_SETTLE);\n    CHECK(ctx.delta_ok_since_ms == 0U);\n\n    sample.timestamp_ms = 1071U;\n    command = aurora_power_stage_step_ex(&ctx, &sample, &mppt, &charger, true, true, false,\n                                         true, AURORA_MODE_BATTERY, 48000U, 30000U, 1071U);\n    CHECK(command.state == AURORA_POWER_RELAY_SETTLE);\n    CHECK(ctx.delta_ok_since_ms == 1071U);\n"""
p = replace_once(p, old, new, "test generation block")
p = p.replace("test_holdoff_requires_two_new_blocks_and_matching_generation", "test_holdoff_requires_two_new_blocks_and_applied_feedback")
p = p.replace("验证Relay闭合需两个关波后新ADC发布，并拒绝过期Relay事务反馈。",
              "验证Relay闭合需两个关波后新ADC发布，且只有Runtime已落实GPIO后才开始100ms稳定计时。")
# Replace weak-light heuristic test with approved deterministic classification.
pattern = r"/\*-{75}\*\n \* Name        : static void test_weak_light_requires_voltage_droop\(void\).*?\n}\n\n(?=/\*-{75}\*\n \* Name        : static void test_demo_low_residual_bus_and_gate_path)"
replacement = '''/*---------------------------------------------------------------------------*\n * Name        : static void test_precharge_timeout_and_weak_pv_are_separate(void)\n * Input       : 无\n * Output      : 无\n * Description : PV仍>=13V时30s预充失败必须算BUS路径失败；只有跌出13V才归类弱光且不耗重试。\n *---------------------------------------------------------------------------*/\nstatic void test_precharge_timeout_and_weak_pv_are_separate(void)\n{\n    aurora_power_stage_ctx_t ctx;\n    aurora_measurement_t sample = valid_sample(1U, AURORA_PRECHARGE_TIMEOUT_MS);\n    aurora_mppt_output_t mppt = {0};\n    aurora_charge_output_t charger = {0};\n    aurora_power_command_t command;\n\n    aurora_power_stage_init(&ctx, 0U);\n    ctx.state = AURORA_POWER_PRECHARGE;\n    ctx.state_since_ms = 0U;\n    sample.bus_voltage_mv = 30000;\n    sample.pv_voltage_mv = 14000;\n    sample.pv_power_mw = 1000;\n    command = aurora_power_stage_step_ex(&ctx, &sample, &mppt, &charger, true, true, false,\n                                         false, AURORA_MODE_BATTERY, 48000U, 30000U,\n                                         AURORA_PRECHARGE_TIMEOUT_MS);\n    CHECK(command.state == AURORA_POWER_FAULT);\n    CHECK(ctx.last_failure_reason == AURORA_START_FAIL_BUS_PRECHARGE_TIMEOUT);\n    CHECK(ctx.precharge_failure_count == 1U);\n\n    aurora_power_stage_init(&ctx, 0U);\n    ctx.state = AURORA_POWER_PRECHARGE;\n    ctx.state_since_ms = 0U;\n    sample.pv_voltage_mv = AURORA_PV_START_MIN_MV - 1L;\n    sample.timestamp_ms++;\n    command = aurora_power_stage_step_ex(&ctx, &sample, &mppt, &charger, true, true, false,\n                                         false, AURORA_MODE_BATTERY, 48000U, 30000U,\n                                         sample.timestamp_ms);\n    CHECK(command.state == AURORA_POWER_WAIT_PV);\n    CHECK(ctx.last_failure_reason == AURORA_START_FAIL_PV_WEAK);\n    CHECK(ctx.precharge_failure_count == 0U);\n}\n\n'''
p = sub_once(p, pattern, replacement, "replace weak heuristic test", re.S)
# Legacy telemetry must remain PV ledger as in v0.10.2.
p = p.replace("test_legacy_energy_fields_keep_charge_semantics", "test_legacy_energy_fields_keep_v0102_pv_semantics")
p = p.replace("验证旧30字节布局不变，daily/lifetime字段继续表达电池侧ESTIMATED充电量。",
              "验证旧30字节布局与v0.10.2 PV发电量语义均保持不变；charge_est继续作为内部独立账本。")
p = p.replace("CHECK(frame.data[4] == 33U);", "CHECK(frame.data[4] == 111U);")
p = p.replace("CHECK(frame.data[15] == 44U);", "CHECK(frame.data[15] == 222U);")
p = p.replace("CHECK(frame.data[20] == 33U);", "CHECK(frame.data[20] == 111U);")
p = p.replace("test_holdoff_requires_two_new_blocks_and_matching_generation();", "test_holdoff_requires_two_new_blocks_and_applied_feedback();")
p = p.replace("test_weak_light_requires_voltage_droop();", "test_precharge_timeout_and_weak_pv_are_separate();")
p = p.replace("test_legacy_energy_fields_keep_charge_semantics();", "test_legacy_energy_fields_keep_v0102_pv_semantics();")
# Remaining generation arguments should not exist.
p = re.sub(r",\s*(false|true),\s*[A-Za-z_][A-Za-z0-9_. -]*relay_generation[A-Za-z0-9_. -]*,\s*(AURORA_MODE_(?:BATTERY|DEMO_LOAD))", r", \1, \2", p)
if "relay_generation" in p or "AURORA_PRECHARGE_WEAK_" in p:
    raise SystemExit("test_v0103.c: generation/heuristic residue remains")
write("tests/test_v0103.c", p)

p = read("tests/test_v0103_contract.py")
p = p.replace('        self.assertIn("relay_applied_generation", main_c)\n', '')
p = p.replace('        self.assertIn("AURORA_PRECHARGE_WEAK_PV_DROOP_MV", power_c)\n',
              '        self.assertNotIn("AURORA_PRECHARGE_WEAK_PV_DROOP_MV", config)\n')
p = p.replace('        self.assertIn("settings->charge_est_lifetime_energy_wh", protocol)\n',
              '        self.assertIn("settings->lifetime_energy_wh", protocol)\n        self.assertNotIn("settings->charge_est_lifetime_energy_wh", protocol)\n')
needle = '        self.assertIn("bool relay_applied", power)\n'
p = replace_once(p, needle,
    needle + '        self.assertNotIn("relay_generation", power)\n', "contract no generation")
write("tests/test_v0103_contract.py", p)

# 4) Documentation: align to approved v0.10.3 scope; keep the separately requested G0-G15 bring-up manual.
p = read("docs/45-v0.10.3-安全握手与快故障修复说明.md")
p = p.replace("→ 等待至少一份更新的ADC快照", "→ 至少跨两个新的完整DMA发布代次")
p = p.replace("本版本不修改既有30字节遥测布局；为保持120W产品语义，旧daily/lifetime energy字段发送Flash v3中的电池侧估算充电量。该值没有BAT_I实测证据，必须标记为ESTIMATED；PV输入侧实测能量继续独立保存在Flash v3。",
              "本版本保持v0.10.2既有30字节遥测布局与字段语义：旧daily/lifetime energy字段继续发送PV实测账本 `daily_energy_wh/lifetime_energy_wh`。Flash v3仍独立保存 `charge_est_*` 电池侧估算账本，但本版本不静默替换旧协议字段；若后续需要公开该估算量，应通过新资源或明确版本化协议。")
p = p.replace("3. Battery Relay必须经过20ms Hold-off和更新ADC序号；", "3. Battery Relay必须经过20ms Hold-off并至少跨两个新的DMA发布代次；")
write("docs/45-v0.10.3-安全握手与快故障修复说明.md", p)

p = read("docs/47-v0.10.3-审阅问题补强与验证记录.md")
p = p.replace("- Relay 反馈增加 generation；旧 `true` 不能授权新事务。",
              "- Relay执行反馈收敛为单线程Runtime的 `relay_applied`；任何新闭合请求前都必须经过HOLD_OFF物理断开，因此不再维护额外generation事务代次。")
p = p.replace("- 低 Ppv 只有同时伴随相对 PRECHARGE 入口至少1V的PV压降并持续2s，才归类为 `PV_WEAK`；高Voc无BUS进展仍按功率路径失败有限重试。",
              "- PRECHARGE分类恢复定稿规则：只有PV跌出13V启动窗口才归类 `PV_WEAK`；只要PV仍在启动窗口，30s无法建立BUS均压一律 `BUS_PRECHARGE_TIMEOUT`。BUS入口/最大值继续仅作G8/G9诊断证据，不提前写无实测依据的保护阈值。")
p = p.replace("- 旧30字节帧布局不变，daily/lifetime energy恢复120W兼容的电池侧充电量语义，发送 `charge_est_*`，质量明确为 ESTIMATED；PV实测账本继续保存在Flash v3。",
              "- 旧30字节帧布局和v0.10.2字段语义均保持不变：daily/lifetime继续发送PV实测 `daily_energy_wh/lifetime_energy_wh`；`charge_est_*`继续保存在Flash v3内部账本，后续若公开需使用新资源或版本化协议。")
insert = """\n## 定稿范围再收敛\n\n- 移除后续收尾阶段加入的 `relay_generation`：当前Runtime是Relay GPIO唯一写者，且Battery/Demo每次闭合前都强制经过HOLD_OFF物理断开，因此不存在需要事务代次解决的可达旧ACK路径；保留 `relay_applied` + 100ms落实超时即可。\n- 注意：ADC `relay_holdoff_sequence==0` 的回绕哨兵问题与Relay generation没有因果关系。独立 `relay_holdoff_sequence_valid` 修复继续保留，因为ADC sequence本身允许自然回绕到0。\n- 移除没有G8/G9台架依据的“低功率+1V压降+2s”PRECHARGE弱光启发式，避免功率路径故障被提前归为弱光而绕过有限重试。\n- 恢复v0.10.2旧30字节PV能量字段语义，避免同一协议布局在v0.10.3静默换账本。\n- 46号G0~G15 Bring-up手册是后续用户明确要求的独立板级执行文档，非固件可执行代码，继续保留。\n\n"""
p = p.replace("## 自动化验证矩阵\n", insert + "## 自动化验证矩阵\n")
write("docs/47-v0.10.3-审阅问题补强与验证记录.md", p)

p = read("README.md")
p = p.replace("本版本不修改3A CC、12A PV限流和BST_U分压BOM，也不新增OTA/IAP代码。旧30字节遥测**布局不变**；daily/lifetime能量字段恢复120W兼容的电池侧充电量语义，当前硬件无BAT_I，因此该值明确为ESTIMATED。",
              "本版本不修改3A CC、12A PV限流和BST_U分压BOM，也不新增OTA/IAP代码。旧30字节遥测**布局与v0.10.2 PV能量字段语义均保持不变**；`charge_est_*`继续作为Flash v3内部独立估算账本，后续如需对外提供应使用新资源或明确版本化协议。")
write("README.md", p)

# Ensure removed scope did not survive anywhere important.
for path in ["app/inc/app_types.h", "app/inc/main.h", "app/inc/power_stage.h", "app/src/main.c", "app/src/power_stage.c", "tests/app.h", "tests/test_v0103.c"]:
    text = read(path)
    if "relay_generation" in text or "relay_applied_generation" in text:
        raise SystemExit(f"{path}: relay generation residue")
for path in ["app/inc/app_config.h", "app/src/power_stage.c", "tests/test_v0103.c"]:
    if "AURORA_PRECHARGE_WEAK_" in read(path):
        raise SystemExit(f"{path}: weak heuristic residue")

# Remove the one-shot helper/workflow from the final tree before committing.
for rel in ["tools/_v0103_plan_converge.py", ".github/workflows/_v0103_plan_converge.yml"]:
    candidate = ROOT / rel
    if candidate.exists():
        candidate.unlink()

subprocess.run(["git", "diff", "--check"], cwd=ROOT, check=True)
subprocess.run(["python", "tools/run_checks.py"], cwd=ROOT, check=True)
subprocess.run(["git", "config", "user.name", "Jovifei"], cwd=ROOT, check=True)
subprocess.run(["git", "config", "user.email", "110970864+Jovifei@users.noreply.github.com"], cwd=ROOT, check=True)
subprocess.run(["git", "add", "-A"], cwd=ROOT, check=True)
subprocess.run(["git", "commit", "-m", "修复：按定稿收敛 v0.10.3 Relay、预充与旧协议边界"], cwd=ROOT, check=True)
subprocess.run(["git", "push", "origin", "HEAD:codex/v0.10.3-safety-handshake-closeout"], cwd=ROOT, check=True)
