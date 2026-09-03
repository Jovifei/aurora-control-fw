#!/usr/bin/env python3
"""检查v0.8.3两层产品架构、目录、依赖和功率安全边界。"""
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[1]
errors: list[str] = []


def is_project_content(path: Path) -> bool:
    """排除Git元数据和Host构建目录。"""
    return (".git" not in path.parts and
            not any(part.startswith("build-") for part in path.parts))


required_dirs = {"app", "driver", "vendor", "project", "docs", "tests", "tools"}
actual_dirs = {
    item.name for item in root.iterdir()
    if item.is_dir() and not item.name.startswith("build-") and item.name != ".git"
}
missing = sorted(required_dirs - actual_dirs)
if missing:
    errors.append(f"缺少目录: {missing}")

# v0.8.3明确删除第三层Service和独立Board层。
for forbidden_root in ["service", "board"]:
    if (root / forbidden_root).exists():
        errors.append(f"v0.8.3禁止根目录: {forbidden_root}/")

for forbidden in [
    "firmware", "legacy_reference", "legacy_parity",
    "legacy_protocol_import", ".bootstrap"
]:
    if any(path.name == forbidden and is_project_content(path)
           for path in root.rglob("*")):
        errors.append(f"禁止目录仍存在: {forbidden}")

# G0~G15迁移过程允许tasks/保存纯Markdown任务/经验记录，但绝不允许它成为第三套固件源码树。
tasks_root = root / "tasks"
if tasks_root.exists():
    for task_file in tasks_root.rglob("*"):
        if task_file.is_file() and task_file.suffix.lower() != ".md":
            errors.append(f"tasks仅允许Markdown过程资料: {task_file.relative_to(root)}")

# APP/Driver必须严格采用inc/src。
for layer in ["app", "driver"]:
    layer_root = root / layer
    subdirs = {item.name for item in layer_root.iterdir() if item.is_dir()}
    if subdirs != {"inc", "src"}:
        errors.append(f"{layer}必须且只能包含inc/src: {sorted(subdirs)}")
    if list(layer_root.glob("*.[ch]")):
        errors.append(f"{layer}根目录不得直接放.c/.h")

required_app_headers = {
    "main.h", "app_types.h", "app_config.h", "debug.h", "interrupts.h",
    "charger.h", "measurement.h", "mppt.h", "power_stage.h",
    "protection.h", "protocol.h", "storage.h", "ui.h",
}
actual_app_headers = {p.name for p in (root / "app/inc").glob("*.h")}
if actual_app_headers != required_app_headers:
    errors.append(
        "app/inc文件集合不符合v0.8.3目标: "
        f"缺少={sorted(required_app_headers-actual_app_headers)} "
        f"多余={sorted(actual_app_headers-required_app_headers)}"
    )

required_app_sources = {
    "main.c", "interrupts.c", "debug.c", "charger.c", "measurement.c",
    "mppt.c", "power_stage.c", "protection.c", "protocol.c", "storage.c", "ui.c",
}
actual_app_sources = {p.name for p in (root / "app/src").glob("*.c")}
if actual_app_sources != required_app_sources:
    errors.append(
        "app/src文件集合不符合v0.8.3目标: "
        f"缺少={sorted(required_app_sources-actual_app_sources)} "
        f"多余={sorted(actual_app_sources-required_app_sources)}"
    )

required_driver_headers = {
    "driver.h", "board_config.h", "drv_board.h", "drv_adc.h", "drv_comp.h",
    "drv_flash.h", "drv_io.h", "drv_pwm.h", "drv_system.h", "drv_uart.h",
    "drv_watchdog.h",
}
actual_driver_headers = {p.name for p in (root / "driver/inc").glob("*.h")}
if actual_driver_headers != required_driver_headers:
    errors.append(
        "driver/inc文件集合不符合v0.8.3目标: "
        f"缺少={sorted(required_driver_headers-actual_driver_headers)} "
        f"多余={sorted(actual_driver_headers-required_driver_headers)}"
    )

required_driver_sources = {
    "drv_board.c", "drv_adc.c", "drv_comp.c", "drv_flash.c", "drv_io.c",
    "drv_pwm.c", "drv_system.c", "drv_uart.c", "drv_watchdog.c",
}
actual_driver_sources = {p.name for p in (root / "driver/src").glob("*.c")}
if actual_driver_sources != required_driver_sources:
    errors.append(
        "driver/src文件集合不符合v0.8.3目标: "
        f"缺少={sorted(required_driver_sources-actual_driver_sources)} "
        f"多余={sorted(actual_driver_sources-required_driver_sources)}"
    )

# App允许调用Driver契约，但不得包含board_config/vendor/芯片头或直接访问寄存器。
app_files = [*(root / "app/inc").glob("*.h"), *(root / "app/src").glob("*.c")]
for path in app_files:
    text = path.read_text(encoding="utf-8")
    if re.search(r'#include\s+["<](board_config\.h|g32f031[^">]*|core_cm0plus\.h)', text,
                 flags=re.IGNORECASE):
        errors.append(f"APP越层包含硬件头: {path.relative_to(root)}")
    if re.search(r'\b(GPIO|ADC|ATMR|GTMR|DMA|FLASH|USART|UART|RCC|PMU|CMP)\d*->', text):
        errors.append(f"APP直接访问寄存器: {path.relative_to(root)}")

# Driver不得反向包含APP头。
app_header_names = {p.name for p in (root / "app/inc").glob("*.h")}
for path in [*(root / "driver/inc").glob("*.h"), *(root / "driver/src").glob("*.c")]:
    text = path.read_text(encoding="utf-8")
    for header in app_header_names:
        if re.search(rf'#include\s+["<]{re.escape(header)}[">]', text):
            errors.append(f"Driver反向依赖APP: {path.relative_to(root)} -> {header}")

# 目标工程不得再引用已删除层，也不得编译Host测试。
uvproj = root / "project/AuroraControl.uvprojx"
if not uvproj.exists():
    errors.append("缺少project/AuroraControl.uvprojx")
else:
    try:
        tree = ET.parse(uvproj)
        file_paths = [
            (node.text or "").replace("\\", "/").lower()
            for node in tree.iter("FilePath")
        ]
        for forbidden in ["service/", "board/src", "board/inc", "tests/", "legacy_"]:
            if any(forbidden in path for path in file_paths):
                errors.append(f"Keil仍引用禁止路径: {forbidden}")
    except ET.ParseError as exc:
        errors.append(f"Keil工程XML解析失败: {exc}")

# Scatter必须保留最后1 KiB给双页Journal，防止链接覆盖参数区。
scatter = root / "project/AuroraControl.sct"
if not scatter.exists():
    errors.append("缺少project/AuroraControl.sct")
else:
    scatter_text = scatter.read_text(encoding="utf-8")
    if "LR_IROM1 0x00000000 0x0000FC00" not in scatter_text:
        errors.append("Scatter未把0xFC00~0xFFFF保留给Flash Journal")

# 关键功率门必须维持锁定，Host专用覆盖宏不得改变生产默认值。
board_config = (root / "driver/inc/board_config.h").read_text(encoding="utf-8")
for macro in [
    "BOARD_GATE_COMP_ROUTE_VALIDATED",
    "BOARD_GATE_ANALOG_CALIBRATED",
    "BOARD_GATE_KEIL_LINKED",
    "BOARD_GATE_LOW_VOLTAGE_BENCH",
    "BOARD_GATE_DEMO_LOAD_VALIDATED",
    "BOARD_POWER_OUTPUT_ALLOWED",
]:
    match = re.search(rf"#define\s+{macro}\s+\((\d+)U\)", board_config)
    if match is None:
        errors.append(f"缺少安全门宏: {macro}")
    elif match.group(1) != "0":
        errors.append(f"生产功率门被提前打开: {macro}={match.group(1)}")

if errors:
    print("ARCHITECTURE CHECK: FAIL")
    for error in errors:
        print(f"- {error}")
    sys.exit(1)

print("ARCHITECTURE CHECK: PASS")
