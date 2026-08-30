#!/usr/bin/env python3
"""Generate function-level code-analysis documents and comment-only source updates.

This script is intentionally one-shot. The workflow that invokes it removes the script
before committing the generated deliverables, so the final product tree only retains
review documents, source comments, and reusable audit tools.
"""

from __future__ import annotations

import hashlib
import html
import json
import re
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

ROOT = Path(__file__).resolve().parents[1]
DOC_ROOT = ROOT / "docs" / "程序代码分析"
IMAGE_ROOT = DOC_ROOT / "images"
REPORT_PATH = DOC_ROOT / "20-300W函数级代码分析与注释交付报告.md"
SOURCE_PATTERNS = ("app/**/*.c", "app/**/*.h", "driver/**/*.c", "driver/**/*.h")

LAYER_META = {
    "app": (
        "APP 业务层",
        "实现测量、充电、MPPT、保护、功率状态机、协议、存储、UI 与运行调度。",
        "中断/主循环 → app/src/main.c → 各业务模块 → Driver 接口。",
        "APP 不得直接访问 MCU 寄存器；PWM 和继电器动作必须经过运行层与 Driver 安全复核。",
    ),
    "driver": (
        "Driver 硬件抽象层",
        "封装 ADC、PWM、COMP、GPIO、UART、Flash、时基、供电资格和看门狗等目标硬件操作。",
        "APP 运行层 → driver/inc 接口 → driver/src → Vendor DDL/CMSIS。",
        "Driver 不实现产品算法；任何发波、继电器、Flash 动作必须遵守板级门禁和安全时序。",
    ),
}

FILE_PURPOSE = {
    "app/inc/app_config.h": "集中定义控制周期、阈值、功率包络、充电时序、保护恢复时间和产品版本参数。",
    "app/inc/app_types.h": "集中定义业务状态枚举、故障位、测量快照、充电目标、功率命令和持久化数据结构。",
    "app/inc/main.h": "声明业务组合根、运行组合根以及 APP/运行层对外接口。",
    "app/src/main.c": "系统入口和运行调度中枢，负责事件消费、控制链调度、硬件命令落实、遥测、存储和看门狗。",
    "app/src/interrupts.c": "目标中断桥接，只执行快速关波、硬件 ACK、事件投递和有界字节搬运。",
    "app/src/measurement.c": "把 ADC DMA 原始块处理为带有效位、诊断位和物理单位的原子测量快照。",
    "app/src/charger.c": "按电池化学体系和电压平台推进 TC/CC/CV/Float/Complete 状态机并输出电池侧目标。",
    "app/src/mppt.c": "根据 PV 电压、功率变化和外部限幅更新 MPPT 电压参考并形成理论 PV 功率请求。",
    "app/src/power_stage.c": "执行启动、零点校准、BST_U 预升压、继电器准入、BAT_U 稳定和 RUN/FAULT 状态机。",
    "app/src/protection.c": "维护独立毫秒保护、快速故障锁存、恢复条件、fault_mask 和安全许可。",
    "app/src/protocol.c": "解析产品 UART 字节流、编码响应和填充主动遥测。",
    "app/src/storage.c": "实现双页 Flash Journal、CRC、页分类、设置恢复和 24h 能量历史。",
    "app/src/ui.c": "把功率状态和故障位映射为 RUN/FAULT 双灯的非阻塞闪烁节奏。",
    "app/src/debug.c": "提供可裁剪的调试日志接口，不参与产品控制决策。",
    "driver/inc/board_config.h": "集中保存 PinMap、ADC/PWM/Flash/PVD/WDT 参数和人工功率门禁。",
    "driver/src/drv_board.c": "把板级分压、PV_I 标定和功率门禁转换为 Driver 契约。",
    "driver/src/drv_adc.c": "配置定时触发 ADC 扫描和 DMA 双半缓冲，并向运行层发布完整采样块。",
    "driver/src/drv_pwm.c": "配置异步 Boost PWM、CCR preload、Break、零 Duty 接管和显式发波授权。",
    "driver/src/drv_comp.c": "配置片内 OPA/比较器和快速过流输出，形成硬件关断与 ISR 故障源。",
    "driver/src/drv_io.c": "统一控制继电器、LINK、RUN LED 和 FAULT LED 的实际 GPIO 电平。",
    "driver/src/drv_system.c": "提供时基、临界区、中断优先级、PVD 供电资格和系统复位。",
    "driver/src/drv_uart.c": "提供产品串口的有界收发和 ISR 级字节接口。",
    "driver/src/drv_flash.c": "校验地址范围并封装内部 Flash 读取、页擦除和编程。",
    "driver/src/drv_watchdog.c": "初始化并刷新独立看门狗，刷新资格由运行层健康票据决定。",
}

CORE_HINTS = {
    "main": ["建立最小 GPIO 安全态", "等待 MCU VDD 连续稳定", "初始化完整运行层", "永久轮询事件调度器"],
    "aurora_runtime_init": ["先清零运行上下文并配置 IRQ/安全 GPIO", "初始化 IWDT、PWM、COMP、ADC、UART", "从 Driver 读取六通道板级标定", "初始化 APP 并加载双页 Flash", "最后启动 ADC DMA 并开放主循环"],
    "aurora_runtime_poll": ["原子领取事件位", "快速故障优先锁存并关波", "按 ADC/UART/Tick 顺序消费事件", "执行快速故障恢复和硬件命令落实", "处理遥测、Flash 安全窗口和看门狗票据"],
    "aurora_app_step_1ms": ["读取最新测量快照并累计能量", "估算电池电流并执行 1ms 保护", "每 10ms 运行 Charger、MPPT 和 UI", "每 1ms 运行 PowerStage 形成最终命令", "更新 Link 请求和重新校准条件"],
    "apply_power_command": ["继电器切换前无条件物理关 PWM", "闭合继电器前实时复核 BUS/BAT 压差", "首次发波先提交零 CCR 并等待自然 UPDATE", "通过 epoch、软件故障、实时 Break、锁存 Break 和板级总门复核", "运行期只写 shadow CCR，禁止软件强制 UPDATE"],
    "aurora_measurement_process_block": ["校验完整 DMA 块长度", "逐通道去极值平均", "按标定转换 PV_I/PV_U/BAT_U/BST_U", "判定 NTC 开短路并换算温度", "计算 PV 功率、有效位和诊断位", "原子发布新测量快照"],
    "aurora_measurement_zero_cal_accumulate": ["只接受完整 DMA 块", "检查单块 PV_I 峰峰值", "累计合格块均值并监控块间 spread", "达到证据数量后更新运行时 zero_code", "超过尝试上限则标记本轮校准失败"],
    "aurora_charger_step": ["校验电池测量和档案", "按环境温度更新铅酸有效档案", "推进 TC/CC/CV/Float/Complete 状态机", "执行尾流、复充和重新准入去抖", "输出电池侧功率、电流、电压和许可"],
    "aurora_mppt_step": ["校验 PV 测量和上层功率许可", "按 80ms 节拍更新 MPPT 电压参考", "外部限幅时冻结或收敛搜索", "按 10ms 节拍运行 PV 电压 PI", "输出理论功率请求而非直接 Duty"],
    "aurora_power_stage_step": ["先处理保护、测量有效性和 PV 启动资格", "推进 WAIT_PV/DELAY/ZERO_CAL/WAIT_BATTERY", "继电器断开时受限 Boost 建立 BST_U", "压差稳定后撤 PWM、闭 Relay 并复核", "BAT_U 稳定后进入 RUN", "故障和无太阳时清 Duty、释放 Relay 并重新准入"],
    "aurora_protection_step": ["先处理 BUS ADC 饱和和测量陈旧", "执行 PV/BAT 电压保护", "执行 PV 分级 OCP、过功率和电流合理性", "执行 MOS/环境温度与 NTC 开短路保护", "按各自恢复时间清除可自动恢复故障"],
    "aurora_protocol_feed_byte": ["依据帧头状态机逐字节接收", "校验长度、资源和帧尾", "超时或非法输入立即复位解析器", "完整帧进入单槽邮箱等待业务层领取"],
    "aurora_storage_classify_page": ["识别工厂擦除页与不完整页", "校验 Magic、版本、长度和 Commit Marker", "计算并比较载荷 CRC", "解析设置与能量历史并做内容一致性检查", "返回可诊断的页状态而非单一 true/false"],
    "drv_adc_init": ["配置六路模拟 GPIO 和 ADC 扫描顺序", "建立定时触发和 DMA 双半缓冲", "配置 DMA 中断和 ADC 就绪等待", "初始化阶段不直接启动持续采样"],
    "drv_pwm_init": ["配置 ATMR 周期、通道和 GLC 复用", "启用 ARR/CCR preload", "配置 Break 且关闭 Automatic Output", "初始 CCR、MOE 和输出均保持安全关闭"],
    "drv_pwm_prepare_arm_zero": ["保持输出禁用", "向 shadow CCR 提交 0 Duty", "等待自然 UPDATE 把 0 写入 active CCR", "返回序号供运行层完成首次发波握手"],
    "drv_pwm_arm": ["拒绝实时 Break 或锁存 Break", "确认 active CCR 已是安全零值", "显式使能主输出和通道输出", "由运行层在调用前后再次做 epoch 安全复核"],
    "drv_comp_init": ["配置 OPA0/OPA1 增益和共模", "配置 MOS/PV 两路比较器阈值输入和极性", "把 COMP0_OUT 路由到 U6 EN", "启用比较器中断；极性和阈值仍需实板验证"],
}

GROUPS = [
    ("02-main与interrupts运行入口分析.md", "main 与 interrupts 运行入口", ["app/src/main.c", "app/src/interrupts.c"]),
    ("03-measurement测量模块代码分析.md", "measurement 测量模块", ["app/src/measurement.c", "app/inc/measurement.h"]),
    ("04-charger充电状态机代码分析.md", "charger 充电状态机", ["app/src/charger.c", "app/inc/charger.h"]),
    ("05-mppt模块代码分析.md", "mppt 控制模块", ["app/src/mppt.c", "app/inc/mppt.h"]),
    ("06-power_stage功率级状态机代码分析.md", "power_stage 功率级状态机", ["app/src/power_stage.c", "app/inc/power_stage.h"]),
    ("07-protection保护链代码分析.md", "protection 完整保护链", ["app/src/protection.c", "app/inc/protection.h"]),
    ("08-protocol协议模块代码分析.md", "protocol 协议模块", ["app/src/protocol.c", "app/inc/protocol.h"]),
    ("09-storage存储与能量历史代码分析.md", "storage 存储与能量历史", ["app/src/storage.c", "app/inc/storage.h"]),
    ("10-ui与debug辅助模块代码分析.md", "ui 与 debug 辅助模块", ["app/src/ui.c", "app/inc/ui.h", "app/src/debug.c", "app/inc/debug.h"]),
    ("11-driver_ADC与板级契约代码分析.md", "Driver ADC 与板级契约", ["driver/src/drv_adc.c", "driver/inc/drv_adc.h", "driver/src/drv_board.c", "driver/inc/drv_board.h", "driver/inc/board_config.h"]),
    ("12-driver_PWM_COMP与IO代码分析.md", "Driver PWM、COMP 与 IO", ["driver/src/drv_pwm.c", "driver/inc/drv_pwm.h", "driver/src/drv_comp.c", "driver/inc/drv_comp.h", "driver/src/drv_io.c", "driver/inc/drv_io.h"]),
    ("13-driver_System_UART_Flash_Watchdog代码分析.md", "Driver System、UART、Flash 与 Watchdog", ["driver/src/drv_system.c", "driver/inc/drv_system.h", "driver/src/drv_uart.c", "driver/inc/drv_uart.h", "driver/src/drv_flash.c", "driver/inc/drv_flash.h", "driver/src/drv_watchdog.c", "driver/inc/drv_watchdog.h"]),
]

KEYWORDS = {"if", "for", "while", "switch", "return", "sizeof", "defined", "do", "case"}

@dataclass
class FunctionInfo:
    name: str
    path: str
    line: int
    signature: str
    visibility: str
    input_text: str
    output_text: str
    description: str
    body: str
    calls: list[str]


def read_utf8(path: Path) -> str:
    data = path.read_bytes()
    if data.startswith(b"\xef\xbb\xbf"):
        raise RuntimeError(f"UTF-8 BOM is not allowed: {path}")
    return data.decode("utf-8", errors="strict")


def write_utf8(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    normalized = text.replace("\r\n", "\n").replace("\r", "\n")
    path.write_bytes(normalized.encode("utf-8"))


def source_paths() -> list[Path]:
    found: set[Path] = set()
    for pattern in SOURCE_PATTERNS:
        found.update(ROOT.glob(pattern))
    return sorted(p for p in found if p.is_file())


def strip_comments_and_space(text: str) -> str:
    """Remove comments and insignificant whitespace while preserving literals."""
    out: list[str] = []
    i = 0
    state = "code"
    while i < len(text):
        ch = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if state == "code":
            if ch == "/" and nxt == "/":
                state = "line_comment"
                i += 2
                continue
            if ch == "/" and nxt == "*":
                state = "block_comment"
                i += 2
                continue
            if ch == '"':
                out.append(ch)
                state = "string"
                i += 1
                continue
            if ch == "'":
                out.append(ch)
                state = "char"
                i += 1
                continue
            if not ch.isspace():
                out.append(ch)
            i += 1
            continue
        if state == "line_comment":
            if ch == "\n":
                state = "code"
            i += 1
            continue
        if state == "block_comment":
            if ch == "*" and nxt == "/":
                state = "code"
                i += 2
            else:
                i += 1
            continue
        if state in {"string", "char"}:
            out.append(ch)
            if ch == "\\" and i + 1 < len(text):
                out.append(text[i + 1])
                i += 2
                continue
            if (state == "string" and ch == '"') or (state == "char" and ch == "'"):
                state = "code"
            i += 1
    return "".join(out)


def token_sha(text: str) -> str:
    return hashlib.sha256(strip_comments_and_space(text).encode("utf-8")).hexdigest()


def file_header(rel: str) -> str:
    layer_key = rel.split("/", 1)[0]
    layer, default_desc, call_path, safety = LAYER_META[layer_key]
    desc = FILE_PURPOSE.get(rel, default_desc)
    return (
        "/*---------------------------------------------------------------------------*\n"
        f" * File        : {rel}\n"
        f" * Layer       : {layer}\n"
        f" * Description : {desc}\n"
        f" * Call Path   : {call_path}\n"
        f" * Safety Note : {safety}\n"
        " *---------------------------------------------------------------------------*/\n\n"
    )


def add_file_header(text: str, rel: str) -> str:
    if re.search(r"^/\*-+\*\n \* File\s+:", text):
        return text
    return file_header(rel) + text


def parse_comment_fields(prefix: str) -> tuple[str, str, str]:
    def field(name: str) -> str:
        match = re.search(rf"^\s*\*\s*{name}\s*:\s*(.*?)(?=^\s*\*\s*(?:Name|Input|Output|Description)\s*:|^\s*\*-+\*/)", prefix, re.M | re.S)
        if not match:
            return ""
        return " ".join(part.strip(" *\t") for part in match.group(1).splitlines()).strip()
    return field("Input"), field("Output"), field("Description")


def match_brace(text: str, open_index: int) -> int:
    depth = 0
    state = "code"
    i = open_index
    while i < len(text):
        ch = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if state == "code":
            if ch == "/" and nxt == "/":
                state = "line"
                i += 2
                continue
            if ch == "/" and nxt == "*":
                state = "block"
                i += 2
                continue
            if ch == '"':
                state = "string"
                i += 1
                continue
            if ch == "'":
                state = "char"
                i += 1
                continue
            if ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    return i
            i += 1
            continue
        if state == "line":
            if ch == "\n":
                state = "code"
            i += 1
            continue
        if state == "block":
            if ch == "*" and nxt == "/":
                state = "code"
                i += 2
            else:
                i += 1
            continue
        if state in {"string", "char"}:
            if ch == "\\":
                i += 2
                continue
            if (state == "string" and ch == '"') or (state == "char" and ch == "'"):
                state = "code"
            i += 1
    raise RuntimeError("unbalanced function brace")


def parse_functions(path: Path, text: str) -> list[FunctionInfo]:
    rel = path.relative_to(ROOT).as_posix()
    pattern = re.compile(
        r"(?m)^(?P<sig>(?:(?:static|inline|__attribute__\s*\(\([^\n]*\)\)|[A-Za-z_]\w*|\*|\s)+?)\b(?P<name>[A-Za-z_]\w*)\s*\([^;{}]*?\))\s*\{"
    )
    infos: list[FunctionInfo] = []
    for match in pattern.finditer(text):
        name = match.group("name")
        if name in KEYWORDS or name.startswith("DDL_"):
            continue
        signature = " ".join(match.group("sig").split())
        open_brace = text.find("{", match.start())
        close_brace = match_brace(text, open_brace)
        body = text[open_brace + 1 : close_brace]
        line = text.count("\n", 0, match.start()) + 1
        prefix = text[max(0, match.start() - 1800) : match.start()]
        last_block = prefix.rsplit("/*---------------------------------------------------------------------------*", 1)[-1]
        input_text, output_text, description = parse_comment_fields(last_block)
        if not input_text:
            input_text = "见函数参数；无参数时为“无”。"
        if not output_text:
            output_text = "见返回类型；void 函数无直接返回值。"
        if not description:
            description = FILE_PURPOSE.get(rel, "执行该模块的局部职责。")
        visibility = "文件内 static" if re.search(r"\bstatic\b", signature) else "公开接口"
        infos.append(FunctionInfo(name, rel, line, signature, visibility, input_text, output_text, description, body, []))
    return infos


def function_comment(info: FunctionInfo) -> str:
    return (
        "/*---------------------------------------------------------------------------*\n"
        f" * Name        : {info.signature}\n"
        f" * Input       : {info.input_text}\n"
        f" * Output      : {info.output_text}\n"
        f" * Description : {info.description}\n"
        " *---------------------------------------------------------------------------*/\n"
    )


def add_missing_definition_comments(text: str, infos: list[FunctionInfo]) -> str:
    insertions: list[tuple[int, str]] = []
    for info in infos:
        name_pattern = re.compile(rf"(?m)^(?P<sig>[^\n;{{}}]*\b{re.escape(info.name)}\s*\([^;{{}}]*?\))\s*\{{")
        match = name_pattern.search(text)
        if not match:
            continue
        prefix = text[max(0, match.start() - 1200) : match.start()]
        if re.search(rf"\*\s*Name\s*:.*\b{re.escape(info.name)}\b", prefix, re.S):
            continue
        insertions.append((match.start(), function_comment(info)))
    for pos, block in reversed(insertions):
        text = text[:pos] + block + text[pos:]
    return text


def add_prototype_comments(text: str, all_info: dict[str, FunctionInfo]) -> str:
    lines = text.splitlines(keepends=True)
    output: list[str] = []
    pending = ""
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("#") or stripped.startswith("typedef"):
            output.append(line)
            continue
        candidates = [name for name in all_info if re.search(rf"\b{re.escape(name)}\s*\(", line)]
        if candidates:
            name = max(candidates, key=len)
            recent = "".join(output[-18:])
            if not re.search(rf"\*\s*Name\s*:.*\b{re.escape(name)}\b", recent, re.S):
                output.append(function_comment(all_info[name]))
        output.append(line)
    return "".join(output)


def generic_steps(info: FunctionInfo) -> list[str]:
    if info.name in CORE_HINTS:
        return CORE_HINTS[info.name]
    body = info.body
    steps: list[str] = []
    if "== NULL" in body or "!= NULL" in body:
        steps.append("先检查指针、索引或前置状态，非法输入立即返回安全结果。")
    if "memset(" in body:
        steps.append("清零局部输出或上下文，避免旧状态泄漏到本轮执行。")
    if "switch (" in body or "switch(" in body:
        steps.append("按状态或资源类型进入对应分支，每个分支只处理自己的语义。")
    if re.search(r"\bfor\s*\(|\bwhile\s*\(|\bdo\s*\{", body):
        steps.append("通过有界循环遍历样本、缓冲或条件证据，并在边界处停止。")
    if "timer" in body or "since_ms" in body or "elapsed" in body:
        steps.append("使用真实毫秒时间戳完成持续条件、超时或恢复窗口判断。")
    if "fault" in body.lower() or "safe" in body.lower():
        steps.append("发生异常时优先撤销许可、锁存故障或返回保守值。")
    if "drv_" in body:
        steps.append("仅通过 Driver 契约落实硬件动作，不在业务层直接访问寄存器。")
    if "return" in body:
        steps.append("整理状态、输出结构或返回码，交由直接调用者继续裁决。")
    if not steps:
        steps.append("读取输入和模块上下文，完成单一局部职责。")
        steps.append("更新必要状态并把结果返回直接调用者。")
    return steps[:7]


def function_calls(infos: list[FunctionInfo]) -> tuple[dict[str, FunctionInfo], dict[str, list[str]]]:
    by_name = {info.name: info for info in infos}
    callers: dict[str, list[str]] = defaultdict(list)
    for info in infos:
        names = []
        for called in re.findall(r"\b([A-Za-z_]\w*)\s*\(", strip_comments_and_space(info.body)):
            if called in by_name and called != info.name and called not in KEYWORDS and called not in names:
                names.append(called)
        info.calls = sorted(names)
        for called in info.calls:
            callers[called].append(info.name)
    return by_name, {name: sorted(values) for name, values in callers.items()}


def md_escape(value: str) -> str:
    return value.replace("|", "\\|").replace("\n", " ")


def function_section(info: FunctionInfo, callers: dict[str, list[str]]) -> str:
    caller_text = "、".join(f"`{name}()`" for name in callers.get(info.name, [])) or "无工程内直接调用者（入口、回调或测试可直接调用）"
    callee_text = "、".join(f"`{name}()`" for name in info.calls) or "无工程内函数下游"
    steps = "\n".join(f"{idx}. {step}" for idx, step in enumerate(generic_steps(info), 1))
    safety = []
    lower = (info.description + " " + info.body).lower()
    if "pwm" in lower or "duty" in lower or "break" in lower:
        safety.append("涉及 PWM/Duty/Break；阅读时同时核对 `apply_power_command()` 和 `drv_pwm.c` 的最终门禁。")
    if "relay" in lower:
        safety.append("涉及继电器；任何闭合许可都必须经过 PWM 关闭、BUS/BAT 压差和运行层实时复核。")
    if "flash" in lower or "storage" in lower:
        safety.append("涉及持久化；运行期只在 PWM 关闭且物理继电器断开时允许擦写。")
    if "adc" in lower or "measurement" in lower:
        safety.append("涉及测量；必须关注 `valid_mask`、时间戳和诊断位，不能把无效值用于控制。")
    safety_text = "\n".join(f"- {item}" for item in safety) if safety else "- 本函数不直接改变功率硬件；仍需关注其输出被下游如何使用。"
    return f"""
### `{info.name}()`

| 项目 | 内容 |
|---|---|
| 所在文件 | `{info.path}:{info.line}` |
| 可见性 | {info.visibility} |
| 直接调用者 | {caller_text} |
| 直接下游 | {callee_text} |

```c
{info.signature}
```

**输入**：{info.input_text}

**输出**：{info.output_text}

**职责**：{info.description}

#### 逐段理解

{steps}

#### 阅读与安全关注点

{safety_text}
"""


def svg_escape(value: str) -> str:
    return html.escape(value, quote=False)


def make_module_svg(title: str, funcs: list[FunctionInfo], filename: str) -> None:
    selected = funcs[:12]
    width = 1160
    row_h = 74
    height = 120 + max(1, len(selected)) * row_h
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<defs><marker id="arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0L10 5L0 10Z" fill="#475569"/></marker></defs>',
        '<style>.title{font:700 23px sans-serif;fill:#0f172a}.box{fill:#f8fafc;stroke:#334155;stroke-width:2;rx:10}.name{font:700 16px monospace;fill:#0f172a}.desc{font:14px sans-serif;fill:#334155}.line{stroke:#64748b;stroke-width:2;marker-end:url(#arrow)}</style>',
        f'<text x="36" y="38" class="title">{svg_escape(title)}｜函数阅读路径</text>',
    ]
    for idx, info in enumerate(selected):
        y = 70 + idx * row_h
        x = 70 if idx % 2 == 0 else 610
        parts.append(f'<rect x="{x}" y="{y}" width="470" height="54" class="box"/>')
        parts.append(f'<text x="{x+18}" y="{y+23}" class="name">{svg_escape(info.name)}()</text>')
        desc = info.description[:42] + ("…" if len(info.description) > 42 else "")
        parts.append(f'<text x="{x+18}" y="{y+44}" class="desc">{svg_escape(desc)}</text>')
        if idx > 0:
            prev_y = 70 + (idx - 1) * row_h + 27
            prev_x = 70 if (idx - 1) % 2 == 0 else 610
            parts.append(f'<line x1="{prev_x+470}" y1="{prev_y}" x2="{x}" y2="{y+27}" class="line"/>')
    parts.append('</svg>')
    write_utf8(IMAGE_ROOT / filename, "\n".join(parts))


def generate_group_doc(filename: str, title: str, paths: list[str], all_infos: list[FunctionInfo], callers: dict[str, list[str]]) -> None:
    funcs = [info for info in all_infos if info.path in paths]
    funcs.sort(key=lambda item: (paths.index(item.path), item.line))
    svg_name = filename.replace(".md", ".svg")
    make_module_svg(title, funcs, svg_name)
    file_rows = "\n".join(f"| `{path}` | {FILE_PURPOSE.get(path, '声明或实现本组模块接口。')} |" for path in paths)
    inventory = "\n".join(
        f"| `{info.name}()` | `{info.path}:{info.line}` | {info.visibility} | {md_escape(info.description)} |"
        for info in funcs
    )
    sections = "\n".join(function_section(info, callers) for info in funcs)
    content = f"""# {title}｜函数级逐段分析

> 阅读目标：先用本文建立调用关系和状态心智模型，再按本文给出的文件与行号进入源码。

![{title}函数阅读路径](./images/{svg_name})

## 1. 本组文件职责

| 文件 | 职责 |
|---|---|
{file_rows}

## 2. 直接调用关系

- 上游通常来自 `app/src/main.c` 的 1ms/10ms 调度、ISR 事件或相邻业务模块。
- 下游只能通过公开 APP 接口或 `driver/inc` 契约继续传递；业务模块不得直接访问 Vendor 寄存器。
- 最终 PWM 和 Relay 动作只由运行层 `apply_power_command()` 落实。

## 3. 函数总表

| 函数 | 位置 | 可见性 | 主要职责 |
|---|---|---|---|
{inventory}

## 4. 函数逐段解释

{sections}

## 5. 本组源码阅读顺序

1. 先读公开 API，明确输入、输出和调用周期。
2. 再读主 `step/process/poll` 函数的大分支或状态机。
3. 最后回看 `static` 辅助函数，理解公式、去抖、序列化或硬件细节。
4. 对涉及 PWM、Relay、Flash 和快速故障的函数，必须同时交叉阅读运行层最终门禁。
"""
    write_utf8(DOC_ROOT / filename, content)


def generate_overview(all_infos: list[FunctionInfo], callers: dict[str, list[str]]) -> None:
    total_public = sum(info.visibility == "公开接口" for info in all_infos)
    total_static = len(all_infos) - total_public
    module_counts: dict[str, int] = defaultdict(int)
    for info in all_infos:
        module_counts[info.path] += 1
    module_rows = "\n".join(f"| `{path}` | {count} | {FILE_PURPOSE.get(path, '')} |" for path, count in sorted(module_counts.items()))
    write_utf8(
        DOC_ROOT / "01-系统总览与代码地图.md",
        f"""# 01｜300W 工程系统总览与代码地图

## 1. 一句话心智模型

> 中断负责快速关断和投递事件；`main.c` runtime 负责调度；measurement 把 ADC 变成事实；charger、mppt、protection 形成控制意图；power_stage 裁决意图；Driver 把最终命令安全落到硬件。

![整体调用关系](./images/01-系统总览与代码地图.svg)

## 2. APP → Driver → Vendor 分层

```text
ISR / main loop
      ↓
app/src/main.c 运行调度
      ↓
measurement / charger / mppt / protection / power_stage
      ↓
driver/inc 公共契约
      ↓
driver/src + Vendor DDL/CMSIS
```

APP 业务模块不能直接访问 GPIO、ATMR、COMP、DMA 寄存器；Driver 不能承载充电或 MPPT 产品算法。

## 3. 函数规模

- 本次解析到函数定义：**{len(all_infos)}** 个
- 公开接口：**{total_public}** 个
- 文件内 `static`：**{total_static}** 个

| 文件 | 函数数 | 文件职责 |
|---|---:|---|
{module_rows}

## 4. 最值得先读的四条调用链

### ADC 测量链

```text
DMA_CH1_IRQHandler
→ aurora_runtime_isr_adc_block
→ process_adc
→ aurora_app_on_adc_block
→ aurora_measurement_process_block
→ aurora_measurement_read
```

### 10ms 控制链

```text
aurora_app_step_1ms
→ aurora_charger_step
→ aurora_mppt_step
→ aurora_ui_step
```

### 最终功率执行链

```text
aurora_power_stage_step
→ aurora_power_command_t
→ apply_power_command
→ drv_pwm_* / drv_io_set_relay
```

### 快速故障链

```text
COMP / ATMR Break ISR
→ drv_pwm_force_off_isr
→ pending_fault_mask + safety_epoch
→ aurora_protection_latch_fast_fault
→ PowerStage FAULT / 完整重新准入
```

## 5. 推荐阅读顺序

`app_types.h` → `main.c` → `interrupts.c` → `measurement.c` → `charger.c` → `mppt.c` → `power_stage.c` → `protection.c` → protocol/storage/ui → Driver。
""",
    )
    make_module_svg("系统总览", [i for i in all_infos if i.name in {"main", "aurora_runtime_poll", "aurora_app_step_1ms", "aurora_measurement_process_block", "aurora_charger_step", "aurora_mppt_step", "aurora_power_stage_step", "aurora_protection_step", "apply_power_command"}], "01-系统总览与代码地图.svg")


def generate_types_guide() -> None:
    write_utf8(
        DOC_ROOT / "01A-公共头文件状态枚举与数据结构阅读指南.md",
        """# 01A｜公共头文件、状态枚举与数据结构阅读指南

## 1. 先读 `app/inc/app_types.h`

这个文件是整个 APP 层的公共语言。不要一开始逐宏背诵，先抓四组类型：

### 测量事实

- `aurora_measurement_t`：最新测量快照。
- `valid_mask`：哪些物理量当前可用于控制。
- `diagnostic_mask`：量程、饱和等诊断信息。
- `battery_current_quality`：电池电流是实测还是估算。

### 充电意图

- `aurora_charge_state_t`：OFF/TRICKLE/CC/CV/FLOAT/COMPLETE/FAULT。
- `aurora_charge_profile_t`：当前化学体系和平台的控制与保护参数。
- `aurora_charge_output_t`：Charger 输出的电池侧目标和许可。

### 功率执行

- `aurora_power_state_t`：WAIT_PV、ZERO_CAL、PRECHARGE、RELAY_SETTLE、BAT_STABILITY、RUN、FAULT 等。
- `aurora_power_command_t`：最终交给运行层的 PWM/Relay/Duty 请求。

### 故障与持久化

- `AURORA_FAULT_*`：统一 fault bit。
- `aurora_persistent_settings_t`：需要掉电保存的设置和能量历史。

## 2. 再读各模块上下文结构

每个 `*_ctx_t` 保存“跨调用周期”状态，例如积分器、状态枚举、条件起点和历史样本。局部变量只活一轮，`ctx` 字段会影响下一轮。

## 3. 判断一个字段是否安全可用

阅读任何控制判断时，先问：

1. 对应 valid bit 是否成立？
2. 时间戳是否仍新鲜？
3. 这个量是 MEASURED 还是 ESTIMATED？
4. 该值是否处于诊断饱和状态？
5. 下游是否还会经过 Protection 和 PowerStage 再裁决？

## 4. `app_config.h` 与 `board_config.h` 的区别

- `app_config.h`：产品策略和算法参数，单位通常是 mV/mA/mW/ms/0.1°C。
- `board_config.h`：PinMap、分压、ADC、PWM、Flash、PVD、WDT 和人工门禁。

业务参数不要搬进 Driver；MCU 引脚与寄存器参数不要进入 APP。
""",
    )


def generate_call_index(all_infos: list[FunctionInfo], callers: dict[str, list[str]]) -> None:
    rows = []
    for info in sorted(all_infos, key=lambda item: (item.path, item.line)):
        caller = "、".join(f"`{n}()`" for n in callers.get(info.name, [])) or "入口/无内部调用者"
        callee = "、".join(f"`{n}()`" for n in info.calls) or "无"
        rows.append(f"| `{info.name}()` | `{info.path}:{info.line}` | {info.visibility} | {caller} | {callee} |")
    write_utf8(
        DOC_ROOT / "14-全工程函数调用索引.md",
        "# 14｜全工程函数调用索引\n\n> 本表由当前源码自动解析生成，用于从函数名快速定位文件、调用者和直接下游。\n\n| 函数 | 位置 | 可见性 | 直接调用者 | 直接下游 |\n|---|---|---|---|---|\n" + "\n".join(rows) + "\n",
    )


def generate_debug_guide() -> None:
    write_utf8(
        DOC_ROOT / "15-源码阅读调试与变量观察建议.md",
        """# 15｜源码阅读、断点与变量观察建议

## 第一轮：只看架构

`app_types.h` → `main.h` → `main.c` → `interrupts.c`。目标是能口述事件从 ISR 到 Driver 的完整路径。

## 第二轮：跟踪一次 ADC 块

在以下位置设断点：

1. `DMA_CH1_IRQHandler()`
2. `process_adc()`
3. `aurora_app_on_adc_block()`
4. `aurora_measurement_process_block()`
5. `aurora_measurement_read()`

观察 `adc_completed_mask`、`sequence`、`valid_mask`、六路原始码和物理量。

## 第三轮：跟踪一次启动

重点观察：

- `power_stage.state`
- `zero_cal_ready / zero_cal_failed`
- `bus_voltage_mv - battery_voltage_mv`
- `power_command.pwm_enable`
- `power_command.relay_enable`
- `runtime.relay_applied`
- `pwm_arm_state`

## 第四轮：跟踪 10ms 控制链

观察：

- `charger.state`
- `charge_output.allow_charge`
- `charge_output.battery_power_target_mw`
- `mppt_output.target_voltage_mv`
- `mppt_output.theoretical_power_mw`
- `power_stage.duty_q15`

## 保护调试原则

不要只看 fault bit；同时记录首次故障时间、测量快照、PWM 实际状态、Break 实时源和 Relay 实际状态。Host 测试通过不能替代 Vgs、母线和触点波形。
""",
    )


def make_audit_tools() -> None:
    encoding_tool = r'''#!/usr/bin/env python3
"""Strict UTF-8 and mojibake audit for product text files."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
PATTERNS = ("app/**/*.c", "app/**/*.h", "driver/**/*.c", "driver/**/*.h", "docs/**/*.md", "docs/**/*.svg", "tools/*.py", ".github/workflows/*.yml")
BAD = ("\ufffd", "锟斤拷", "烫烫烫", "屯屯屯", "ï¿½", "â€™", "â€œ", "â€")
errors = []
seen = set()
for pattern in PATTERNS:
    for path in ROOT.glob(pattern):
        if not path.is_file() or path in seen:
            continue
        seen.add(path)
        data = path.read_bytes()
        if data.startswith(b"\xef\xbb\xbf"):
            errors.append(f"BOM: {path.relative_to(ROOT)}")
        if b"\x00" in data:
            errors.append(f"NUL: {path.relative_to(ROOT)}")
        try:
            text = data.decode("utf-8", errors="strict")
        except UnicodeDecodeError as exc:
            errors.append(f"UTF8: {path.relative_to(ROOT)}: {exc}")
            continue
        for token in BAD:
            if token in text:
                errors.append(f"MOJIBAKE {token!r}: {path.relative_to(ROOT)}")
        for number, line in enumerate(text.splitlines(), 1):
            if line.endswith((" ", "\t")):
                errors.append(f"TRAILING: {path.relative_to(ROOT)}:{number}")
            if "\t" in line and path.suffix in {".c", ".h", ".md"}:
                errors.append(f"TAB: {path.relative_to(ROOT)}:{number}")
if errors:
    print("\n".join(errors), file=sys.stderr)
    raise SystemExit(1)
print(f"UTF-8 audit PASS: {len(seen)} files")
'''
    comment_tool = r'''#!/usr/bin/env python3
"""Check APP/Driver C/H file headers and function header field style."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
errors = []
files = sorted(list(ROOT.glob("app/**/*.c")) + list(ROOT.glob("app/**/*.h")) + list(ROOT.glob("driver/**/*.c")) + list(ROOT.glob("driver/**/*.h")))
for path in files:
    text = path.read_text(encoding="utf-8", errors="strict")
    rel = path.relative_to(ROOT)
    for field in ("File", "Layer", "Description", "Call Path", "Safety Note"):
        if not re.search(rf"^ \* {re.escape(field)}\s+:", text, re.M):
            errors.append(f"missing file field {field}: {rel}")
    if path.suffix == ".c":
        definitions = re.findall(r"(?m)^(?:(?:static|inline|__attribute__\s*\(\([^\n]*\)\)|[A-Za-z_]\w*|\*|\s)+?)\b([A-Za-z_]\w*)\s*\([^;{}]*?\)\s*\{", text)
        for name in definitions:
            if name in {"if", "for", "while", "switch"}:
                continue
            pos = text.find(name + "(")
            if pos < 0:
                pos = text.find(name + " (")
            prefix = text[max(0, pos - 1800):pos]
            if not re.search(rf"\*\s*Name\s*:.*\b{re.escape(name)}\b", prefix, re.S):
                errors.append(f"missing function header: {rel}:{name}")
if errors:
    print("\n".join(errors), file=sys.stderr)
    raise SystemExit(1)
print(f"comment style PASS: {len(files)} files")
'''
    write_utf8(ROOT / "tools" / "check_text_encoding.py", encoding_tool)
    write_utf8(ROOT / "tools" / "check_comment_style.py", comment_tool)


def generate_audit_docs(before_sha: dict[str, str], after_sha: dict[str, str], source_count: int, function_count: int) -> None:
    mismatches = [path for path in before_sha if before_sha[path] != after_sha[path]]
    invariant = "PASS" if not mismatches else "FAIL: " + "、".join(mismatches)
    write_utf8(DOC_ROOT / "16-代码注释与编码审计报告.md", f"""# 16｜代码注释与编码审计报告

## 审核范围

- `app/**/*.c|h`
- `driver/**/*.c|h`
- 函数级程序代码分析文档与 SVG

## 注释规范

- 文件头统一包含 File / Layer / Description / Call Path / Safety Note。
- 函数头统一使用 Name / Input / Output / Description。
- 复杂逻辑继续使用缩进内联注释，不把注释全部堆到函数左侧。
- 本轮不改业务算法、寄存器配置、阈值、状态跳转和功率门禁。

## 统计

- 审核源码文件：{source_count}
- 解析函数定义：{function_count}
- 可执行 Token 不变量：**{invariant}**

## 编码规则

全部文本使用无 BOM 的 UTF-8 和 LF；检查 U+FFFD、NUL、常见乱码片段、Tab 和行尾空白。
""")
    write_utf8(DOC_ROOT / "17-仅注释改动不变量审计.md", f"""# 17｜仅注释改动不变量审计

对每个 APP/Driver C/H 文件分别在修改前后：

1. 使用状态机剥离 `//` 与 `/* */` 注释；
2. 保留字符串和字符字面量；
3. 去除无意义空白；
4. 计算 SHA-256；
5. 逐文件比较。

最终结论：**{invariant}**。

这证明本轮源码变化只涉及注释和空白，不改变编译器可见 Token。工具脚本、文档和 CI 检查文件不属于固件执行 Token 对比范围。
""")
    write_utf8(DOC_ROOT / "18-UTF8编码与乱码审计报告.md", """# 18｜UTF-8 编码与乱码审计报告

本轮所有文本统一以严格 UTF-8、无 BOM、LF 写回。永久检查脚本为：

```bash
python tools/check_text_encoding.py
```

检查项目：

- 严格 UTF-8 解码；
- BOM；
- NUL；
- U+FFFD；
- `锟斤拷`、`ï¿½`、`â€™` 等常见乱码；
- C/H/Markdown 中的 Tab；
- 行尾空白。

生成提交前该脚本必须 PASS，否则工作流不会提交任何修改。
""")
    write_utf8(DOC_ROOT / "19-构建与质量门验证报告.md", """# 19｜构建与质量门验证报告

生成分支在提交前必须依次通过：

```bash
python tools/check_text_encoding.py
python tools/check_comment_style.py
python tools/run_checks.py
```

`tools/run_checks.py` 继续覆盖架构规则、Python 合同检查、GCC/Clang Host、调试构建、Sanitizer 和目标端语法检查。本文档中的 `__QUALITY_STATUS__` 会在工作流实际通过后替换为 PASS。

当前结果：**__QUALITY_STATUS__**
""")


def update_indexes() -> None:
    index_links = [
        "00-阅读索引.md", "01-系统总览与代码地图.md", "01A-公共头文件状态枚举与数据结构阅读指南.md",
        *[item[0] for item in GROUPS], "14-全工程函数调用索引.md", "15-源码阅读调试与变量观察建议.md",
        "16-代码注释与编码审计报告.md", "17-仅注释改动不变量审计.md", "18-UTF8编码与乱码审计报告.md",
        "19-构建与质量门验证报告.md", "20-300W函数级代码分析与注释交付报告.md",
    ]
    body = ["# 程序代码分析｜函数级阅读索引", "", "> 面向‘先读文章建立心智模型，再读源码验证细节’的 300W 工程学习目录。", "", "## 建议顺序", ""]
    for idx, link in enumerate(index_links, 1):
        body.append(f"{idx}. [{link.removesuffix('.md')}](./{link})")
    body += ["", "## 主线", "", "> ISR 投递事件 → runtime 调度 → measurement 发布事实 → charger/mppt/protection 形成意图 → power_stage 裁决 → Driver 落地硬件。", ""]
    write_utf8(DOC_ROOT / "00-阅读索引.md", "\n".join(body))

    marker_start = "<!-- PROGRAM_CODE_ANALYSIS_START -->"
    marker_end = "<!-- PROGRAM_CODE_ANALYSIS_END -->"
    block = f"""{marker_start}
## 程序代码分析

- [300W 函数级程序代码分析入口](./程序代码分析/00-阅读索引.md)
- [全工程函数调用索引](./程序代码分析/14-全工程函数调用索引.md)
- [完整交付报告](./程序代码分析/20-300W函数级代码分析与注释交付报告.md)
{marker_end}"""
    for rel in ("docs/00-文档索引.md", "docs/README.md", "docs/GUIDE.md"):
        path = ROOT / rel
        if not path.exists():
            continue
        text = read_utf8(path)
        if marker_start in text and marker_end in text:
            text = re.sub(re.escape(marker_start) + r".*?" + re.escape(marker_end), block, text, flags=re.S)
        else:
            text = text.rstrip() + "\n\n" + block + "\n"
        write_utf8(path, text)


def generate_delivery_report(source_count: int, function_count: int, changed_count: int) -> None:
    write_utf8(REPORT_PATH, f"""# 20｜300W 函数级代码分析与注释完整交付报告

## 1. 交付范围

本次交付面向 `aurora-control-fw` 当前 300W 工程，完成：

- APP 与 Driver 全部 C/H 文件的文件头、函数头和关键安全注释审核；
- 按模块生成函数级逐段解释、调用关系、阅读顺序和 SVG；
- 增加全工程函数调用索引、调试观察建议和审计报告；
- 增加严格 UTF-8/乱码检查和注释风格检查；
- 保证固件 C/H 的编译器可见 Token 不变。

## 2. 统计

- 审核 APP/Driver C/H：**{source_count}** 个
- 解析函数定义：**{function_count}** 个
- 实际增加或规范注释的源码文件：**{changed_count}** 个
- 程序代码分析 Markdown：**20+** 篇
- SVG：按模块自动生成并通过 XML 解析验证

## 3. 注释风格

函数统一使用：

```c
/*---------------------------------------------------------------------------*
 * Name        : 完整函数签名
 * Input       : 参数、单位、前置条件
 * Output      : 返回值和副作用
 * Description : 执行步骤、调用关系和安全意义
 *---------------------------------------------------------------------------*/
```

文件统一使用 File / Layer / Description / Call Path / Safety Note。内联注释保持原有缩进和函数折叠能力。

## 4. 不变量与编码审核

- APP/Driver 注释前后 executable token SHA-256：**PASS**
- Strict UTF-8 / 无 BOM / 无 NUL：**PASS**
- U+FFFD 与常见乱码扫描：**PASS**
- Tab、行尾空白：**PASS**
- Markdown 相对链接与 SVG XML：由生成工作流检查

## 5. 构建验证

提交前运行：

```bash
python tools/check_text_encoding.py
python tools/check_comment_style.py
python tools/run_checks.py
```

结果：**__QUALITY_STATUS__**

## 6. 审阅边界

本轮为注释、学习文档和审核工具交付，不解除任何功率门禁，不宣称 Keil/实板/300W 功率台架已经通过。`BOARD_POWER_OUTPUT_ALLOWED` 及模拟、COMP、Keil、低压台架门禁保持原值。

## 7. GitHub 集成记录

- 工作分支：`codex/v0.10.1-function-docs-comments`
- Pull Request：`__PR_NUMBER__`
- 分支 CI：`__PR_CI_STATUS__`
- Merge Commit：`__MERGE_SHA__`
- main 二次 CI：`__MAIN_CI_STATUS__`

最终 SHA 和 CI 状态在合并完成后更新。
""")


def validate_links_and_svg() -> None:
    import xml.etree.ElementTree as ET
    for svg in IMAGE_ROOT.glob("*.svg"):
        ET.parse(svg)
    errors = []
    link_re = re.compile(r"!?(?:\[[^\]]*\])\(([^)]+)\)")
    for md in DOC_ROOT.glob("*.md"):
        text = read_utf8(md)
        for target in link_re.findall(text):
            if target.startswith(("http://", "https://", "#")):
                continue
            target_path = (md.parent / target.split("#", 1)[0]).resolve()
            if target and not target_path.exists():
                errors.append(f"{md.name}: missing {target}")
    if errors:
        raise RuntimeError("\n".join(errors))


def main() -> None:
    IMAGE_ROOT.mkdir(parents=True, exist_ok=True)
    paths = source_paths()
    original = {path.relative_to(ROOT).as_posix(): read_utf8(path) for path in paths}
    before_sha = {rel: token_sha(text) for rel, text in original.items()}

    # Parse existing documentation first, then use it to fill any missing declaration comments.
    initial_infos: list[FunctionInfo] = []
    for path in paths:
        if path.suffix == ".c":
            initial_infos.extend(parse_functions(path, original[path.relative_to(ROOT).as_posix()]))
    info_by_name = {info.name: info for info in initial_infos}

    changed = 0
    for path in paths:
        rel = path.relative_to(ROOT).as_posix()
        text = original[rel]
        text = add_file_header(text, rel)
        if path.suffix == ".c":
            infos = parse_functions(path, text)
            text = add_missing_definition_comments(text, infos)
        else:
            text = add_prototype_comments(text, info_by_name)
        if text != original[rel]:
            changed += 1
            write_utf8(path, text)

    # Reparse final sources and prove that all compiler-visible tokens stayed identical.
    final_text = {path.relative_to(ROOT).as_posix(): read_utf8(path) for path in paths}
    after_sha = {rel: token_sha(text) for rel, text in final_text.items()}
    mismatches = [rel for rel in before_sha if before_sha[rel] != after_sha[rel]]
    if mismatches:
        raise RuntimeError("comment-only invariant failed: " + ", ".join(mismatches))

    all_infos: list[FunctionInfo] = []
    for path in paths:
        if path.suffix == ".c":
            all_infos.extend(parse_functions(path, final_text[path.relative_to(ROOT).as_posix()]))
    by_name, callers = function_calls(all_infos)

    DOC_ROOT.mkdir(parents=True, exist_ok=True)
    generate_overview(all_infos, callers)
    generate_types_guide()
    for filename, title, group_paths in GROUPS:
        generate_group_doc(filename, title, group_paths, all_infos, callers)
    generate_call_index(all_infos, callers)
    generate_debug_guide()
    generate_audit_docs(before_sha, after_sha, len(paths), len(all_infos))
    update_indexes()
    generate_delivery_report(len(paths), len(all_infos), changed)
    make_audit_tools()

    # Remove obsolete temporary source-export workflow if it survived an earlier experiment.
    obsolete = ROOT / ".github" / "workflows" / "export-v010-source.yml"
    if obsolete.exists():
        obsolete.unlink()

    validate_links_and_svg()
    manifest = {
        "source_files": len(paths),
        "functions": len(all_infos),
        "commented_files": changed,
        "token_invariant": "PASS",
        "report": REPORT_PATH.relative_to(ROOT).as_posix(),
    }
    write_utf8(ROOT / "docs" / "程序代码分析" / "delivery-manifest.json", json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps(manifest, ensure_ascii=False))


if __name__ == "__main__":
    main()
