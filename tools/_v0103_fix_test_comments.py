#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
path = root / "tests/test_v0103.c"
text = path.read_text(encoding="utf-8")

headers = {
"static aurora_measurement_calibration_t unit_calibration(void)": """/*---------------------------------------------------------------------------*
 * Name        : static aurora_measurement_calibration_t unit_calibration(void)
 * Input       : 无
 * Output      : 六通道单位增益标定
 * Description : 构造v0.10.3安全行为测试使用的最小有效测量标定。
 *---------------------------------------------------------------------------*/
static aurora_measurement_calibration_t unit_calibration(void)""",
"static aurora_measurement_t valid_sample(uint32_t sequence, uint32_t now_ms)": """/*---------------------------------------------------------------------------*
 * Name        : static aurora_measurement_t valid_sample(uint32_t sequence,
 *               uint32_t now_ms)
 * Input       : sequence - 测量发布序号；now_ms - 测量时间戳，ms
 * Output      : 可供功率状态机、保护和能量统计使用的完整样本
 * Description : 构造16V PV、48V BAT、47.5V BST与1A PV电流的稳定测试点。
 *---------------------------------------------------------------------------*/
static aurora_measurement_t valid_sample(uint32_t sequence, uint32_t now_ms)""",
"static void test_break_uses_software_arm_state(void)": """/*---------------------------------------------------------------------------*
 * Name        : static void test_break_uses_software_arm_state(void)
 * Input       : 无
 * Output      : 无
 * Description : 模拟硬件Break先撤销MOE、ISR后到，验证ACTIVE仍锁存而WAIT_ZERO只记启动诊断。
 *---------------------------------------------------------------------------*/
static void test_break_uses_software_arm_state(void)""",
"static void test_runtime_captures_post_pwm_off_baseline(void)": """/*---------------------------------------------------------------------------*
 * Name        : static void test_runtime_captures_post_pwm_off_baseline(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证HOLD_OFF的ADC基准只能由Runtime在物理关PWM后建立。
 *---------------------------------------------------------------------------*/
static void test_runtime_captures_post_pwm_off_baseline(void)""",
"static void test_holdoff_requires_two_new_blocks_and_matching_generation(void)": """/*---------------------------------------------------------------------------*
 * Name        : static void test_holdoff_requires_two_new_blocks_and_matching_generation(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证Relay闭合需两个关波后新ADC发布，并拒绝过期Relay事务反馈。
 *---------------------------------------------------------------------------*/
static void test_holdoff_requires_two_new_blocks_and_matching_generation(void)""",
"static void test_holdoff_delta_loss_is_bounded(void)": """/*---------------------------------------------------------------------------*
 * Name        : static void test_holdoff_delta_loss_is_bounded(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证关波后均压丢失会进入有限预充失败，而不是PRECHARGE/HOLD_OFF无限循环。
 *---------------------------------------------------------------------------*/
static void test_holdoff_delta_loss_is_bounded(void)""",
"static void test_weak_light_requires_voltage_droop(void)": """/*---------------------------------------------------------------------------*
 * Name        : static void test_weak_light_requires_voltage_droop(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证高Voc低Ppv不直接视为弱光；低功率需伴随明确PV压降并持续成立。
 *---------------------------------------------------------------------------*/
static void test_weak_light_requires_voltage_droop(void)""",
"static void test_demo_low_residual_bus_and_gate_path(void)": """/*---------------------------------------------------------------------------*
 * Name        : static void test_demo_low_residual_bus_and_gate_path(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证Demo Relay拒绝高BST_U残压，并在Host专用门禁放行后才允许安全闭合。
 *---------------------------------------------------------------------------*/
static void test_demo_low_residual_bus_and_gate_path(void)""",
"static void test_pending_fault_keeps_break_latched(void)": """/*---------------------------------------------------------------------------*
 * Name        : static void test_pending_fault_keeps_break_latched(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证ISR已投递但Protection未消费的快速故障会阻止启动清理路径提前擦除Break锁存。
 *---------------------------------------------------------------------------*/
static void test_pending_fault_keeps_break_latched(void)""",
"static void test_stale_energy_and_adc_timebase(void)": """/*---------------------------------------------------------------------------*
 * Name        : static void test_stale_energy_and_adc_timebase(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证能量只按新ADC时间戳间隔累计，首样本和陈旧样本都不会被墙上时间外推。
 *---------------------------------------------------------------------------*/
static void test_stale_energy_and_adc_timebase(void)""",
"static void test_legacy_energy_fields_keep_charge_semantics(void)": """/*---------------------------------------------------------------------------*
 * Name        : static void test_legacy_energy_fields_keep_charge_semantics(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证旧30字节布局不变，daily/lifetime字段继续表达电池侧ESTIMATED充电量。
 *---------------------------------------------------------------------------*/
static void test_legacy_energy_fields_keep_charge_semantics(void)""",
"int main(void)": """/*---------------------------------------------------------------------------*
 * Name        : int main(void)
 * Input       : 无
 * Output      : 0表示全部v0.10.3二次审阅行为回归通过
 * Description : 顺序执行快速故障、Relay事务、Demo、预充、能量与旧协议兼容测试。
 *---------------------------------------------------------------------------*/
int main(void)""",
}

for signature, replacement in headers.items():
    if replacement in text:
        continue
    count = text.count(signature)
    if count != 1:
        raise SystemExit(f"expected one signature {signature!r}, got {count}")
    text = text.replace(signature, replacement, 1)

path.write_text(text, encoding="utf-8", newline="\n")

# 主修补脚本运行时的三引号会把 \x00 转成真实NUL；这里使用 raw 字符串覆盖成合法的永久检查器。
encoding_gate = r'''#!/usr/bin/env python3
"""严格检查工程文本编码，防止UTF-8/GBK误转和常见乱码进入仓库。"""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
SUFFIXES = {".c", ".h", ".py", ".md", ".yml", ".yaml", ".cmake", ".txt", ".sct", ".uvprojx", ".svg"}
SKIP = {".git", "build-gcc", "build-clang", "build-sanitize", "__pycache__"}
BAD = ("\ufffd", "锟斤拷", "烫烫烫", "屯屯屯", "ï¿½", "â€™")
errors = []

for candidate in ROOT.rglob("*"):
    if not candidate.is_file() or candidate.suffix.lower() not in SUFFIXES:
        continue
    if any(part in SKIP for part in candidate.parts):
        continue
    raw = candidate.read_bytes()
    if raw.startswith(b"\xef\xbb\xbf"):
        errors.append(f"{candidate.relative_to(ROOT)}: UTF-8 BOM")
        continue
    if b"\x00" in raw:
        errors.append(f"{candidate.relative_to(ROOT)}: NUL byte")
        continue
    try:
        decoded = raw.decode("utf-8", errors="strict")
    except UnicodeDecodeError as exc:
        errors.append(f"{candidate.relative_to(ROOT)}: invalid UTF-8: {exc}")
        continue
    for token in BAD:
        if token in decoded:
            errors.append(f"{candidate.relative_to(ROOT)}: mojibake token {token!r}")

if errors:
    print("TEXT ENCODING CHECK: FAIL")
    print("\n".join(errors))
    sys.exit(1)
print("TEXT ENCODING CHECK: PASS")
'''
(root / "tools/check_text_encoding.py").write_text(encoding_gate, encoding="utf-8", newline="\n")
print("v0.10.3 test comments and UTF-8 gate normalized")
