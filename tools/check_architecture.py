#!/usr/bin/env python3
"""检查量产嵌入式工程的目录、依赖、PinMap与功率安全边界。"""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
errors: list[str] = []


def is_project_content(path: Path) -> bool:
    """排除Git内部元数据和CMake生成目录，不改变生产目录的检查范围。"""
    return (".git" not in path.parts and
            not any(part.startswith("build-") for part in path.parts))


required_dirs = {
    "app", "service", "driver", "board", "vendor", "project", "docs", "tests", "tools"
}
actual_dirs = {
    item.name for item in root.iterdir()
    if item.is_dir() and not item.name.startswith("build-") and item.name != ".git"
}
missing = sorted(required_dirs - actual_dirs)
if missing:
    errors.append(f"缺少目录: {missing}")

# 历史工程、套娃工程和生成目录不得重新进入生产仓库。
for forbidden in [
    "firmware", "tasks", "legacy_reference", "legacy_parity", "legacy_protocol_import",
    ".bootstrap"
]:
    if any(path.name == forbidden and is_project_content(path)
           for path in root.rglob("*")):
        errors.append(f"禁止目录仍存在: {forbidden}")

# tests/tools/docs只能位于仓库根目录，不能在固件目录中复制第二套。
for name in ["tests", "tools", "docs"]:
    nested = [
        path for path in root.rglob(name)
        if path.is_dir() and path.parent != root and
        is_project_content(path)
    ]
    if nested:
        errors.append(f"重复的{name}目录: {[str(path.relative_to(root)) for path in nested]}")

# app按用户约定拆为inc/src；其余首方模块保持扁平。
app_subdirs = {item.name for item in (root / "app").iterdir() if item.is_dir()}
if app_subdirs != {"inc", "src"}:
    errors.append(f"app必须且只能包含inc/src: {sorted(app_subdirs)}")
if list((root / "app").glob("*.[ch]")):
    errors.append("app根目录仍存在未迁移的.c/.h")
for flat_dir in [root / "service", root / "board"]:
    nested = [path for path in flat_dir.iterdir() if path.is_dir()]
    if nested:
        errors.append(f"{flat_dir.name}不应再分子目录: {[path.name for path in nested]}")

# 生成JSON和构建输出不得提交。
for path in root.rglob("*.json"):
    if is_project_content(path):
        errors.append(f"仓库不应提交生成JSON: {path.relative_to(root)}")

app_files = [*(root / "app/inc").glob("*.h"), *(root / "app/src").glob("*.c")]

driver_root_files = [p for p in (root / "driver").iterdir() if p.is_file()] if (root / "driver").exists() else []
if driver_root_files:
    errors.append("Driver源码必须分开放入driver/inc和driver/src，driver根目录不得直接放.c/.h")

if not (root / "driver/inc/driver.h").is_file():
    errors.append("缺少driver/inc/driver.h")
if not list((root / "driver/src").glob("*.c")):
    errors.append("driver/src中没有目标驱动源文件")
for path in app_files:
    text = path.read_text(encoding="utf-8", errors="ignore")
    if re.search(r'#include\s+"(?:g32|board|driver|service)', text, re.I):
        errors.append(f"应用层越层依赖: {path.relative_to(root)}")
    code = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    code = re.sub(r"//.*", "", code)
    if re.search(r'\b(?:GPIO[AB]|ATMR|COMP[0-9]|DMA|NVIC|SysTick|DDL_)\b|->(?:CR|SR|DR|CCR|ARR)', code):
        errors.append(f"应用层疑似访问硬件: {path.relative_to(root)}")

for path in [*(root / "driver/inc").glob("*.h"), *(root / "driver/src").glob("*.c")]:
    text = path.read_text(encoding="utf-8", errors="ignore")
    if re.search(r'#include\s+"(?:app|service|mppt|charger|protection|power_stage)', text):
        errors.append(f"驱动层反向依赖应用: {path.relative_to(root)}")

for path in (root / "docs").iterdir():
    if path.is_file() and path.suffix.lower() not in {".md", ".pdf", ".png", ".jpg", ".svg"}:
        errors.append(f"docs含非文档文件: {path.name}")
    if path.is_file() and path.suffix.lower() in {".c", ".h"}:
        errors.append(f"docs中不得放生产源码: {path.name}")

all_text = "\n".join(
    path.read_text(encoding="utf-8", errors="ignore")
    for directory in ["app", "service", "driver", "board", "project"]
    for path in (root / directory).rglob("*.[ch]")
)
for token in ["HT32_Mppt_Solar", "Fun_MPPT_FUNC", "arm_math", "cmsis_os1", "legacy_parity"]:
    if token in all_text:
        errors.append(f"生产代码含历史/闭源依赖: {token}")
for token in ["BAT_S1", "BAT_S2", "drv_io_read_setting"]:
    if token in all_text:
        errors.append(f"生产代码含已删除硬件信号: {token}")

# APP/Service只能经driver契约操作PWM，不得直接出现目标寄存器名。
for directory in ["app", "service"]:
    for path in (root / directory).rglob("*.[ch]"):
        text = path.read_text(encoding="utf-8", errors="ignore")
        if re.search(r'EnableAllOutputs|CHMOE|GenerateEvent_UPDATE|DDL_ATMR_', text):
            errors.append(f"非驱动层直接操作PWM硬件: {path.relative_to(root)}")

board_cfg = (root / "board/board_config.h").read_text(encoding="utf-8", errors="ignore")
expected_macros = {
    "BOARD_PIN_GLC_NUMBER": "15U",
    "BOARD_PIN_UART_TX_NUMBER": "10U",
    "BOARD_PIN_UART_RX_NUMBER": "11U",
    "BOARD_PIN_LED_RUN_NUMBER": "9U",
    "BOARD_PIN_COMP0_OUT_NUMBER": "10U",
    "BOARD_PIN_COMP0_OUT_AF": "7U",
    "BOARD_COMP0_FAULT_ACTIVE_LOW": "1U",
    "BOARD_PIN_LED_FAULT_NUMBER": "11U",
    "BOARD_POWER_OUTPUT_ALLOWED": "0U",
}
for name, value in expected_macros.items():
    if re.search(rf"#define\s+{name}\s+\({re.escape(value)}\)", board_cfg) is None:
        errors.append(f"最终PinMap/门禁缺失或被改动: {name}=({value})")

uart = (root / "driver/src/drv_uart.c").read_text(encoding="utf-8", errors="ignore")
if "DDL_GPIO_PIN_10; /* UR_TX / PA10 / AF0 */" not in uart or \
   "DDL_GPIO_PIN_11; /* UR_RX / PA11 / AF0 */" not in uart:
    errors.append("UART驱动未绑定最终PA10/PA11")
comp = (root / "driver/src/drv_comp.c").read_text(encoding="utf-8", errors="ignore")
if "PB10 / AF7 = COMP0_OUT" not in comp or "DDL_GPIO_AF_7" not in comp:
    errors.append("COMP0_OUT未绑定PB10/AF7")
for token in [
    "DDL_GPIO_OUTPUT_OPENDRAIN", "DDL_COMP0_OUTPUTPOL_INVERTED",
    "DDL_COMP0_EDGE_INT_FALLING"
]:
    if token not in comp:
        errors.append(f"COMP0外部EN低有效安全链缺失: {token}")

# 软件UPDATE事件只允许初始化时一次；首次放行只临时开启一次UPDATE IRQ。
pwm = (root / "driver/src/drv_pwm.c").read_text(encoding="utf-8", errors="ignore")
if pwm.count("DDL_ATMR_GenerateEvent_UPDATE") != 1:
    errors.append("PWM软件UPDATE事件必须且只能存在于初始化阶段一次")
if pwm.count("DDL_ATMR_EnableIT_UPDATE") != 1 or "drv_pwm_prepare_arm_zero" not in pwm:
    errors.append("PWM UPDATE中断必须只在首个零占空比装载时临时开启")
if "DDL_ATMR_DisableIT_UPDATE" not in pwm:
    errors.append("PWM UPDATE中断缺少一次性关闭")
if "AutomaticOutput = DDL_ATMR_AUTOMATICOUTPUT_DISABLE" not in pwm:
    errors.append("PWM未明确关闭Automatic Output")
if "DDL_ATMR_OC_EnablePreload" not in pwm:
    errors.append("PWM未启用CCR preload")
if "DDL_ATMR_BREAK_POLARITY_LOW" not in pwm:
    errors.append("COMP0低有效故障未对应ATMR低有效Break")
if "drv_pwm_quiesce_break_irq_isr" not in pwm:
    errors.append("Break ISR缺少只屏蔽重复中断、不清锁存的接口")

# ISR桥接保持轻量，不得调用APP重业务。
isr = (root / "project/keil/interrupts.c").read_text(encoding="utf-8", errors="ignore")
for forbidden_call in [
    "aurora_mppt_step", "aurora_charger_step", "aurora_measurement_process_block",
    "aurora_storage_encode_page", "aurora_ui_step"
]:
    if forbidden_call in isr:
        errors.append(f"ISR调用重业务: {forbidden_call}")
for line in isr.splitlines():
    stripped = line.strip()
    if stripped.startswith("while") and not (
        "budget" in stripped and "drv_uart_rx_ready_isr" in stripped
    ):
        errors.append(f"ISR中存在未证明有界的while: {stripped}")

app_c_count = len(list((root / "app/src").glob("*.c")))
if app_c_count > 10:
    errors.append(f"APP源文件过度碎片化: {app_c_count}个.c")

cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8", errors="ignore")
if "app/src/app.c" not in cmake or "app/inc" not in cmake:
    errors.append("CMake未切换到app/src和app/inc")
keil = (root / "project/keil/AuroraControl.uvprojx").read_text(encoding="utf-8", errors="ignore")
if "app\\src\\app.c" not in keil or "app\\inc" not in keil:
    errors.append("Keil工程未切换到app/src和app/inc")
if re.search(r"<FileName>\.\.\\\.\.\\", keil):
    errors.append("Keil外部源码的FileName不得带父目录，避免中间文件落入源码路径")

if errors:
    print("ARCHITECTURE CHECK: FAIL")
    for error in errors:
        print(f"- {error}")
    sys.exit(1)

print("ARCHITECTURE CHECK: PASS")
