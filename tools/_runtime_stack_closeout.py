#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    (ROOT / path).write_text(text, encoding="utf-8", newline="\n")


def replace_once(path: str, old: str, new: str) -> None:
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, got {count}: {old[:140]!r}")
    write(path, text.replace(old, new, 1))


def replace_region(path: str, start_marker: str, end_marker: str, replacement: str) -> None:
    text = read(path)
    start = text.index(start_marker)
    end = text.index(end_marker, start)
    write(path, text[:start] + replacement + text[end:])


# 1. 协议收发工作区移出1KB目标调用栈；主循环单线程独占，ISR绝不访问。
replace_once(
    "app/src/main.c",
    """/* Flash Journal单线程工作页；避免512B页缓冲占用目标仅1KB的启动栈。 */\nstatic uint8_t g_storage_page_workspace[AURORA_STORAGE_PAGE_SIZE];""",
    """/* Flash Journal单线程工作页；避免512B页缓冲占用目标仅1KB的启动栈。 */\nstatic uint8_t g_storage_page_workspace[AURORA_STORAGE_PAGE_SIZE];\n\n/* 主循环独占的协议收发工作区；ISR只写RX环形缓冲，不得访问这里。 */\ntypedef struct\n{\n    aurora_protocol_frame_t request;                 /* 已校验的当前请求帧。 */\n    aurora_protocol_frame_t tx_frame;                /* 应答或主动遥测发送帧。 */\n    uint8_t wire[AURORA_PROTOCOL_MAX_WIRE];          /* 线格式编码缓冲。 */\n} runtime_protocol_workspace_t;\n\n/* 单实例裸机主循环可安全复用，避免约420B协议临时对象压入1KB调用栈。 */\nstatic runtime_protocol_workspace_t g_protocol_workspace;""",
)

process_uart_start = """/*---------------------------------------------------------------------------*\n * Name        : static void process_uart(aurora_runtime_t *runtime,"""
process_uart_end = """/*---------------------------------------------------------------------------*\n * Name        : static aurora_storage_page_status_t storage_read_page("""
new_process_uart = r'''/*---------------------------------------------------------------------------*
 * Name        : static void process_uart(aurora_runtime_t *runtime,
 *               uint32_t now_ms)
 * Input       : runtime - 应用运行上下文；now_ms - 当前毫秒
 * Output      : 无
 * Description : 按预算消费RX环形缓冲、推进协议解析并发送应答；请求/发送/线缓冲复用主循环静态工作区，
 *               避免约420B协议对象占用目标1KB调用栈。
 *---------------------------------------------------------------------------*/
static void process_uart(aurora_runtime_t *runtime, uint32_t now_ms)
{
    bool has_response;
    size_t wire_length;
    uint32_t budget = AURORA_RUNTIME_UART_RX_BUDGET;

    while (budget > 0U)
    {
        uint8_t byte;
        aurora_irq_state_t irq = drv_irq_save();
        if (runtime->uart_tail == runtime->uart_head)
        {
            drv_irq_restore(irq);
            break;
        }
        byte = runtime->uart_rx[runtime->uart_tail];
        runtime->uart_tail = (uint16_t)((runtime->uart_tail + 1U) % sizeof(runtime->uart_rx));
        drv_irq_restore(irq);
        budget--;

        aurora_protocol_feed_byte(&runtime->app.protocol, byte, now_ms);
        if (aurora_protocol_take_frame(&runtime->app.protocol, &g_protocol_workspace.request))
        {
            aurora_app_on_protocol_frame(&runtime->app, &g_protocol_workspace.request,
                                         &g_protocol_workspace.tx_frame, &has_response, now_ms);
            if (has_response)
            {
                wire_length = aurora_protocol_encode(&g_protocol_workspace.tx_frame,
                                                     g_protocol_workspace.wire,
                                                     sizeof(g_protocol_workspace.wire));
                if (wire_length != 0U)
                {
                    (void)drv_uart_send(g_protocol_workspace.wire, wire_length);
                }
            }
        }
    }

    if (runtime->uart_tail != runtime->uart_head)
    {
        atomic_or_u32(&runtime->event_flags, RUNTIME_EVENT_UART_RX);
    }
}

'''
replace_region("app/src/main.c", process_uart_start, process_uart_end, new_process_uart)

old_telemetry = r'''    if ((now_ms - runtime->last_telemetry_ms) >= AURORA_TELEMETRY_PERIOD_MS)
    {
        aurora_protocol_frame_t telemetry;
        uint8_t wire[AURORA_PROTOCOL_MAX_WIRE];
        size_t wire_length;

        aurora_protocol_fill_telemetry_ex(
            &telemetry, runtime->app.telemetry_message_id++, &runtime->app.sample,
            runtime->app.charger.state, runtime->app.actual_power_transfer,
            aurora_protection_fault_mask(&runtime->app.protection), &runtime->app.storage.settings);
        wire_length = aurora_protocol_encode(&telemetry, wire, sizeof(wire));
        if (wire_length != 0U)
        {
            (void)drv_uart_send(wire, wire_length);
        }
        runtime->last_telemetry_ms = now_ms;
    }
'''
new_telemetry = r'''    if ((now_ms - runtime->last_telemetry_ms) >= AURORA_TELEMETRY_PERIOD_MS)
    {
        size_t wire_length;

        aurora_protocol_fill_telemetry_ex(
            &g_protocol_workspace.tx_frame, runtime->app.telemetry_message_id++, &runtime->app.sample,
            runtime->app.charger.state, runtime->app.actual_power_transfer,
            aurora_protection_fault_mask(&runtime->app.protection), &runtime->app.storage.settings);
        wire_length = aurora_protocol_encode(&g_protocol_workspace.tx_frame,
                                             g_protocol_workspace.wire,
                                             sizeof(g_protocol_workspace.wire));
        if (wire_length != 0U)
        {
            (void)drv_uart_send(g_protocol_workspace.wire, wire_length);
        }
        runtime->last_telemetry_ms = now_ms;
    }
'''
replace_once("app/src/main.c", old_telemetry, new_telemetry)

# 2. 永久Cortex-M0+首方静态栈预算门：不替代Keil MAP，但禁止明显回归重新逼近1KB。
stack_checker = r'''#!/usr/bin/env python3
"""以Clang Cortex-M0+ O2估算首方静态调用链与当前NVIC嵌套预算。"""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
clang = shutil.which("clang")
if clang is None:
    raise SystemExit("TARGET STACK CHECK: FAIL (clang not found)")

TARGET_STACK_BYTES = 1024
EXCEPTION_FRAME_BYTES = 36  # Cortex-M0+基础8字硬件帧32B，另保留4B对齐裕量。
EXTRA_MARGIN_BYTES = 128    # 给vendor/库调用、编译器差异和不可见小帧的额外保守余量。
MAX_RUNTIME_POLL_FRAME = 256

sources = sorted((root / "app/src").glob("*.c")) + sorted((root / "driver/src").glob("*.c"))

with tempfile.TemporaryDirectory(prefix="aurora-arm-stack-") as temp_dir:
    temp = Path(temp_dir)
    stub = temp / "stub"
    out = temp / "out"
    ir_dir = temp / "ir"
    stub.mkdir()
    out.mkdir()
    ir_dir.mkdir()
    (stub / "string.h").write_text(
        "#ifndef AURORA_STACK_STRING_H\n"
        "#define AURORA_STACK_STRING_H\n"
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
        "-O2",
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
        base = source.stem
        extra = ["-Wno-missing-prototypes"] if rel.as_posix() == "app/src/interrupts.c" else []
        subprocess.run(
            common + extra + ["-fstack-usage", "-c", str(rel), "-o", str(out / f"{base}.o")],
            cwd=root,
            check=True,
        )
        subprocess.run(
            common + extra + ["-S", "-emit-llvm", str(rel), "-o", str(ir_dir / f"{base}.ll")],
            cwd=root,
            check=True,
        )

    frames: dict[str, int] = {}
    for su in out.glob("*.su"):
        for raw in su.read_text(encoding="utf-8").splitlines():
            if not raw.strip():
                continue
            loc, size, _kind = raw.split("\t")[:3]
            function = loc.rsplit(":", 1)[-1]
            frames[function] = max(frames.get(function, 0), int(size))

    calls: dict[str, set[str]] = {name: set() for name in frames}
    define_re = re.compile(r"^define\b.*?@([A-Za-z_.$][A-Za-z0-9_.$]*)\(")
    call_re = re.compile(r"\b(?:call|invoke)\b.*?@([A-Za-z_.$][A-Za-z0-9_.$]*)\(")
    for ll in ir_dir.glob("*.ll"):
        current: str | None = None
        for line in ll.read_text(encoding="utf-8").splitlines():
            match = define_re.match(line)
            if match:
                current = match.group(1)
                calls.setdefault(current, set())
                continue
            if current is not None and line.strip() == "}":
                current = None
                continue
            if current is None:
                continue
            for target in call_re.findall(line):
                if target in frames:
                    calls[current].add(target)

    memo: dict[str, tuple[int, list[str]]] = {}
    visiting: set[str] = set()

    def deepest(name: str) -> tuple[int, list[str]]:
        if name in memo:
            return memo[name]
        if name in visiting:
            return frames.get(name, 0), [name, "<cycle>"]
        visiting.add(name)
        best_size = 0
        best_chain: list[str] = []
        for child in sorted(calls.get(name, set())):
            child_size, child_chain = deepest(child)
            if child_size > best_size:
                best_size = child_size
                best_chain = child_chain
        visiting.remove(name)
        result = frames.get(name, 0) + best_size, [name] + best_chain
        memo[name] = result
        return result

    required = [
        "main",
        "aurora_runtime_poll",
        "USART_IRQHandler",
        "SysTick_Handler",
        "ADC_IRQHandler",
        "DMA_CH1_IRQHandler",
        "COMP0_IRQHandler",
        "COMP1_2_3_IRQHandler",
        "ATMR_BRK_UP_TRG_COM_IRQHandler",
    ]
    missing = [name for name in required if name not in frames]
    if missing:
        raise SystemExit(f"TARGET STACK CHECK: FAIL (missing functions: {missing})")

    main_chain, main_path = deepest("main")
    poll_frame = frames["aurora_runtime_poll"]
    irq_prio3 = deepest("USART_IRQHandler")[0] + EXCEPTION_FRAME_BYTES
    irq_prio2 = max(deepest("SysTick_Handler")[0], deepest("ADC_IRQHandler")[0]) + EXCEPTION_FRAME_BYTES
    irq_prio1 = deepest("DMA_CH1_IRQHandler")[0] + EXCEPTION_FRAME_BYTES
    irq_prio0 = max(
        deepest("COMP0_IRQHandler")[0],
        deepest("COMP1_2_3_IRQHandler")[0],
        deepest("ATMR_BRK_UP_TRG_COM_IRQHandler")[0],
    ) + EXCEPTION_FRAME_BYTES
    irq_nested = irq_prio3 + irq_prio2 + irq_prio1 + irq_prio0
    projected = main_chain + irq_nested + EXTRA_MARGIN_BYTES

    print(f"TARGET STACK: runtime_poll frame={poll_frame} B")
    print(f"TARGET STACK: main first-party chain={main_chain} B :: {' -> '.join(main_path)}")
    print(
        "TARGET STACK: nested IRQ reserve="
        f"{irq_nested} B (P3={irq_prio3}, P2={irq_prio2}, P1={irq_prio1}, P0={irq_prio0})"
    )
    print(
        f"TARGET STACK: projected={projected} B = first-party {main_chain} + IRQ {irq_nested} + "
        f"margin {EXTRA_MARGIN_BYTES}; configured={TARGET_STACK_BYTES} B"
    )

    errors: list[str] = []
    if poll_frame > MAX_RUNTIME_POLL_FRAME:
        errors.append(
            f"aurora_runtime_poll frame {poll_frame}B exceeds {MAX_RUNTIME_POLL_FRAME}B; "
            "large main-loop locals likely returned"
        )
    if projected > TARGET_STACK_BYTES:
        errors.append(
            f"projected stack budget {projected}B exceeds configured {TARGET_STACK_BYTES}B"
        )

    if errors:
        print("TARGET STACK CHECK: FAIL")
        for error in errors:
            print(f"- {error}")
        sys.exit(1)

print("TARGET STACK CHECK: PASS")
'''
write("tools/check_target_stack.py", stack_checker)

replace_once(
    "tools/run_checks.py",
    """run([sys.executable, \"tools/check_target_syntax.py\"])\nprint(\"ALL CHECKS PASSED\")""",
    """run([sys.executable, \"tools/check_target_syntax.py\"])\nrun([sys.executable, \"tools/check_target_stack.py\"])\nprint(\"ALL CHECKS PASSED\")""",
)

# 3. 静态契约锁死：协议大缓冲不得重新回到process_uart/runtime_poll局部栈。
contract = r'''from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RuntimeStackContract(unittest.TestCase):
    def test_protocol_workspaces_are_file_static(self):
        main = (ROOT / "app/src/main.c").read_text(encoding="utf-8")
        self.assertIn("static runtime_protocol_workspace_t g_protocol_workspace;", main)
        self.assertIn("aurora_protocol_take_frame(&runtime->app.protocol, &g_protocol_workspace.request)", main)
        self.assertIn("&g_protocol_workspace.tx_frame", main)
        self.assertIn("g_protocol_workspace.wire", main)

    def test_large_protocol_buffers_are_not_local_to_runtime_path(self):
        main = (ROOT / "app/src/main.c").read_text(encoding="utf-8")
        uart_start = main.index("static void process_uart(")
        uart_end = main.index("static aurora_storage_page_status_t storage_read_page(", uart_start)
        uart = main[uart_start:uart_end]
        poll_start = main.index("void aurora_runtime_poll(")
        poll_end = main.index("void aurora_runtime_isr_tick(", poll_start)
        poll = main[poll_start:poll_end]
        for body in (uart, poll):
            self.assertIsNone(re.search(r"aurora_protocol_frame_t\\s+\\w+\\s*;", body))
            self.assertNotIn("uint8_t wire[AURORA_PROTOCOL_MAX_WIRE]", body)

    def test_target_stack_gate_is_permanent(self):
        run_checks = (ROOT / "tools/run_checks.py").read_text(encoding="utf-8")
        stack_check = (ROOT / "tools/check_target_stack.py").read_text(encoding="utf-8")
        self.assertIn('run([sys.executable, "tools/check_target_stack.py"])', run_checks)
        self.assertIn("TARGET_STACK_BYTES = 1024", stack_check)
        self.assertIn("EXTRA_MARGIN_BYTES = 128", stack_check)
        self.assertIn("MAX_RUNTIME_POLL_FRAME = 256", stack_check)
        self.assertIn("nested IRQ reserve", stack_check)


if __name__ == "__main__":
    unittest.main()
'''
write("tests/test_runtime_stack_contract.py", contract)

print("主循环1KB栈余量修复与永久门禁已应用")
