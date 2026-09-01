#!/usr/bin/env python3
from pathlib import Path

path = Path(__file__).resolve().parents[1] / "tests/app.h"
text = path.read_text(encoding="utf-8")
old = """        // 历史夹具没有Runtime，这里显式模拟“物理关PWM后记录基准”的生产握手。\n        ctx->relay_holdoff_sequence = sample->sequence;\n        ctx->state_since_ms = now_ms;"""
new = """        // 历史夹具没有Runtime，这里显式模拟“物理关PWM后记录基准”的生产握手。\n        ctx->relay_holdoff_sequence = sample->sequence;\n        ctx->relay_holdoff_sequence_valid = true;\n        ctx->state_since_ms = now_ms;"""
if text.count(old) != 1:
    raise SystemExit(f"tests/app.h: expected one legacy holdoff fixture, got {text.count(old)}")
path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")
print("v0.10.3旧Host夹具已适配独立HOLD_OFF有效标志")
