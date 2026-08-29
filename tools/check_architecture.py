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
    "firmware", "tasks", "legacy_reference", "legacy_parity",
    "legacy_protocol_import", ".bootstrap"
]:
    if any(path.name == forbidden and is_project_content(path)
           for path in root.rglob("*")):
        errors.append(f"禁止目录仍存在: {forbidden}")

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
    text = path.read_text(encoding="utf-8", errors="ignore")
    if re.search(r'#include\s+"(?:g32|board_config|system_g32|cmsis)', text, re.I):
        errors.append(f"APP直接包含目标硬件头: {path.relative_to(root)}")
    code = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    code = re.sub(r"//.*", "", code)
    if re.search(r'\b(?:GPIO[AB]|ATMR|COMP[0-9]|DMA|NVIC|SysTick|DDL_)\b|->(?:CR|SR|DR|CCR|ARR)', code):
        errors.append(f"APP疑似直接访问硬件/DDL: {path.relative_to(root)}")

# Driver只能向下依赖标准库/board_config/vendor，不得反向依赖APP。
for path in [*(root / "driver/inc").glob("*.h"), *(root / "driver/src").glob("*.c")]:
    text = path.read_text(encoding="utf-8", errors="ignore")
    if re.search(
        r'#include\s+"(?:main|app_config|app_types|charger|measurement|mppt|power_stage|protection|protocol|storage|ui|debug|interrupts)\.h"',
        text,
    ):
        errors.append(f"Driver反向依赖APP: {path.relative_to(root)}")

# 生产代码不得残留service/board旧接口名或历史闭源依赖。
production_text = "\n".join(
    path.read_text(encoding="utf-8", errors="ignore")
    for directory in [root / "app", root / "driver"]
    for path in directory.rglob("*.[ch]")
)
for token in ["HT32_Mppt_Solar", "Fun_MPPT_FUNC", "arm_math", "cmsis_os1", "legacy_parity"]:
    if token in production_text:
        errors.append(f"生产代码含历史/闭源依赖: {token}")
for token in ["aurora_service_t", "aurora_service_", "aurora_board_"]:
    if token in production_text:
        errors.append(f"生产代码残留旧三层接口: {token}")
for token in ["BAT_S1", "BAT_S2", "drv_io_read_setting"]:
    if token in production_text:
        errors.append(f"生产代码含已删除硬件信号: {token}")

# APP中只有main.c/interrupts.c允许调用Driver；业务模块保持纯业务。
for path in (root / "app/src").glob("*.c"):
    text = path.read_text(encoding="utf-8", errors="ignore")
    if path.name not in {"main.c", "interrupts.c"} and re.search(r'\bdrv_[A-Za-z0-9_]+\s*\(', text):
        errors.append(f"业务模块直接调用Driver: {path.relative_to(root)}")

# ISR保持轻量且不访问vendor。
isr = (root / "app/src/interrupts.c").read_text(encoding="utf-8", errors="ignore")
for forbidden_call in [
    "aurora_mppt_step", "aurora_charger_step", "aurora_measurement_process_block",
    "aurora_storage_encode_page", "aurora_ui_step"
]:
    if forbidden_call in isr:
        errors.append(f"ISR调用重业务: {forbidden_call}")
if "DDL_" in isr or "g32f" in isr.lower():
    errors.append("interrupts.c不得直接访问vendor/DDL")
for line in isr.splitlines():
    stripped = line.strip()
    if stripped.startswith("while") and not (
        "budget" in stripped and "drv_uart_rx_ready_isr" in stripped
    ):
        errors.append(f"ISR中存在未证明有界的while: {stripped}")

# 板级配置已经归入Driver。
board_cfg_path = root / "driver/inc/board_config.h"
if not board_cfg_path.is_file():
    errors.append("driver/inc/board_config.h不存在")
else:
    board_cfg = board_cfg_path.read_text(encoding="utf-8", errors="ignore")
    expected_macros = {
        "BOARD_PIN_GLC_NUMBER": "15U",
        "BOARD_PIN_UART_TX_NUMBER": "10U",
        "BOARD_PIN_UART_RX_NUMBER": "11U",
        "BOARD_PIN_LED_RUN_NUMBER": "9U",
        "BOARD_PIN_COMP0_OUT_NUMBER": "10U",
        "BOARD_PIN_COMP0_OUT_AF": "7U",
        "BOARD_COMP0_FAULT_ACTIVE_LOW": "1U",
        "BOARD_PIN_LED_FAULT_NUMBER": "11U",
        "BOARD_NTC_MOS_PULLUP_OHM": "5100L",
        "BOARD_NTC_MOS_R25_OHM": "100000L",
        "BOARD_NTC_MOS_BETA_KELVIN": "3950L",
        "BOARD_NTC_AMB_PULLUP_OHM": "5100L",
        "BOARD_NTC_AMB_R25_OHM": "100000L",
        "BOARD_NTC_AMB_BETA_KELVIN": "3950L",
        "BOARD_POWER_OUTPUT_ALLOWED": "0U",
    }
    for name, value in expected_macros.items():
        if re.search(rf"#define\s+{name}\s+\({re.escape(value)}\)", board_cfg) is None:
            errors.append(f"最终PinMap/门禁缺失或被改动: {name}=({value})")

# PWM安全不因两层重构而弱化。
pwm = (root / "driver/src/drv_pwm.c").read_text(encoding="utf-8", errors="ignore")
if pwm.count("DDL_ATMR_GenerateEvent_UPDATE") != 1:
    errors.append("PWM软件UPDATE事件必须且只能存在于初始化阶段一次")
if pwm.count("DDL_ATMR_EnableIT_UPDATE") != 1 or "drv_pwm_prepare_arm_zero" not in pwm:
    errors.append("首次PWM零CCR握手缺失")
if "DDL_ATMR_DisableIT_UPDATE" not in pwm:
    errors.append("PWM UPDATE中断缺少一次性关闭")
if "AutomaticOutput = DDL_ATMR_AUTOMATICOUTPUT_DISABLE" not in pwm:
    errors.append("PWM未明确关闭Automatic Output")
if "DDL_ATMR_OC_EnablePreload" not in pwm:
    errors.append("PWM未启用CCR preload")

# CMake必须只使用APP/Driver两层首方源码。
cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8", errors="ignore")
for required in ["app/src/main.c", "driver/src/drv_board.c", "app/inc", "driver/inc"]:
    if required not in cmake:
        errors.append(f"CMake缺少v0.8.3路径: {required}")
for forbidden in ["service/service.c", "board/board.c", "app/src/app.c"]:
    if forbidden in cmake:
        errors.append(f"CMake仍引用旧路径: {forbidden}")

# Keil工程必须位于project根目录且只引用新两层源码。
keil_path = root / "project/AuroraControl.uvprojx"
if not keil_path.is_file() or not (root / "project/AuroraControl.sct").is_file():
    errors.append("Keil工程/Scatter未迁移到project根目录")
else:
    try:
        project = ET.parse(keil_path)
        file_paths = [node.text or "" for node in project.findall(".//FilePath")]
        joined = "\n".join(file_paths)
        for required in ["app\\src\\main.c", "app\\src\\interrupts.c", "driver\\src\\drv_board.c"]:
            if required not in joined:
                errors.append(f"Keil缺少v0.8.3源码: {required}")
        for forbidden in ["service\\", "board\\board.c", "app\\src\\app.c", "project\\keil"]:
            if forbidden in joined:
                errors.append(f"Keil仍引用旧路径: {forbidden}")
    except ET.ParseError as exc:
        errors.append(f"Keil工程XML无效: {exc}")

if (root / "project/keil").exists():
    errors.append("project/keil旧目录仍存在；v0.8.3工程文件应直接位于project/")

# docs根目录只允许文档类文件；reference子目录可保存表格等证据文件。
for path in (root / "docs").iterdir():
    if path.is_file() and path.suffix.lower() not in {".md", ".pdf", ".png", ".jpg", ".svg"}:
        errors.append(f"docs根目录含非文档文件: {path.name}")

if errors:
    print("ARCHITECTURE CHECK: FAIL")
    for error in errors:
        print(f"- {error}")
    sys.exit(1)

print("ARCHITECTURE CHECK: PASS")
