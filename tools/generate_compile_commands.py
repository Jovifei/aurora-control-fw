#!/usr/bin/env python3
"""为 clangd 生成 compile_commands.json，覆盖全部固件与 Host 测试源文件。"""
from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import sys

root = Path(__file__).resolve().parents[1]

FIRMWARE_SOURCES = sorted((root / "driver/src").glob("*.c")) + sorted(
    path
    for path in (root / "app/src").glob("*.c")
    if path.name not in {"main.c", "interrupts.c", "debug.c"}
)
FIRMWARE_SOURCES = sorted(
    {
        *FIRMWARE_SOURCES,
        root / "app/src/main.c",
        root / "app/src/interrupts.c",
        root / "app/src/debug.c",
    }
)
HOST_SOURCES = [
    root / "tests/mock_driver.c",
    root / "tests/test_main.c",
    root / "driver/src/drv_board.c",
]


def detect_clang() -> tuple[str, Path | None]:
    clang = shutil.which("clang")
    if clang is not None:
        return clang, None

    configured = os.environ.get("AURORA_ARMCLANG")
    fallback = Path(r"D:\Keil_v5\ARM\ARMCLANG\Bin\armclang.exe")
    for candidate in (configured, str(fallback) if fallback.is_file() else None):
        if candidate and Path(candidate).is_file():
            armclang = Path(candidate)
            runtime_include = armclang.parent.parent / "include"
            return str(armclang), runtime_include

    print("ERROR: 未找到 clang 或 armclang，无法生成 compile_commands.json", file=sys.stderr)
    sys.exit(1)


def base_command(clang: str, runtime_include: Path | None) -> list[str]:
    target = "arm-arm-none-eabi" if runtime_include is not None else "arm-none-eabi"
    command = [
        clang,
        f"--target={target}",
        "-mcpu=cortex-m0plus",
        "-mthumb",
        "-std=c11",
        "-ffreestanding",
        "-DUSE_FULL_DDL_DRIVER",
        "-DG32F031xx",
        "-Iapp/inc",
        "-Idriver/inc",
        "-Ivendor/cmsis/Include",
        "-Ivendor/device/Include",
        "-Ivendor/ddl/Include",
    ]
    if runtime_include is not None:
        command.extend(["-isystem", str(runtime_include)])
    return command


def entry(directory: str, file: str, command: list[str]) -> dict[str, str]:
    return {
        "directory": directory,
        "file": file,
        "command": " ".join(command + [file]),
    }


def main() -> None:
    clang, runtime_include = detect_clang()
    directory = str(root)
    commands: list[dict[str, str]] = []

    firmware_base = base_command(clang, runtime_include)
    for source in FIRMWARE_SOURCES:
        rel = source.relative_to(root).as_posix()
        commands.append(entry(directory, rel, firmware_base))

    host_base = [
        clang,
        "-std=c11",
        "-DAURORA_HOST_TEST=1",
        "-Iapp/inc",
        "-Idriver/inc",
        "-Itests",
    ]
    for source in HOST_SOURCES:
        rel = source.relative_to(root).as_posix()
        commands.append(entry(directory, rel, host_base))

    output = root / "compile_commands.json"
    output.write_text(json.dumps(commands, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {output} ({len(commands)} translation units)")


if __name__ == "__main__":
    main()
