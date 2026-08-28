#!/usr/bin/env python3
"""用 Clang 的 Cortex-M0+ 前端检查目标端自有驱动与中断源码。"""
from pathlib import Path
import os
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
clang = shutil.which("clang")
runtime_include = None
if clang is None:
    configured_armclang = os.environ.get("AURORA_ARMCLANG")
    fallback_armclang = Path(r"D:\Keil_v5\ARM\ARMCLANG\Bin\armclang.exe")
    if configured_armclang and Path(configured_armclang).is_file():
        clang = configured_armclang
        runtime_include = Path(configured_armclang).parent.parent / "include"
    elif fallback_armclang.is_file():
        clang = str(fallback_armclang)
        runtime_include = fallback_armclang.parent.parent / "include"
    else:
        print("TARGET SYNTAX CHECK: SKIP (clang/armclang not found)")
        sys.exit(0)

sources = sorted((root / "driver/src").glob("*.c")) + [
    root / "app/src/main.c",
    root / "app/src/interrupts.c",
    root / "app/src/debug.c",
]
target = "arm-arm-none-eabi" if runtime_include is not None else "arm-none-eabi"
common = [
    clang,
    f"--target={target}",
    "-mcpu=cortex-m0plus",
    "-mthumb",
    "-std=c11",
    "-ffreestanding",
    "-fsyntax-only",
    "-DUSE_FULL_DDL_DRIVER",
    "-DG32F031xx",
    "-Iapp/inc",
    "-Idriver/inc",
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
if runtime_include is not None:
    common.extend(["-I", str(runtime_include)])

routes = [
    ("bluetooth", []),
    ("debug", ["-DBOARD_USART_MODE=BOARD_USART_MODE_DEBUG", "-DDEBUG_ENABLE=1"]),
]
for route, route_defines in routes:
    for source in sources:
        rel = source.relative_to(root)
        print(f"+ target syntax [{route}] {rel}")
        subprocess.run(common + route_defines + [str(rel)], cwd=root, check=True)

print(f"TARGET SYNTAX CHECK: PASS ({len(sources)} files x {len(routes)} routes)")
