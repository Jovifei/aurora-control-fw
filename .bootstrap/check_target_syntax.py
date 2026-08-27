#!/usr/bin/env python3
"""用 Clang 的 Cortex-M0+ 前端检查目标端自有驱动与中断源码。"""
from pathlib import Path
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
clang = shutil.which("clang")
if clang is None:
    print("TARGET SYNTAX CHECK: SKIP (clang not found)")
    sys.exit(0)

sources = sorted((root / "driver").glob("*.c")) + [
    root / "project/keil/main.c",
    root / "project/keil/interrupts.c",
]
common = [
    clang,
    "--target=arm-none-eabi",
    "-mcpu=cortex-m0plus",
    "-mthumb",
    "-std=c11",
    "-ffreestanding",
    "-fsyntax-only",
    "-DUSE_FULL_DDL_DRIVER",
    "-DG32F031xx",
    "-Iapp",
    "-Iservice",
    "-Idriver",
    "-Iboard",
    "-Ivendor/cmsis/Include",
    "-Ivendor/device/Include",
    "-Ivendor/ddl/Include",
    "-Wall",
    "-Wextra",
    "-Wpedantic",
    "-Wconversion",
    "-Wsign-conversion",
    "-Wshadow",
    "-Wstrict-prototypes",
    "-Wmissing-prototypes",
    "-Werror",
]

for source in sources:
    rel = source.relative_to(root)
    flags = list(common)
    # Cortex-M 向量函数由启动汇编按固定符号名引用，不属于普通跨模块C接口。
    # 只对该向量入口文件关闭 missing-prototypes，其余严格告警保持不变。
    if rel.as_posix() == "project/keil/interrupts.c":
        flags.append("-Wno-missing-prototypes")
    print(f"+ target syntax {rel}")
    subprocess.run(flags + [str(rel)], cwd=root, check=True)

print(f"TARGET SYNTAX CHECK: PASS ({len(sources)} files)")
