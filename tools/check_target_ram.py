#!/usr/bin/env python3
"""按Keil实际C源和O1基线估算G32F031 8KiB SRAM静态预算。"""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[1]
clang = shutil.which("clang")
size_tool = shutil.which("llvm-size") or shutil.which("llvm-size-18") or shutil.which("size")
if clang is None or size_tool is None:
    raise SystemExit("TARGET RAM CHECK: FAIL (clang/size tool not found)")

RAM_TOTAL_BYTES = 8192
MIN_ESTIMATED_FREE_BYTES = 2048
KEIL_RELEASE_OPTIMIZATION = "-O1"

project_path = root / "project/AuroraControl.uvprojx"
startup_path = root / "vendor/device/Source/startup_g32f031.s"
project = ET.parse(project_path)
sources: list[Path] = []
for node in project.findall(".//FilePath"):
    raw = (node.text or "").replace("\\", "/")
    if raw.lower().endswith(".c"):
        source = (root / "project" / raw).resolve()
        if not source.is_file():
            raise SystemExit(f"TARGET RAM CHECK: FAIL (missing Keil source: {source})")
        sources.append(source)

if not sources:
    raise SystemExit("TARGET RAM CHECK: FAIL (no Keil C sources)")

with tempfile.TemporaryDirectory(prefix="aurora-arm-ram-") as temp_dir:
    temp = Path(temp_dir)
    stub = temp / "stub"
    obj_dir = temp / "obj"
    stub.mkdir()
    obj_dir.mkdir()
    (stub / "string.h").write_text(
        "#ifndef AURORA_RAM_STRING_H\n"
        "#define AURORA_RAM_STRING_H\n"
        "#include <stddef.h>\n"
        "void *memset(void *s, int c, size_t n);\n"
        "void *memcpy(void *dest, const void *src, size_t n);\n"
        "void *memmove(void *dest, const void *src, size_t n);\n"
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
        KEIL_RELEASE_OPTIMIZATION,
        "-DUSE_FULL_DDL_DRIVER",
        "-DG32F031xx",
        f"-I{stub}",
        "-Iapp/inc",
        "-Idriver/inc",
        "-Ivendor/cmsis/Include",
        "-Ivendor/device/Include",
        "-Ivendor/ddl/Include",
        "-w",
    ]

    objects: list[Path] = []
    for index, source in enumerate(sources):
        rel = source.relative_to(root)
        obj = obj_dir / f"{index:02d}.o"
        subprocess.run(common + ["-c", str(rel), "-o", str(obj)], cwd=root, check=True)
        objects.append(obj)

    size_result = subprocess.run(
        [size_tool, *[str(obj) for obj in objects]],
        cwd=root,
        check=True,
        text=True,
        capture_output=True,
    )

    data_total = 0
    bss_total = 0
    for line in size_result.stdout.splitlines():
        parts = line.split()
        if len(parts) < 6 or parts[0] == "text":
            continue
        try:
            data_total += int(parts[1])
            bss_total += int(parts[2])
        except ValueError:
            continue

startup = startup_path.read_text(encoding="utf-8")
stack_match = re.search(r"Stack_Size\s+EQU\s+(0x[0-9A-Fa-f]+)", startup)
heap_match = re.search(r"Heap_Size\s+EQU\s+(0x[0-9A-Fa-f]+)", startup)
if stack_match is None or heap_match is None:
    raise SystemExit("TARGET RAM CHECK: FAIL (cannot parse startup Stack/Heap)")

stack_bytes = int(stack_match.group(1), 16)
heap_bytes = int(heap_match.group(1), 16)
static_ram = data_total + bss_total
reserved = static_ram + stack_bytes + heap_bytes
estimated_free = RAM_TOTAL_BYTES - reserved

print(f"TARGET RAM: Keil C sources={len(sources)}, optimization={KEIL_RELEASE_OPTIMIZATION}")
print(f"TARGET RAM: data={data_total} B, bss={bss_total} B, static={static_ram} B")
print(f"TARGET RAM: stack={stack_bytes} B, heap={heap_bytes} B")
print(f"TARGET RAM: estimated reserved={reserved}/{RAM_TOTAL_BYTES} B, free={estimated_free} B")
print(f"TARGET RAM: required estimated free >= {MIN_ESTIMATED_FREE_BYTES} B")

if reserved > RAM_TOTAL_BYTES:
    raise SystemExit(f"TARGET RAM CHECK: FAIL (estimated use {reserved}B exceeds {RAM_TOTAL_BYTES}B)")
if estimated_free < MIN_ESTIMATED_FREE_BYTES:
    raise SystemExit(
        f"TARGET RAM CHECK: FAIL (estimated free {estimated_free}B < {MIN_ESTIMATED_FREE_BYTES}B)"
    )

print("TARGET RAM CHECK: PASS")
