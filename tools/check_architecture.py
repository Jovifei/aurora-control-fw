#!/usr/bin/env python3
"""检查量产嵌入式工程的目录、依赖、PinMap与安全边界。"""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
errors: list[str] = []

required_dirs = {"app", "service", "driver", "board", "vendor", "project", "docs", "tests", "tools"}
actual_dirs = {p.name for p in root.iterdir() if p.is_dir() and not p.name.startswith("build-")}
missing = sorted(required_dirs - actual_dirs)
if missing:
    errors.append(f"缺少目录: {missing}")

for forbidden in ["firmware", "tasks", "legacy_reference", "legacy_parity", "legacy_protocol_import"]:
    if any(p.name == forbidden for p in root.rglob("*")):
        errors.append(f"禁止目录仍存在: {forbidden}")

# tests/tools只能位于仓库根目录，不能在固件目录中再复制一套。
for name in ["tests", "tools", "docs"]:
    nested = [p for p in root.rglob(name) if p.is_dir() and p.parent != root and not any(part.startswith("build-") for part in p.parts)]
    if nested:
        errors.append(f"重复的{name}目录: {[str(p.relative_to(root)) for p in nested]}")

# APP和Driver均采用扁平布局；Vendor和Project允许按工具链组织子目录。
for flat_dir in [root / "app", root / "driver", root / "service", root / "board"]:
    nested = [p for p in flat_dir.iterdir() if p.is_dir()]
    if nested:
        errors.append(f"{flat_dir.name}不应再分子目录: {[p.name for p in nested]}")

for p in root.rglob("*.json"):
    if not any(part.startswith("build-") for part in p.parts):
        errors.append(f"仓库不应提交生成JSON: {p.relative_to(root)}")

for p in (root / "app").glob("*.[ch]"):
    text = p.read_text(encoding="utf-8", errors="ignore")
    if re.search(r'#include\s+"(?:g32|board|driver|service)', text, re.I):
        errors.append(f"应用层越层依赖: {p.relative_to(root)}")
    code = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    code = re.sub(r"//.*", "", code)
    if re.search(r'\b(?:GPIO[AB]|ATMR|COMP[0-9]|DMA|NVIC|SysTick|DDL_)\b|->(?:CR|SR|DR|CCR|ARR)', code):
        errors.append(f"应用层疑似访问硬件: {p.relative_to(root)}")

for p in (root / "driver").glob("*.[ch]"):
    text = p.read_text(encoding="utf-8", errors="ignore")
    if re.search(r'#include\s+"(?:app|service|mppt|charger|protection|power_stage)', text):
        errors.append(f"驱动层反向依赖应用: {p.relative_to(root)}")

for p in (root / "docs").iterdir():
    if p.is_file() and p.suffix.lower() not in {".md", ".pdf", ".png", ".jpg", ".svg"}:
        errors.append(f"docs含非文档文件: {p.name}")
    if p.is_file() and p.suffix.lower() in {".c", ".h"}:
        errors.append(f"docs中不得放生产源码: {p.name}")

all_text = "\n".join(
    p.read_text(encoding="utf-8", errors="ignore")
    for d in ["app", "service", "driver", "board", "project"]
    for p in (root / d).rglob("*.[ch]")
)
for token in ["HT32_Mppt_Solar", "Fun_MPPT_FUNC", "arm_math", "cmsis_os1", "legacy_parity"]:
    if token in all_text:
        errors.append(f"生产代码含历史/闭源依赖: {token}")

# 当前PCB没有BAT_S1/BAT_S2，禁止旧按键定义重新进入生产代码。
for token in ["BAT_S1", "BAT_S2", "drv_io_read_setting"]:
    if token in all_text:
        errors.append(f"生产代码含已删除硬件信号: {token}")

# Only target driver may access MOE; application and service use the driver contract.
for d in ["app", "service"]:
    for p in (root / d).rglob("*.[ch]"):
        text = p.read_text(encoding="utf-8", errors="ignore")
        if re.search(r'EnableAllOutputs|MOE|CHMOE|GenerateEvent_UPDATE', text):
            errors.append(f"非驱动层直接操作PWM硬件: {p.relative_to(root)}")

# PinMap硬约束：与2026-08-26最终原理图一致。
board_cfg = (root / "board/board_config.h").read_text(encoding="utf-8", errors="ignore")
pin_tokens = [
    "BOARD_PIN_GLC_NUMBER                (15U)",
    "BOARD_PIN_UART_TX_NUMBER            (10U)",
    "BOARD_PIN_UART_RX_NUMBER            (11U)",
    "BOARD_PIN_LED_RUN_NUMBER            (9U)",
    "BOARD_PIN_COMP0_OUT_NUMBER           (10U)",
    "BOARD_PIN_COMP0_OUT_AF               (7U)",
    "BOARD_COMP0_FAULT_ACTIVE_LOW          (1U)",
    "BOARD_PIN_LED_FAULT_NUMBER           (11U)",
]
for token in pin_tokens:
    if token not in board_cfg:
        errors.append(f"最终PinMap缺失或被改动: {token.strip()}")

uart = (root / "driver/drv_uart.c").read_text(encoding="utf-8", errors="ignore")
if "DDL_GPIO_PIN_10; /* UR_TX / PA10 / AF0 */" not in uart or \
   "DDL_GPIO_PIN_11; /* UR_RX / PA11 / AF0 */" not in uart:
    errors.append("UART驱动未绑定最终PA10/PA11")
comp = (root / "driver/drv_comp.c").read_text(encoding="utf-8", errors="ignore")
if "PB10 / AF7 = COMP0_OUT" not in comp or "DDL_GPIO_AF_7" not in comp:
    errors.append("COMP0_OUT未绑定PB10/AF7")
for token in ["DDL_GPIO_OUTPUT_OPENDRAIN", "DDL_COMP0_OUTPUTPOL_INVERTED",
              "DDL_COMP0_EDGE_INT_FALLING"]:
    if token not in comp:
        errors.append(f"COMP0外部EN低有效安全链缺失: {token}")

# 软件UEV只允许初始化时一次；UPDATE IRQ只允许首个零占空比装载时临时开启。
pwm = (root / "driver/drv_pwm.c").read_text(encoding="utf-8", errors="ignore")
if pwm.count("DDL_ATMR_GenerateEvent_UPDATE") != 1:
    errors.append("PWM软件UEV必须且只能存在于初始化阶段一次")
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

# ISR bridge must stay small and may not run application algorithms.
isr = (root / "project/keil/interrupts.c").read_text(encoding="utf-8", errors="ignore")
for forbidden_call in ["aurora_mppt_step", "aurora_charger_step", "aurora_measurement_process_block",
                       "aurora_storage_encode_page", "aurora_ui_step"]:
    if forbidden_call in isr:
        errors.append(f"ISR调用重业务: {forbidden_call}")
# UART ISR有明确预算的有界while；其他ISR不允许阻塞while。
for line in isr.splitlines():
    stripped = line.strip()
    if stripped.startswith("while") and not ("budget" in stripped and "drv_uart_rx_ready_isr" in stripped):
        errors.append(f"ISR中存在未证明有界的while: {stripped}")

# 应用模块数量保持可读，不重新退化成数十个碎片文件。
app_c_count = len(list((root / "app").glob("*.c")))
if app_c_count > 10:
    errors.append(f"APP源文件过度碎片化: {app_c_count}个.c")

if errors:
    print("ARCHITECTURE CHECK: FAIL")
    for e in errors:
        print(f"- {e}")
    sys.exit(1)
print("ARCHITECTURE CHECK: PASS")
