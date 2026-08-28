#!/usr/bin/env python3
"""用Clang Cortex-M0+前端检查v0.8.3 Driver与APP目标入口源码。"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
clang = shutil.which("clang")
if clang is None:
    print("TARGET SYNTAX CHECK: SKIP (clang not found)")
    sys.exit(0)

sources = sorted((root / "driver/src").glob("*.c")) + [
    root / "app/src/main.c",
    root / "app/src/interrupts.c",
    root / "app/src/debug.c",
]

# GitHub runner的arm-none-eabi Clang没有ARM libc sysroot。
# 这里只为-fsyntax-only提供标准C函数原型，不参与Host/目标链接，也不替代Keil AC6标准库验证。
with tempfile.TemporaryDirectory(prefix="aurora-arm-stub-") as temp_dir:
    stub = Path(temp_dir)
    (stub / "string.h").write_text(
        "#ifndef AURORA_SYNTAX_STRING_H\n"
        "#define AURORA_SYNTAX_STRING_H\n"
        "#include <stddef.h>\n"
        "void *memset(void *s, int c, size_t n);\n"
        "void *memcpy(void *dest, const void *src, size_t n);\n"
        "#endif\n",
        encoding="utf-8",
    )

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
        f"-I{stub}",
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

    for source in sources:
        rel = source.relative_to(root)
        flags = list(common)
        if rel.as_posix() == "app/src/interrupts.c":
            flags.append("-Wno-missing-prototypes")
        print(f"+ target syntax {rel}")
        subprocess.run(flags + [str(rel)], cwd=root, check=True)

print(f"TARGET SYNTAX CHECK: PASS ({len(sources)} files)")
