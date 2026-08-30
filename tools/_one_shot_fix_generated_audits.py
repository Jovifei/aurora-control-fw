#!/usr/bin/env python3
"""Apply final comment-only review fixes and write the reusable style checker."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: Path) -> str:
    return path.read_bytes().decode("utf-8", errors="strict")


def write(path: Path, text: str) -> None:
    path.write_bytes(text.replace("\r\n", "\n").replace("\r", "\n").encode("utf-8"))


replacements = {
    "driver/inc/board_config.h": [
        (
            " * PV_I：3mΩ分流器×内部16倍OPA，3.3V/12bit下理论约16.79mA/码。\n"
            " * 300W新板没有旧120W的VDDA/2电流偏置，零电流理论码接近0；实际零点必须在PWM/Relay关闭时运行时校准。\n"
            " * 正向电流使内部OPA输出升高，因此极性候选为+1；BOARD_GATE_ANALOG_CALIBRATED保持0直到实板方向和比例校准完成。\n",
            " * PV_I：3mΩ分流器×内部16倍OPA，3.3V/12bit下理论约16.79mA/码。\n"
            " * drv_comp.c 当前把两路OPA配置为AVDD/2共模；板级zero_code仅是启动名义值，真正控制零点由PWM/Relay关闭时的运行时校准覆盖。\n"
            " * 正向极性、实际中点、增益和温漂必须经台架冻结；BOARD_GATE_ANALOG_CALIBRATED保持0直到证据闭环。\n",
        )
    ],
    "driver/src/drv_board.c": [
        (
            "        /* 300W新板内部OPA×16没有旧VDDA/2偏置；正向PV电流候选为ADC码上升，最终以低流实测冻结极性。 */",
            "        /* OPA1使用内部×16并由drv_comp.c选择AVDD/2共模；这里给出名义增益/极性，实际零点由运行时校准建立。 */",
        )
    ],
    "app/inc/app_config.h": [
        (
            "/* 新300W理论零点接近0；该宽窗口仅用于启动诊断/Host兼容，最终量产边界必须由实板冻结。 */",
            "/* OPA当前使用AVDD/2共模；该窗口覆盖名义中点及启动偏差，最终量产边界必须由实板零点/温漂统计冻结。 */",
        )
    ],
    "docs/32-REF-120W与300W模拟采样硬件对比.md": [
        (
            "3mΩ×内部OPA16倍，无中点偏置",
            "3mΩ×内部OPA16倍；当前固件选择AVDD/2共模"
        ),
        (
            "zero默认改0、极性候选+1，必须实板校准",
            "名义zero仅作启动占位，运行时重新校零；极性/增益必须实板校准"
        ),
        (
            "旧板零电流靠近ADC中点的假设不再成立。",
            "当前固件同样使用半电源共模，但偏置来源改为片内OPA；不得直接照搬旧板实测零点。"
        ),
    ],
}

for rel, pairs in replacements.items():
    path = ROOT / rel
    if not path.exists():
        continue
    text = read(path)
    for old, new in pairs:
        text = text.replace(old, new)
    write(path, text)

checker = r'''#!/usr/bin/env python3
"""Check APP/Driver C/H file headers and function header field style."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
errors = []
files = sorted(list(ROOT.glob("app/**/*.c")) + list(ROOT.glob("app/**/*.h")) + list(ROOT.glob("driver/**/*.c")) + list(ROOT.glob("driver/**/*.h")))
definition_re = re.compile(r"(?m)^(?P<sig>(?:(?:static|inline|__attribute__\s*\(\([^\n]*\)\)|[A-Za-z_]\w*|\*|\s)+?)\b(?P<name>[A-Za-z_]\w*)\s*\([^;{}]*?\))\s*\{")
for path in files:
    text = path.read_text(encoding="utf-8", errors="strict")
    rel = path.relative_to(ROOT)
    for field in ("File", "Layer", "Description", "Call Path", "Safety Note"):
        if not re.search(rf"^ \* {re.escape(field)}\s+:", text, re.M):
            errors.append(f"missing file field {field}: {rel}")
    if path.suffix == ".c":
        for match in definition_re.finditer(text):
            name = match.group("name")
            if name in {"if", "for", "while", "switch"}:
                continue
            prefix = text[max(0, match.start() - 2000):match.start()]
            blocks = prefix.rsplit("/*---------------------------------------------------------------------------*", 1)
            nearest = blocks[-1] if len(blocks) > 1 else ""
            if not re.search(rf"\*\s*Name\s*:.*\b{re.escape(name)}\b", nearest, re.S):
                errors.append(f"missing function header: {rel}:{name}")
if errors:
    print("\n".join(errors), file=sys.stderr)
    raise SystemExit(1)
print(f"comment style PASS: {len(files)} files")
'''
write(ROOT / "tools" / "check_comment_style.py", checker)
