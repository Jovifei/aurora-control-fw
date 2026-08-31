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
# 使用Unicode转义生成检测词，避免检查器源码本身含有待检乱码字面量。
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
    # 本轮一次性生成器会在最终提交后删除；不要让检测词模板反向命中它们自身。
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
print("UTF-8 gate made self-safe")
