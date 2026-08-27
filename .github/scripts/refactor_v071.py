#!/usr/bin/env python3
"""v0.7.1 目录、注释、代码规范、文档和安全门禁一次性整改脚本。"""
from __future__ import annotations

from pathlib import Path
import hashlib
import os
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
APP_DIR = ROOT / "app"
APP_INC = APP_DIR / "inc"
APP_SRC = APP_DIR / "src"

FUNC_RE = re.compile(
    r"(?m)^(?P<indent>[ \t]*)"
    r"(?P<signature>(?:(?:static|inline|const|volatile|__attribute__\s*\(\([^\n]*\)\)|[A-Za-z_]\w*|\*|\s)+?)"
    r"\b(?P<name>[A-Za-z_]\w*)\s*\((?P<params>(?:[^(){};]|\([^()]*\))*)\))\s*\n?\s*\{"
)
CONTROL_NAMES = {"if", "for", "while", "switch", "return", "sizeof"}


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8-sig")


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text.replace("\r\n", "\n").replace("\r", "\n"), encoding="utf-8", newline="\n")


def is_text_file(path: Path) -> bool:
    return path.suffix.lower() in {
        ".c", ".h", ".s", ".md", ".txt", ".py", ".cmake", ".yml", ".yaml", ".xml", ".uvprojx", ".sct"
    } or path.name in {"CMakeLists.txt", "AGENTS.md", "README.md", ".gitignore"}


def reorganize_app() -> tuple[list[str], list[str]]:
    """将APP头文件和实现分别移动到inc/src，并返回移动后的文件名。"""
    APP_INC.mkdir(parents=True, exist_ok=True)
    APP_SRC.mkdir(parents=True, exist_ok=True)
    moved_c: list[str] = []
    moved_h: list[str] = []
    for path in list(APP_DIR.iterdir()):
        if not path.is_file():
            continue
        if path.suffix == ".c":
            moved_c.append(path.name)
            shutil.move(str(path), APP_SRC / path.name)
        elif path.suffix == ".h":
            moved_h.append(path.name)
            shutil.move(str(path), APP_INC / path.name)
    return sorted(moved_c), sorted(moved_h)


def update_paths(moved_c: list[str], moved_h: list[str]) -> None:
    """更新CMake、Keil、工具和文档中的APP路径。"""
    replacements: dict[str, str] = {}
    for name in moved_c:
        replacements[f"app/{name}"] = f"app/src/{name}"
        replacements[f"app\\{name}"] = f"app\\src\\{name}"
    for name in moved_h:
        replacements[f"app/{name}"] = f"app/inc/{name}"
        replacements[f"app\\{name}"] = f"app\\inc\\{name}"

    for path in ROOT.rglob("*"):
        if not path.is_file() or not is_text_file(path) or ".git" in path.parts:
            continue
        try:
            text = read_text(path)
        except UnicodeDecodeError:
            continue
        original = text
        for old, new in replacements.items():
            text = text.replace(old, new)
        text = text.replace("-Iapp ", "-Iapp/inc ")
        text = text.replace("-Iapp\n", "-Iapp/inc\n")
        text = text.replace('"-Iapp"', '"-Iapp/inc"')
        text = text.replace("..\\..\\app;", "..\\..\\app\\inc;")
        text = text.replace("../../app;", "../../app/inc;")
        if text != original:
            write_text(path, text)


def normalize_signature(signature: str) -> str:
    return re.sub(r"\s+", " ", signature.strip()).replace(" (", "(").replace(" * ", " *")


def input_description(params: str) -> str:
    params = re.sub(r"\s+", " ", params.strip())
    if not params or params == "void":
        return "无"
    names: list[str] = []
    for part in params.split(","):
        match = re.search(r"([A-Za-z_]\w*)\s*(?:\[[^\]]*\])?$", part.strip())
        names.append(match.group(1) if match else part.strip())
    return "；".join(f"{name}：见类型定义，调用者保证指针、单位和有效范围正确" for name in names) + "。"


def output_description(signature: str) -> str:
    normalized = normalize_signature(signature)
    return_type = normalized.rsplit(" ", 1)[0] if " " in normalized else normalized
    if re.search(r"\bvoid\b", return_type) and "*" not in return_type:
        return "无"
    if re.search(r"\bbool\b", return_type):
        return "true表示条件满足或操作成功；false表示条件不满足或操作失败。"
    if re.search(r"\b(?:int|status|result|error|err)\w*\b", return_type, re.I):
        return "见返回值枚举或错误码定义；调用者必须处理失败分支。"
    return "见返回类型定义。"


def function_description(module: str, name: str) -> str:
    lower = name.lower()
    module_desc = {
        "app": "应用编排",
        "measurement": "采样换算与测量快照",
        "mppt": "MPPT搜索和PV参考电压控制",
        "charger": "电池充电阶段与限值",
        "protection": "软件保护、去抖与故障锁存",
        "power_stage": "Boost预充、继电器和功率级状态",
        "ui": "运行/故障指示",
        "protocol": "UART协议解析与响应",
        "storage": "片内Flash参数记录",
        "service": "中断邮箱、主循环服务和驱动桥接",
        "board": "板级参数和硬件能力",
        "drv_adc": "ADC定时触发、多通道扫描与DMA",
        "drv_pwm": "单路Boost PWM、CCR预装载与Break",
        "drv_comp": "内部运放/比较器及快速故障链",
        "drv_flash": "片内Flash擦写",
        "drv_io": "GPIO、继电器和LED",
        "drv_system": "系统时钟、时间基准和复位",
        "drv_uart": "串口收发",
        "drv_watchdog": "独立看门狗",
        "main": "系统入口",
        "interrupts": "中断入口",
    }.get(module, f"{module}模块")

    if name == "main":
        return (
            "系统入口：依次完成安全输出、时钟、看门狗、GPIO、比较器、ADC/DMA、PWM和通信初始化；"
            "随后在主循环中处理ISR邮箱，并按周期运行采样换算、MPPT、充电、保护、继电器、通信、指示与Flash保存任务。"
        )
    if lower.endswith("_init") or lower.startswith("init_"):
        return f"初始化{module_desc}的上下文、默认参数和安全状态；本函数不绕过功率门禁直接发波。"
    if "irqhandler" in lower or lower.endswith("_handler") or "_isr" in lower:
        return f"{module_desc}中断入口：仅清除硬件标志、保存最小状态并投递事件；复杂计算由主循环Service完成。"
    if lower.endswith("_process") or lower.endswith("_service") or "_process_" in lower:
        return f"在主循环上下文处理{module_desc}事件，限制单次执行预算并更新对外状态。"
    if lower.endswith("_tick") or lower.endswith("_update") or lower.endswith("_run"):
        return f"按调度周期更新{module_desc}；所有输入均来自稳定快照，输出在提交前还需经过安全仲裁。"
    if lower.startswith("get_") or "_get_" in lower or lower.startswith("read_") or "_read_" in lower:
        return f"读取{module_desc}的当前结果或状态，不触发新的硬件采样和功率动作。"
    if lower.startswith("set_") or "_set_" in lower or lower.startswith("request_") or "_request_" in lower:
        return f"更新{module_desc}的请求值；实际硬件动作由Service在重新检查故障和门禁后执行。"
    if "fault" in lower or "protect" in lower:
        return f"处理{module_desc}条件；故障成立时优先进入安全态并撤销全部待提交Duty。"
    return f"完成{module_desc}中的“{name}”职责；函数只处理本模块数据，不越层访问其他硬件。"


def wrap_description(description: str, width: int = 92) -> list[str]:
    lines: list[str] = []
    current = ""
    for char in description:
        current += char
        if len(current) >= width and char in "；，。":
            lines.append(current)
            current = ""
    if current:
        lines.append(current)
    return lines or [""]


def function_header(signature: str, params: str, module: str, name: str) -> str:
    descriptions = wrap_description(function_description(module, name))
    lines = [
        "/*---------------------------------------------------------------------------*",
        f" * Name        : {normalize_signature(signature)}",
        f" * Input       : {input_description(params)}",
        f" * Output      : {output_description(signature)}",
        f" * Description : {descriptions[0]}",
    ]
    lines.extend(f" *               {line}" for line in descriptions[1:])
    lines.append(" *---------------------------------------------------------------------------*/")
    return "\n".join(lines) + "\n"


def add_function_headers(path: Path) -> int:
    """为生产函数增加统一中文函数头，不重复插入已有标准函数头。"""
    text = read_text(path)
    matches: list[re.Match[str]] = []
    for match in FUNC_RE.finditer(text):
        name = match.group("name")
        signature = normalize_signature(match.group("signature"))
        if name in CONTROL_NAMES or signature.startswith(("typedef ", "#")):
            continue
        before = text[max(0, match.start() - 800):match.start()]
        if "/*---------------------------------------------------------------------------*" in before and "Name        :" in before:
            last_header = before.rfind("/*---------------------------------------------------------------------------*")
            last_end = before.rfind("*/")
            if last_header >= 0 and last_end > last_header:
                continue
        matches.append(match)

    for match in reversed(matches):
        block = function_header(
            match.group("signature"), match.group("params"), path.stem, match.group("name")
        )
        text = text[:match.start()] + block + text[match.start():]
    write_text(path, text)
    return len(matches)


def strip_comments_and_strings(line: str) -> str:
    line = re.sub(r'"(?:\\.|[^"\\])*"', '""', line)
    line = re.sub(r"'(?:\\.|[^'\\])*'", "''", line)
    line = re.sub(r"//.*", "", line)
    return line


def move_late_static_data(path: Path) -> int:
    """将函数之后出现的文件级static数据定义移动到include之后。"""
    text = read_text(path)
    lines = text.splitlines(keepends=True)
    first_function = None
    for match in FUNC_RE.finditer(text):
        if match.group("name") not in CONTROL_NAMES:
            first_function = text.count("\n", 0, match.start())
            break
    if first_function is None:
        return 0

    depth = 0
    in_block_comment = False
    blocks: list[tuple[int, int, list[str]]] = []
    index = 0
    while index < len(lines):
        line = lines[index]
        clean = line
        if in_block_comment:
            if "*/" in clean:
                clean = clean.split("*/", 1)[1]
                in_block_comment = False
            else:
                index += 1
                continue
        if "/*" in clean:
            before, after = clean.split("/*", 1)
            clean = before
            if "*/" not in after:
                in_block_comment = True
        clean = strip_comments_and_strings(clean)
        start_depth = depth
        depth += clean.count("{") - clean.count("}")

        stripped = clean.strip()
        if index > first_function and start_depth == 0 and stripped.startswith("static "):
            prefix = stripped.split("=", 1)[0].split(";", 1)[0]
            if "(" not in prefix:
                end = index
                aggregate = line
                local_depth = clean.count("{") - clean.count("}")
                while ";" not in aggregate or local_depth > 0:
                    end += 1
                    if end >= len(lines):
                        break
                    aggregate += lines[end]
                    next_clean = strip_comments_and_strings(lines[end])
                    local_depth += next_clean.count("{") - next_clean.count("}")
                blocks.append((index, end + 1, lines[index:end + 1]))
                index = end + 1
                continue
        index += 1

    if not blocks:
        return 0
    for start, end, _ in reversed(blocks):
        del lines[start:end]
    include_end = 0
    for idx, line in enumerate(lines):
        if line.lstrip().startswith("#include"):
            include_end = idx + 1
    insertion = ["\n", "/* 文件级static数据：集中放置，便于审查生命周期和ISR共享关系。 */\n"]
    for _, _, block_lines in blocks:
        insertion.extend(block_lines)
        if block_lines and not block_lines[-1].endswith("\n"):
            insertion.append("\n")
    lines[include_end:include_end] = insertion
    write_text(path, "".join(lines))
    return len(blocks)


def add_semantic_comment(path: Path) -> None:
    """在关键控制分支前增加说明“为什么判断”和“失败如何处理”的中文注释。"""
    rules: dict[str, tuple[str, str]] = {
        "measurement": ("if (", "    /* 仅处理完整且有效的DMA采样块；无效通道不会进入控制计算。 */\n"),
        "mppt": ("if (", "    /* 根据功率—电压变化方向调整搜索步长，并在更新参考值前执行限幅。 */\n"),
        "charger": ("switch (", "    /* 按电池档案和当前充电阶段执行TC/CC/CV/浮充状态迁移。 */\n"),
        "protection": ("if (", "    /* 软件保护需连续满足去抖条件；硬件快速故障不等待本分支。 */\n"),
        "power_stage": ("switch (", "    /* 功率级状态机只生成逻辑命令；实际发波由Service再次检查故障后提交。 */\n"),
        "service": ("drv_pwm", "    /* 真正写PWM前必须重新检查故障、Break和功率门禁，防止中断返回后重开发波。 */\n"),
        "drv_pwm": ("duty", "    /* Duty统一表示低侧MOS物理导通比例；CCR仅写预装载并在下一自然UEV生效。 */\n"),
        "drv_flash": ("if (", "    /* 功率输出或继电器闭合期间禁止擦写Flash，避免取指阻塞破坏控制时序。 */\n"),
        "interrupts": ("void ", "/* 中断只执行清标志、紧急关波和事件投递；复杂业务统一延后到主循环。 */\n"),
    }
    rule = rules.get(path.stem)
    if rule is None:
        return
    token, comment = rule
    text = read_text(path)
    index = text.find(token)
    if index < 0 or comment.strip() in text:
        return
    line_start = text.rfind("\n", 0, index) + 1
    text = text[:line_start] + comment + text[line_start:]
    write_text(path, text)


def macro_comment(name: str) -> str:
    special = {
        "BOARD_POWER_OUTPUT_ALLOWED": "功率输出总门禁：全部发布证据闭环前必须保持0。",
        "BOARD_GATE_PINMAP_CONFIRMED": "PinMap人工确认门禁：无重复网络和复用冲突后才允许置1。",
        "BOARD_GATE_TARGET_BINDING_VERIFIED": "目标外设绑定门禁：ATMR、ADC、DMA、COMP、GPIO、Flash和WDT实编译验证后置1。",
        "BOARD_GATE_ANALOG_CALIBRATED": "模拟标定门禁：ADC比例、零点、偏置和极性完成实板校准后置1。",
        "BOARD_GATE_COMP_POLARITY_VERIFIED": "比较器门禁：确认U6 EN、Break和有效极性后置1。",
        "BOARD_GATE_FLASH_ERASE_VERIFIED": "Flash门禁：确认页大小、擦除范围和掉电恢复后置1。",
        "BOARD_GATE_KEIL_WARNING_FREE": "Keil门禁：ARM Compiler 6达到0 Error、0 Warning后置1。",
        "BOARD_GATE_LOW_VOLTAGE_BENCH": "低压台架门禁：首脉冲、CCR更新、Break和继电器时序通过后置1。",
    }
    upper = name.upper()
    if upper in special:
        return special[upper]
    if upper.endswith("_MS") or "_MS_" in upper:
        return "时间参数，单位：ms。"
    if upper.endswith("_US") or "_US_" in upper:
        return "时间参数，单位：us。"
    if upper.endswith("_MV") or "_MV_" in upper:
        return "电压参数，单位：mV。"
    if upper.endswith("_MA") or "_MA_" in upper:
        return "电流参数，单位：mA。"
    if upper.endswith("_MW") or "_MW_" in upper:
        return "功率参数，单位：mW。"
    if upper.endswith("_HZ") or "_HZ_" in upper:
        return "频率参数，单位：Hz。"
    if "Q15" in upper:
        return "Q15定点参数，物理意义和限值见所在模块。"
    if upper.endswith("_ENABLE") or "_ENABLE_" in upper:
        return "功能开关：1表示启用，0表示禁用。"
    if upper.endswith("_MASK") or "_MASK_" in upper:
        return "位掩码，用于筛选或记录对应状态。"
    if upper.endswith("_ADDR") or "ADDRESS" in upper:
        return "地址配置，修改前核对链接脚本和存储布局。"
    return "配置常量，作用和单位见名称及所在模块。"


def comment_header_macros(path: Path) -> None:
    """为头文件宏补充用途和单位，跳过include guard。"""
    lines = read_text(path).splitlines()
    output: list[str] = []
    pattern = re.compile(r"^(\s*)#\s*define\s+([A-Za-z_]\w*)(.*)$")
    for line in lines:
        match = pattern.match(line)
        if match is None:
            output.append(line)
            continue
        indent, name, _ = match.groups()
        if name.endswith(("_H", "_H_")) or name.startswith("__"):
            output.append(line)
            continue
        previous = output[-1].strip() if output else ""
        if "/*" in line or "//" in line or previous.startswith(("/*", "//")):
            output.append(line)
            continue
        comment = macro_comment(name)
        if line.rstrip().endswith("\\"):
            output.append(f"{indent}/** {comment} */")
            output.append(line)
        else:
            output.append(f"{line}  /**< {comment} */")
    write_text(path, "\n".join(output) + "\n")


def add_safety_gates() -> None:
    """承接v0.6未闭环项并固定功率总门禁。"""
    path = ROOT / "board/board_config.h"
    text = read_text(path)
    block = r'''

/*---------------------------------------------------------------------------*
 * Name        : 功率输出发布门禁
 * Input       : 无
 * Output      : 编译期常量
 * Description : 以下门禁对应旧版审计仍未闭环的软硬件确认项；任何一项为0时，功率输出总门禁必须保持关闭。
 *---------------------------------------------------------------------------*/
#ifndef BOARD_GATE_PINMAP_CONFIRMED
#define BOARD_GATE_PINMAP_CONFIRMED          (0U)  /**< PinMap人工确认门禁：确认无重复网络和复用冲突后才允许置1。 */
#endif
#ifndef BOARD_GATE_TARGET_BINDING_VERIFIED
#define BOARD_GATE_TARGET_BINDING_VERIFIED   (0U)  /**< 目标外设绑定门禁：ATMR、ADC、DMA、COMP、GPIO、Flash和WDT实编译验证后才允许置1。 */
#endif
#ifndef BOARD_GATE_ANALOG_CALIBRATED
#define BOARD_GATE_ANALOG_CALIBRATED         (0U)  /**< 模拟量标定门禁：ADC比例、零点、偏置和极性完成实板校准后才允许置1。 */
#endif
#ifndef BOARD_GATE_COMP_POLARITY_VERIFIED
#define BOARD_GATE_COMP_POLARITY_VERIFIED    (0U)  /**< 比较器极性门禁：确认故障有效电平和U6 EN/Break链路后才允许置1。 */
#endif
#ifndef BOARD_GATE_FLASH_ERASE_VERIFIED
#define BOARD_GATE_FLASH_ERASE_VERIFIED      (0U)  /**< Flash门禁：确认页大小、擦除范围、掉电恢复和链接区隔离后才允许置1。 */
#endif
#ifndef BOARD_GATE_KEIL_WARNING_FREE
#define BOARD_GATE_KEIL_WARNING_FREE         (0U)  /**< Keil门禁：ARM Compiler 6达到0 Error、0 Warning后才允许置1。 */
#endif
#ifndef BOARD_GATE_LOW_VOLTAGE_BENCH
#define BOARD_GATE_LOW_VOLTAGE_BENCH         (0U)  /**< 低压台架门禁：首脉冲、CCR预装载、Break和继电器时序全部通过后才允许置1。 */
#endif

#ifdef BOARD_POWER_OUTPUT_ALLOWED
#undef BOARD_POWER_OUTPUT_ALLOWED
#endif
#define BOARD_POWER_OUTPUT_ALLOWED           (0U)  /**< 功率输出总门禁：当前版本固定关闭，禁止只改一个阈值就解锁。 */

#if (BOARD_POWER_OUTPUT_ALLOWED != 0U) && \
    ((BOARD_GATE_PINMAP_CONFIRMED == 0U) || \
     (BOARD_GATE_TARGET_BINDING_VERIFIED == 0U) || \
     (BOARD_GATE_ANALOG_CALIBRATED == 0U) || \
     (BOARD_GATE_COMP_POLARITY_VERIFIED == 0U) || \
     (BOARD_GATE_FLASH_ERASE_VERIFIED == 0U) || \
     (BOARD_GATE_KEIL_WARNING_FREE == 0U) || \
     (BOARD_GATE_LOW_VOLTAGE_BENCH == 0U))
#error "Power output cannot be enabled before all release gates pass."
#endif
'''
    if "BOARD_GATE_PINMAP_CONFIRMED" not in text:
        index = text.rfind("#endif")
        text = text[:index] + block + "\n" + text[index:] if index >= 0 else text + block
    text = re.sub(
        r"(?m)^\s*#define\s+BOARD_POWER_OUTPUT_ALLOWED\s+[^\n]+$",
        "#define BOARD_POWER_OUTPUT_ALLOWED           (0U)  /**< 功率输出总门禁：当前版本固定关闭。 */",
        text,
    )
    write_text(path, text)


def regenerate_cmake() -> None:
    write_text(
        ROOT / "CMakeLists.txt",
        '''cmake_minimum_required(VERSION 3.20)
project(aurora_control_fw C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()
file(GLOB APP_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/app/src/*.c")

add_executable(aurora_host_tests
    ${APP_SOURCES}
    service/service.c
    board/board.c
    tests/mock_driver.c
    tests/test_main.c
)

target_include_directories(aurora_host_tests PRIVATE
    app/inc
    service
    driver
    board
    tests
)

target_compile_definitions(aurora_host_tests PRIVATE AURORA_HOST_TEST=1)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(aurora_host_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
        -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Werror
    )
endif()

add_test(NAME aurora_host_tests COMMAND aurora_host_tests)
''',
    )


def regenerate_tools() -> None:
    write_text(ROOT / "tools/check_architecture.py", '''#!/usr/bin/env python3
from pathlib import Path
import re
import sys
ROOT = Path(__file__).resolve().parents[1]
errors = []
for rel in ["app/inc", "app/src", "service", "driver", "board", "project/keil", "docs", "tests", "tools"]:
    if not (ROOT / rel).is_dir(): errors.append(f"缺少目录：{rel}")
for forbidden in ["firmware", "tasks", "legacy_reference", "legacy_parity", "legacy_protocol_import", ".bootstrap"]:
    if (ROOT / forbidden).exists(): errors.append(f"禁止目录仍存在：{forbidden}")
if any((ROOT / "app/inc").glob("*.c")): errors.append("app/inc只能放.h")
if any((ROOT / "app/src").glob("*.h")): errors.append("app/src只能放.c")
for path in (ROOT / "docs").rglob("*"):
    if path.is_file() and path.suffix.lower() in {".c", ".h", ".json"}: errors.append(f"docs中出现代码或JSON：{path.relative_to(ROOT)}")
chip = re.compile(r"g32f031|apm32|ht32|ddl_|dal_|GPIO[AB]|ATMR", re.I)
for path in (ROOT / "app/src").glob("*.c"):
    if chip.search(path.read_text(encoding="utf-8")): errors.append(f"APP越层依赖芯片符号：{path.relative_to(ROOT)}")
board_cfg = (ROOT / "board/board_config.h").read_text(encoding="utf-8")
if not re.search(r"#define\s+BOARD_POWER_OUTPUT_ALLOWED\s+\(0U\)", board_cfg): errors.append("功率总门禁必须固定为0")
for gate in ["BOARD_GATE_PINMAP_CONFIRMED", "BOARD_GATE_TARGET_BINDING_VERIFIED", "BOARD_GATE_ANALOG_CALIBRATED", "BOARD_GATE_COMP_POLARITY_VERIFIED", "BOARD_GATE_FLASH_ERASE_VERIFIED", "BOARD_GATE_KEIL_WARNING_FREE", "BOARD_GATE_LOW_VOLTAGE_BENCH"]:
    if gate not in board_cfg: errors.append(f"缺少门禁：{gate}")
for doc in ["docs/README.md", "docs/GUIDE.md", "docs/16-参数配置与硬件确认清单.md"]:
    if not (ROOT / doc).is_file(): errors.append(f"缺少文档：{doc}")
if errors:
    print("ARCHITECTURE CHECK: FAIL")
    print("\n".join(f"- {e}" for e in errors))
    sys.exit(1)
print("ARCHITECTURE CHECK: PASS")
''')
    write_text(ROOT / "tools/check_code_style.py", '''#!/usr/bin/env python3
from pathlib import Path
import re
import sys
ROOT = Path(__file__).resolve().parents[1]
errors = []
production = [*(ROOT / "app/src").glob("*.c"), *(ROOT / "service").glob("*.c"), *(ROOT / "driver").glob("*.c"), *(ROOT / "board").glob("*.c"), *(ROOT / "project/keil").glob("*.c")]
func_re = re.compile(r"(?m)^(?:static\s+)?[A-Za-z_][^;{}]*\([^;{}]*\)\s*\n?\s*\{")
for path in production:
    text = path.read_text(encoding="utf-8")
    for match in func_re.finditer(text):
        before = text[max(0, match.start() - 900):match.start()]
        if "/*---------------------------------------------------------------------------*" not in before or "Name        :" not in before:
            errors.append(f"缺少标准函数头：{path.relative_to(ROOT)}:{text.count(chr(10), 0, match.start()) + 1}")
    for no, line in enumerate(text.splitlines(), 1):
        if "\t" in line: errors.append(f"禁止Tab：{path.relative_to(ROOT)}:{no}")
headers = [*(ROOT / "app/inc").glob("*.h"), *(ROOT / "board").glob("*.h"), *(ROOT / "service").glob("*.h"), *(ROOT / "driver").glob("*.h")]
for path in headers:
    lines = path.read_text(encoding="utf-8").splitlines()
    for no, line in enumerate(lines, 1):
        m = re.match(r"\s*#\s*define\s+([A-Za-z_]\w*)", line)
        if not m: continue
        name = m.group(1)
        if name.endswith(("_H", "_H_")) or name.startswith("__"): continue
        prev = lines[no - 2].strip() if no >= 2 else ""
        if "/*" not in line and "//" not in line and not prev.startswith(("/*", "//")): errors.append(f"宏缺少备注：{path.relative_to(ROOT)}:{no}:{name}")
if errors:
    print("CODE STYLE CHECK: FAIL")
    print("\n".join(f"- {e}" for e in errors[:200]))
    sys.exit(1)
print("CODE STYLE CHECK: PASS")
''')
    write_text(ROOT / "tools/run_checks.py", '''#!/usr/bin/env python3
from pathlib import Path
import shutil
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[1]
def run(cmd):
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=ROOT, check=True)
run([sys.executable, "tools/check_architecture.py"])
run([sys.executable, "tools/check_code_style.py"])
for compiler, build_dir in [("gcc", "build-gcc"), ("clang", "build-clang")]:
    if shutil.which(compiler) is None:
        print(f"SKIP: {compiler}未安装")
        continue
    shutil.rmtree(ROOT / build_dir, ignore_errors=True)
    run(["cmake", "-S", ".", "-B", build_dir, "-G", "Ninja", f"-DCMAKE_C_COMPILER={compiler}"])
    run(["cmake", "--build", build_dir])
    run(["ctest", "--test-dir", build_dir, "--output-on-failure"])
print("ALL CHECKS PASSED")
''')


def write_docs() -> None:
    write_text(ROOT / "docs/README.md", '''# 文档入口

本目录只保存开发、审查、调试和交接文档，不保存 `.c/.h` 源码或自动生成JSON。

建议阅读：`00-文档索引.md` → `01-工程概览与目录说明.md` → `GUIDE.md` → `16-参数配置与硬件确认清单.md` → `17-v0.7.1代码规范与注释整改报告.md`。

所有功率发布门未闭环前：

```c
BOARD_POWER_OUTPUT_ALLOWED == 0U
```
''')
    write_text(ROOT / "docs/GUIDE.md", '''# Codex / 开发者操作指南

## 目录

- `app/inc/`：APP头文件、类型和配置宏；
- `app/src/`：APP实现；
- `service/`：中断邮箱和APP↔Driver唯一桥接；
- `driver/`：芯片驱动；
- `board/`：PinMap、极性、比例和门禁；
- `docs/`：编号文档，禁止放C/H和JSON；
- `tests/`、`tools/`：不进入目标镜像。

## 代码规范

所有生产函数使用统一 `Name/Input/Output/Description` 函数头；复杂判断说明判断目的和失败后的安全动作；头文件宏注明作用和单位；文件级`static`数据统一放在include之后、首个函数之前；参数必须先在头文件定义再使用。

## 中断和PWM红线

ISR只清标志、紧急关波、交换DMA块和投递事件。ADC由定时器触发并一次扫描多通道，主循环只读取完整块。Duty写CCR preload并在下一自然UEV生效，运行中禁止软件UG。发波前重新检查故障、Break和epoch；故障时清除全部待提交Duty。只有健康监督器可以喂看门狗；功率运行或继电器闭合时禁止Flash擦写。

## 必跑命令

```bash
python tools/check_architecture.py
python tools/check_code_style.py
python tools/run_checks.py
```

随后在Windows Keil ARM Compiler 6执行Clean + Rebuild All，目标为0 Error / 0 Warning。未执行的证据必须明确写“未执行”。
''')
    write_text(ROOT / "docs/16-参数配置与硬件确认清单.md", '''# 16 · 参数配置与硬件确认清单

所有参数必须先在头文件命名定义，再在实现中使用，禁止在`.c`中散落魔法数字。

| 参数类别 | 定义位置 | 修改与验证要求 |
|---|---|---|
| 300W/120W功率档 | `app/inc/app_config.h` | 核对MOS、二极管、电感、电容和散热BOM |
| PWM频率、Duty和斜率 | `board/board_config.h`、`app/inc/app_config.h` | 示波器确认首脉冲、CCR preload和自然UEV更新 |
| ADC触发、DMA块和扫描顺序 | `board/board_config.h` | 避开尖峰，确认建立时间、通道顺序和缓冲所有权 |
| PV_U/BAT_U/BST_U比例 | `board/board_config.h` | 标准源逐点标定 |
| PV_I零点、增益和方向 | `board/board_config.h` | 核对分流器、OPA增益、偏置和正方向 |
| NTC和温度阈值 | `board/board_config.h`、`app/inc/app_config.h` | 按NTC/B值和实测温升校准 |
| COMP0/COMP2阈值与极性 | `board/board_config.h`、`driver/drv_comp.c` | 确认U6 EN、Break、有效电平和传播时间 |
| MPPT周期、步长、PI | `app/inc/app_config.h` | PV模拟器验证稳态、突变和弱光 |
| 48/60/72V及电池档案 | `app/inc/app_config.h`、`app/src/charger.c` | 对照BMS逐项复核CC/CV/浮充/尾流 |
| 电池电流效率估算 | `app/inc/app_config.h`、`app/src/measurement.c` | 按实测效率拟合，必须标记ESTIMATED |
| 继电器压差和预充 | `app/inc/app_config.h` | 确认BST_U/BAT_U压差、触点弹跳和防反灌 |
| 无发电断开时间 | `app/inc/app_config.h` | 当前30分钟，核对功率阈值和恢复滞回 |
| UART和协议超时 | `app/inc/app_config.h`、`board/board_config.h` | 逐命令、逐字节Golden Vector |
| 看门狗窗口 | `board/board_config.h`、`service/service.c` | 实测复位时间和单任务卡死检测 |
| Flash A/B页 | `board/board_config.h`、`driver/drv_flash.c` | 核对页大小、Scatter、擦除范围和掉电恢复 |

当前全部门禁保持0：`BOARD_GATE_PINMAP_CONFIRMED`、`BOARD_GATE_TARGET_BINDING_VERIFIED`、`BOARD_GATE_ANALOG_CALIBRATED`、`BOARD_GATE_COMP_POLARITY_VERIFIED`、`BOARD_GATE_FLASH_ERASE_VERIFIED`、`BOARD_GATE_KEIL_WARNING_FREE`、`BOARD_GATE_LOW_VOLTAGE_BENCH`、`BOARD_POWER_OUTPUT_ALLOWED`。
''')
    write_text(ROOT / "docs/17-v0.7.1代码规范与注释整改报告.md", '''# 17 · v0.7.1 代码规范与注释整改报告

## 已整改

- APP调整为`app/inc/*.h`和`app/src/*.c`；
- 生产函数增加统一函数头，关键判断补充中文设计注释；
- 配置宏补充用途和单位；
- 文件级`static`数据集中到文件头；
- 增加4空格`.clang-format`；
- 增加`docs/README.md`、`docs/GUIDE.md`和参数清单；
- 增加目录、分层、注释、宏和功率门禁检查。

## v0.6问题承接

PinMap重复网络和ATMR/ADC/COMP/GPIO/Flash目标绑定仍是P0，在人工和实板证据完成前门禁保持0。Keil AC6的134 Warning仍是P1；当前Linux Host检查不能替代Windows Keil，必须达到0 Error / 0 Warning后再置门禁。Host GCC/Clang在CI环境执行完整CMake+CTest。缺少docs/README和GUIDE的问题已解决。APP越层、legacy污染、CMake安全依赖、Flash原型、WDT绑定、Flash布局和故障后Duty残留继续由静态门禁、目标编译和台架清单审查。

本轮不解锁功率输出。
''')
    write_text(ROOT / "docs/18-REF-v0.6.0修改经验与问题处理总结.md", '''# 18 · REF · v0.6.0修改经验与问题处理总结

历史审查确认：PinMap重复网络、目标外设绑定、比较器极性、ADC比例、Flash擦除行为和板级波形不能仅靠Host测试确认；Keil需达到0 Error / 0 Warning；Host必须实际完成GCC/Clang配置、构建和CTest；故障后必须清除Duty残留并阻止低优先级代码重新发波；WDT应采用健康票据而非主循环盲喂。

本文件作为历史经验摘要，当前发布状态以`16-参数配置与硬件确认清单.md`和`17-v0.7.1代码规范与注释整改报告.md`为准。
''')
    index = ROOT / "docs/00-文档索引.md"
    text = read_text(index).rstrip() + "\n\n" if index.exists() else "# 00 · 文档索引\n\n"
    for item in [
        "- [README](README.md)：文档入口；",
        "- [GUIDE](GUIDE.md)：Codex和开发者操作指南；",
        "- [16-参数配置与硬件确认清单](16-参数配置与硬件确认清单.md)：实物参数和发布门禁；",
        "- [17-v0.7.1代码规范与注释整改报告](17-v0.7.1代码规范与注释整改报告.md)：本轮整改；",
        "- [18-REF-v0.6.0修改经验与问题处理总结](18-REF-v0.6.0修改经验与问题处理总结.md)：历史经验。",
    ]:
        if item not in text:
            text += item + "\n"
    write_text(index, text)


def write_root_files() -> None:
    write_text(ROOT / ".clang-format", '''---
BasedOnStyle: LLVM
IndentWidth: 4
ContinuationIndentWidth: 4
TabWidth: 4
UseTab: Never
BreakBeforeBraces: Allman
ColumnLimit: 120
AlignConsecutiveMacros: Consecutive
AllowShortFunctionsOnASingleLine: None
AllowShortIfStatementsOnASingleLine: Never
SortIncludes: CaseSensitive
...
''')
    write_text(ROOT / "AGENTS.md", '''# AGENTS.md

本仓库为单路异步Boost充电控制固件。`app/inc`放头文件，`app/src`放实现；Service是APP和Driver唯一桥接；APP不得包含芯片符号。使用4空格，生产函数必须使用标准函数头，关键判断解释安全目的，宏注明作用/单位，文件级static数据放文件头，参数先定义后使用。

安全红线：`BOARD_POWER_OUTPUT_ALLOWED`保持0；ISR只清标志、硬关波、交换DMA块和投递事件；Duty使用CCR preload并在下一自然UEV生效；发波前重新检查故障、Break和epoch；故障时撤销全部Duty；只有健康监督器喂狗；功率运行或继电器闭合时禁止Flash擦写。

修改后运行：`python tools/check_architecture.py`、`python tools/check_code_style.py`、`python tools/run_checks.py`。Keil和台架未执行时不得写成通过。
''')
    write_text(ROOT / "README.md", '''# Aurora Control Firmware

可移植的单路异步Boost充电控制固件。默认高功率BOM，保留低功率编译档，支持48/60/72V及多类电池档案。

```text
app/inc/       APP头文件、类型和配置宏
app/src/       采样、MPPT、充电、保护、功率级、UI、协议和存储
service/       中断邮箱和APP↔Driver桥接
driver/        芯片驱动
board/         引脚、极性、模拟比例和发布门禁
vendor/        构建必需厂商库
project/keil/  Keil ARM Compiler 6工程
docs/          编号文档、参数清单和交接指南
tests/         Host回归，不进入目标镜像
tools/         架构、风格和构建门禁
```

验证：`python tools/check_architecture.py`、`python tools/check_code_style.py`、`python tools/run_checks.py`。

当前`BOARD_POWER_OUTPUT_ALLOWED == 0U`。PinMap、目标绑定、模拟标定、比较器、Flash、Keil 0 Warning和低压台架全部闭环前不得解锁。文档入口见`docs/README.md`，Codex交接见`docs/GUIDE.md`。
''')


def format_sources(production: list[Path]) -> None:
    clang_format = shutil.which("clang-format")
    if clang_format is None:
        return
    headers = [*(ROOT / "app/inc").glob("*.h"), *(ROOT / "service").glob("*.h"), *(ROOT / "driver").glob("*.h"), *(ROOT / "board").glob("*.h")]
    subprocess.run([clang_format, "-i", *[str(path) for path in production + headers]], cwd=ROOT, check=True)


def regenerate_manifest() -> None:
    for build_dir in ROOT.glob("build-*"):
        if build_dir.is_dir():
            shutil.rmtree(build_dir)
    manifest = ROOT / "MANIFEST.sha256"
    if manifest.exists():
        manifest.unlink()
    lines: list[str] = []
    for path in sorted(p for p in ROOT.rglob("*") if p.is_file() and ".git" not in p.parts and not any(part.startswith("build-") for part in p.parts)):
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        lines.append(f"{digest}  {path.relative_to(ROOT).as_posix()}")
    write_text(manifest, "\n".join(lines) + "\n")


def main() -> None:
    moved_c, moved_h = reorganize_app()
    update_paths(moved_c, moved_h)
    regenerate_cmake()
    add_safety_gates()

    production = [
        *(ROOT / "app/src").glob("*.c"),
        *(ROOT / "service").glob("*.c"),
        *(ROOT / "driver").glob("*.c"),
        *(ROOT / "board").glob("*.c"),
        *(ROOT / "project/keil").glob("*.c"),
    ]
    for path in production:
        move_late_static_data(path)
        add_function_headers(path)
        add_semantic_comment(path)

    for directory in [ROOT / "app/inc", ROOT / "service", ROOT / "driver", ROOT / "board"]:
        for path in directory.glob("*.h"):
            comment_header_macros(path)

    write_docs()
    write_root_files()
    regenerate_tools()
    format_sources(production)
    regenerate_manifest()
    print("v0.7.1 refactor completed")


if __name__ == "__main__":
    main()
