#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
content = r'''#!/usr/bin/env python3
"""严格检查工程文本编码，防止UTF-8/GBK误转和常见乱码进入仓库。"""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
SUFFIXES = {".c", ".h", ".py", ".md", ".yml", ".yaml", ".cmake", ".txt", ".sct", ".uvprojx", ".svg"}
SKIP_DIRS = {".git", "build-gcc", "build-clang", "build-sanitize", "__pycache__"}
BAD = (
    "\ufffd",
    "\u951f\u65a4\u62f7",
    "\u70eb\u70eb\u70eb",
    "\u5c6f\u5c6f\u5c6f",
    "\u00ef\u00bf\u00bd",
    "\u00e2\u20ac\u2122",
)
errors = []

for candidate in ROOT.rglob("*"):
    if not candidate.is_file() or candidate.suffix.lower() not in SUFFIXES:
        continue
    if any(part in SKIP_DIRS for part in candidate.parts):
        continue
    if candidate.parent == ROOT / "tools" and candidate.name.startswith("_v0103_"):
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
            errors.append(f"{candidate.relative_to(ROOT)}: mojibake token U+{' '.join(f'{ord(ch):04X}' for ch in token)}")

if errors:
    print("TEXT ENCODING CHECK: FAIL")
    print("\n".join(errors))
    sys.exit(1)
print("TEXT ENCODING CHECK: PASS")
'''
(root / "tools/check_text_encoding.py").write_text(content, encoding="utf-8", newline="\n")

contract_path = root / "tests/test_v0103_contract.py"
contract = contract_path.read_text(encoding="utf-8")
old = '        self.assertIn("sample->sequence == ctx->relay_holdoff_sequence", power)\n'
new = ('        self.assertIn("AURORA_RELAY_POST_OFF_MIN_BLOCKS", power)\n'
       '        self.assertIn("sample->sequence - ctx->relay_holdoff_sequence", power)\n')
if old in contract:
    contract = contract.replace(old, new, 1)
elif new not in contract:
    raise SystemExit("v0.10.3 Relay holdoff contract pattern not found")
contract_path.write_text(contract, encoding="utf-8", newline="\n")

compat_path = root / "tests/app.h"
compat = compat_path.read_text(encoding="utf-8")
compat = compat.replace(
    "zero_cal_failed, true,\n        AURORA_MODE_BATTERY",
    "zero_cal_failed, true, ctx->relay_generation,\n        AURORA_MODE_BATTERY",
)
compat = compat.replace(
    "zero_cal_failed, true,\n            AURORA_MODE_BATTERY",
    "zero_cal_failed, true, ctx->relay_generation,\n            AURORA_MODE_BATTERY",
)
compat = compat.replace("fresh.sequence++;", "fresh.sequence += AURORA_RELAY_POST_OFF_MIN_BLOCKS;")
old_holdoff = '''    if (command.state == AURORA_POWER_RELAY_HOLD_OFF)
    {
        aurora_measurement_t fresh = *sample;
        fresh.sequence += AURORA_RELAY_POST_OFF_MIN_BLOCKS;
        fresh.timestamp_ms = now_ms + AURORA_RELAY_PWM_OFF_DECAY_MS;'''
new_holdoff = '''    if (command.state == AURORA_POWER_RELAY_HOLD_OFF)
    {
        aurora_measurement_t fresh = *sample;
        // 历史夹具没有Runtime，这里显式模拟“物理关PWM后记录基准”的生产握手。
        ctx->relay_holdoff_sequence = sample->sequence;
        ctx->state_since_ms = now_ms;
        fresh.sequence += AURORA_RELAY_POST_OFF_MIN_BLOCKS;
        fresh.timestamp_ms = now_ms + AURORA_RELAY_PWM_OFF_DECAY_MS;'''
if old_holdoff in compat:
    compat = compat.replace(old_holdoff, new_holdoff, 1)
elif new_holdoff not in compat:
    raise SystemExit("tests/app.h: HOLD_OFF compatibility block not found")
if compat.count("true, ctx->relay_generation") < 2:
    raise SystemExit("tests/app.h: Relay generation compatibility update incomplete")
compat_path.write_text(compat, encoding="utf-8", newline="\n")

test_path = root / "tests/test_v0103.c"
test = test_path.read_text(encoding="utf-8")
test = test.replace("sample.timestamp_ms = 1122U;", "sample.timestamp_ms = 1070U;")
test = test.replace("AURORA_MODE_BATTERY, 48000U, 30000U, 1122U);",
                    "AURORA_MODE_BATTERY, 48000U, 30000U, 1070U);")
test_path.write_text(test, encoding="utf-8", newline="\n")

# 普通Host目标保持真实生产门禁=0；正向Relay闭合由专用v0.10.3目标使用Host-only门禁覆盖验证。
legacy_path = root / "tests/test_main.c"
legacy = legacy_path.read_text(encoding="utf-8")
old_block = '''    aurora_service_poll(&service);
    CHECK(mock_relay());
    CHECK(!mock_pwm_active());

    /* 重新断开后制造18V压差；即使APP错误请求闭合，Service也必须拒绝。 */'''
new_block = '''    aurora_service_poll(&service);
    CHECK(!mock_relay()); // 生产功率门仍为0，即使压差合格也禁止物理吸合。
    CHECK(!mock_pwm_active());

    /* 继续制造18V压差；无论门禁还是压差均应保持Relay断开。 */'''
if old_block in legacy:
    legacy = legacy.replace(old_block, new_block, 1)
elif new_block not in legacy:
    raise SystemExit("tests/test_main.c: production Relay gate assertion block not found")
legacy_path.write_text(legacy, encoding="utf-8", newline="\n")

print("UTF-8 gate, Relay Host timing and production gate contracts normalized")
