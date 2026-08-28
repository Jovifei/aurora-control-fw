#!/usr/bin/env python3
"""检查首方C代码的目录、函数头注释、缩进和文件级定义顺序。"""
from __future__ import annotations

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOTS = [
    ROOT / "app/src",
    ROOT / "board",
    ROOT / "driver/src",
    ROOT / "service",
    ROOT / "project",
    ROOT / "tests",
]
HEADER_ROOTS = [
    ROOT / "app/inc",
    ROOT / "board",
    ROOT / "driver/inc",
    ROOT / "service",
    ROOT / "tests",
]
BANNER_END = "*---------------------------------------------------------------------------*/"
errors: list[str] = []


def rel(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def previous_nonblank(lines: list[str], index: int) -> int | None:
    index -= 1
    while index >= 0 and not lines[index].strip():
        index -= 1
    return index if index >= 0 else None


def looks_like_function_start(lines: list[str], index: int) -> bool:
    """识别签名末行后紧跟左花括号的函数定义，避免把控制语句当函数。"""
    if not lines[index].strip().endswith(")"):
        return False
    next_index = index + 1
    while next_index < len(lines) and not lines[next_index].strip():
        next_index += 1
    if next_index >= len(lines) or lines[next_index].strip() != "{":
        return False

    start = index
    while start > 0:
        prior = lines[start - 1].strip()
        if (not prior or prior.endswith((";", "{", "}", ":")) or prior.startswith("#") or
                prior.endswith("*/") or prior.startswith("//")):
            break
        start -= 1
    signature = " ".join(line.strip() for line in lines[start : index + 1])
    if re.match(r"^(?:else\s+)?(if|for|while|switch)\s*\(", signature):
        return False
    return re.search(r"\b[A-Za-z_]\w*\s*\([^;]*\)$", signature) is not None


def check_banner(lines: list[str], signature_end: int, path: Path) -> None:
    signature_start = signature_end
    while signature_start > 0:
        prior = lines[signature_start - 1].strip()
        if (not prior or prior.endswith((";", "{", "}", ":")) or prior.startswith("#") or
                prior.endswith("*/") or prior.startswith("//")):
            break
        signature_start -= 1

    end_index = previous_nonblank(lines, signature_start)
    if end_index is None or lines[end_index].strip() != BANNER_END:
        errors.append(f"{rel(path)}:{signature_end + 1}: 函数缺少统一头注释")
        return

    start_index = end_index
    while start_index >= 0 and lines[start_index].strip() != "/*---------------------------------------------------------------------------*":
        start_index -= 1
    if start_index < 0:
        errors.append(f"{rel(path)}:{signature_end + 1}: 函数头注释起始格式错误")
        return

    banner = "\n".join(lines[start_index : end_index + 1])
    for label in ["* Name        :", "* Input       :", "* Output      :", "* Description :"]:
        if label not in banner:
            errors.append(f"{rel(path)}:{signature_end + 1}: 函数头注释缺少字段 {label.strip()}")


def strip_comments_for_braces(line: str, in_block: bool) -> tuple[str, bool]:
    result = ""
    i = 0
    while i < len(line):
        if in_block:
            end = line.find("*/", i)
            if end < 0:
                return result, True
            in_block = False
            i = end + 2
            continue
        if line.startswith("/*", i):
            in_block = True
            i += 2
            continue
        if line.startswith("//", i):
            break
        if line[i] in {'"', "'"}:
            quote = line[i]
            result += " "
            i += 1
            while i < len(line):
                if line[i] == "\\":
                    i += 2
                    continue
                if line[i] == quote:
                    i += 1
                    break
                i += 1
            continue
        result += line[i]
        i += 1
    return result, in_block


def check_source(path: Path) -> None:
    text = path.read_text(encoding="utf-8")
    lines = text.splitlines()

    for line_number, line in enumerate(lines, 1):
        if "\t" in line:
            errors.append(f"{rel(path)}:{line_number}: 禁止Tab缩进")
        if line.rstrip() != line:
            errors.append(f"{rel(path)}:{line_number}: 行尾空白")

    function_ends = [i for i in range(len(lines)) if looks_like_function_start(lines, i)]
    for index in function_ends:
        check_banner(lines, index, path)

    first_public_line: int | None = None
    depth = 0
    in_block = False
    function_signature_lines: set[int] = set(function_ends)
    for index, line in enumerate(lines):
        code, in_block = strip_comments_for_braces(line, in_block)
        stripped = code.strip()

        if depth == 0 and index in function_signature_lines:
            # 向上拼接签名，判断是否以static开头。
            start = index
            while start > 0:
                prior = lines[start - 1].strip()
                if not prior or prior.endswith((";", "{", "}", ":")) or prior.startswith("#"):
                    break
                if prior.endswith(BANNER_END):
                    break
                start -= 1
            signature = " ".join(item.strip() for item in lines[start : index + 1])
            if not signature.startswith("static ") and first_public_line is None:
                first_public_line = index + 1

        if depth == 0 and first_public_line is not None:
            if stripped.startswith("#define "):
                errors.append(
                    f"{rel(path)}:{index + 1}: 宏定义必须位于首个公开函数之前"
                )
            if stripped.startswith("static "):
                errors.append(
                    f"{rel(path)}:{index + 1}: static定义/声明必须集中在文件头"
                )

        depth += code.count("{") - code.count("}")
        if depth < 0:
            depth = 0


def check_header(path: Path) -> None:
    lines = path.read_text(encoding="utf-8").splitlines()
    guard = None
    for line_number, line in enumerate(lines, 1):
        if "\t" in line:
            errors.append(f"{rel(path)}:{line_number}: 禁止Tab缩进")
        if line.rstrip() != line:
            errors.append(f"{rel(path)}:{line_number}: 行尾空白")

        match = re.match(r"\s*#ifndef\s+(\w+)", line)
        if match:
            guard = match.group(1)

        define = re.match(r"\s*#define\s+(\w+)", line)
        if not define:
            continue
        name = define.group(1)
        if name == guard or name.startswith("__cplusplus"):
            continue
        if "/*" in line or "//" in line:
            continue
        prior = previous_nonblank(lines, line_number - 1)
        # 允许一个说明性注释覆盖紧随其后的同组宏定义。
        while prior is not None and lines[prior].lstrip().startswith("#define "):
            prior = previous_nonblank(lines, prior)
        if prior is None or ("*/" not in lines[prior] and "//" not in lines[prior]):
            errors.append(f"{rel(path)}:{line_number}: 宏 {name} 缺少用途注释")


# app必须只有inc与src两个子目录，根目录不得再放.c/.h。
app_root = ROOT / "app"
actual_subdirs = {item.name for item in app_root.iterdir() if item.is_dir()}
if actual_subdirs != {"inc", "src"}:
    errors.append(f"app目录只能包含inc/src，当前为: {sorted(actual_subdirs)}")
for stray in [*app_root.glob("*.c"), *app_root.glob("*.h")]:
    errors.append(f"APP源码未迁移到inc/src: {rel(stray)}")

for source_root in SOURCE_ROOTS:
    for source in sorted(source_root.glob("*.c")):
        check_source(source)
for header_root in HEADER_ROOTS:
    for header in sorted(header_root.glob("*.h")):
        check_header(header)

if errors:
    print("CODE STYLE CHECK: FAIL")
    for error in errors:
        print(f"- {error}")
    sys.exit(1)

print("CODE STYLE CHECK: PASS")
