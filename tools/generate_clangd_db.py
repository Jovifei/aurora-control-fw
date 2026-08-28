#!/usr/bin/env python3
"""从 Keil uvprojx 生成 clangd 用的 compile_commands.json。"""
from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PROJECT = ROOT / "project" / "AuroraControl.uvprojx"
OUTPUT = ROOT / "compile_commands.json"
FLAGS_TXT = ROOT / "compile_flags.txt"

HOST_TEST_SOURCES = [
    ROOT / "tests/mock_driver.c",
    ROOT / "tests/test_main.c",
]


def detect_clang() -> tuple[str, Path | None]:
    """优先使用 PATH 中的 clang；否则回退到 Keil armclang 的运行时头路径。"""
    clang = shutil.which("clang")
    if clang is not None:
        return clang, None

    configured = os.environ.get("AURORA_ARMCLANG")
    fallback = Path(r"D:\Keil_v5\ARM\ARMCLANG\Bin\armclang.exe")
    for candidate in (configured, str(fallback) if fallback.is_file() else None):
        if candidate and Path(candidate).is_file():
            armclang = Path(candidate)
            runtime_include = armclang.parent.parent / "include"
            system_clang = shutil.which("clang")
            compiler = system_clang if system_clang is not None else "clang"
            return compiler, runtime_include if runtime_include.is_dir() else None

    return "clang", None


def parse_uvprojx(project_path: Path) -> tuple[list[str], list[str], list[Path]]:
    if not project_path.is_file():
        raise FileNotFoundError(f"Keil 工程不存在: {project_path}")

    tree = ET.parse(project_path)
    xml_root = tree.getroot()
    project_dir = project_path.parent

    defines: list[str] = []
    includes: list[str] = []
    for controls in xml_root.iter("VariousControls"):
        define_node = controls.find("Define")
        include_node = controls.find("IncludePath")
        define_text = define_node.text if define_node is not None and define_node.text else ""
        include_text = include_node.text if include_node is not None and include_node.text else ""
        if "USE_FULL_DDL_DRIVER" in define_text or "G32F031xx" in define_text:
            defines = [item.strip() for item in define_text.split(",") if item.strip()]
            includes = [item.strip() for item in include_text.split(";") if item.strip()]
            break

    sources: list[Path] = []
    for file_path in xml_root.iter("FilePath"):
        if file_path.text is None:
            continue
        normalized = file_path.text.replace("\\", "/")
        if not normalized.lower().endswith(".c"):
            continue
        absolute = (project_dir / normalized).resolve()
        if absolute.is_file():
            sources.append(absolute)

    unique_sources = sorted(set(sources))
    if not unique_sources:
        raise RuntimeError(f"未在 {project_path} 中解析到任何 .c 源文件")
    return defines, includes, unique_sources


def resolve_include_path(project_dir: Path, include_entry: str) -> str:
    absolute = (project_dir / include_entry).resolve()
    try:
        relative = absolute.relative_to(ROOT)
    except ValueError as exc:
        raise ValueError(f"include 路径不在仓库内: {absolute}") from exc
    return relative.as_posix()


def firmware_arguments(
    clang: str,
    runtime_include: Path | None,
    project_dir: Path,
    defines: list[str],
    includes: list[str],
    source: Path,
) -> list[str]:
    args = [
        clang,
        "--target=arm-none-eabi",
        "-mcpu=cortex-m0plus",
        "-mthumb",
        "-std=c11",
        "-ffreestanding",
        "-Wno-unknown-warning-option",
    ]
    for define in defines:
        args.append(f"-D{define}")
    args.append("-I.clangd-support/include")
    for include_entry in includes:
        args.append(f"-I{resolve_include_path(project_dir, include_entry)}")
    if runtime_include is not None:
        args.extend(["-isystem", str(runtime_include)])
    args.extend(["-c", source.relative_to(ROOT).as_posix()])
    return args


def host_arguments(clang: str, source: Path) -> list[str]:
    return [
        clang,
        "-std=c11",
        "-DAURORA_HOST_TEST=1",
        "-Iapp/inc",
        "-Idriver/inc",
        "-Itests",
        "-Wno-unknown-warning-option",
        "-c",
        source.relative_to(ROOT).as_posix(),
    ]


def write_database(entries: list[dict[str, object]]) -> None:
    OUTPUT.write_text(json.dumps(entries, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {OUTPUT} ({len(entries)} translation units)")


def write_compile_flags(runtime_include: Path | None) -> None:
    lines = [
        "--target=arm-none-eabi",
        "-mcpu=cortex-m0plus",
        "-mthumb",
        "-std=c11",
        "-ffreestanding",
        "-DUSE_FULL_DDL_DRIVER",
        "-DG32F031xx",
        "-I.clangd-support/include",
        "-Iapp/inc",
        "-Idriver/inc",
        "-Ivendor/cmsis/Include",
        "-Ivendor/device/Include",
        "-Ivendor/ddl/Include",
    ]
    if runtime_include is not None:
        lines.extend(["-isystem", runtime_include.as_posix()])
    lines.append("-Wno-unknown-warning-option")
    FLAGS_TXT.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Wrote {FLAGS_TXT}")


def main(argv: list[str]) -> int:
    project = Path(argv[1]).resolve() if len(argv) > 1 else DEFAULT_PROJECT
    clang, runtime_include = detect_clang()
    defines, includes, sources = parse_uvprojx(project)
    project_dir = project.parent

    entries: list[dict[str, object]] = []
    directory = str(ROOT)
    for source in sources:
        entries.append(
            {
                "directory": directory,
                "file": source.relative_to(ROOT).as_posix(),
                "arguments": firmware_arguments(
                    clang,
                    runtime_include,
                    project_dir,
                    defines,
                    includes,
                    source,
                ),
            }
        )

    for source in HOST_TEST_SOURCES:
        if source.is_file():
            entries.append(
                {
                    "directory": directory,
                    "file": source.relative_to(ROOT).as_posix(),
                    "arguments": host_arguments(clang, source),
                }
            )

    write_database(entries)
    write_compile_flags(runtime_include)
    print(f"Compiler: {clang}")
    if runtime_include is not None:
        print(f"System headers: {runtime_include}")
    print("Defines:", ", ".join(defines))
    print("Includes:", ", ".join(includes))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
