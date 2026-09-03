#!/usr/bin/env python3
"""以Clang Cortex-M0+ O1/O2估算首方静态调用链与当前NVIC嵌套预算。"""
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
OPTIMIZATION_LEVELS = ("-O1", "-O2")

sources = sorted((root / "app/src").glob("*.c")) + sorted((root / "driver/src").glob("*.c"))


def evaluate_level(optimization: str) -> list[str]:
    errors: list[str] = []
    with tempfile.TemporaryDirectory(prefix=f"aurora-arm-stack-{optimization[1:]}-") as temp_dir:
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
            optimization,
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
            return [f"{optimization}: missing functions: {missing}"]

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

        print(f"TARGET STACK {optimization}: runtime_poll frame={poll_frame} B")
        print(f"TARGET STACK {optimization}: main first-party chain={main_chain} B :: {' -> '.join(main_path)}")
        print(
            f"TARGET STACK {optimization}: nested IRQ reserve={irq_nested} B "
            f"(P3={irq_prio3}, P2={irq_prio2}, P1={irq_prio1}, P0={irq_prio0})"
        )
        print(
            f"TARGET STACK {optimization}: projected={projected} B = first-party {main_chain} + "
            f"IRQ {irq_nested} + margin {EXTRA_MARGIN_BYTES}; configured={TARGET_STACK_BYTES} B"
        )

        if poll_frame > MAX_RUNTIME_POLL_FRAME:
            errors.append(
                f"{optimization}: aurora_runtime_poll frame {poll_frame}B exceeds "
                f"{MAX_RUNTIME_POLL_FRAME}B; large main-loop locals likely returned"
            )
        if projected > TARGET_STACK_BYTES:
            errors.append(
                f"{optimization}: projected stack budget {projected}B exceeds configured "
                f"{TARGET_STACK_BYTES}B"
            )
    return errors


all_errors: list[str] = []
for level in OPTIMIZATION_LEVELS:
    all_errors.extend(evaluate_level(level))

if all_errors:
    print("TARGET STACK CHECK: FAIL")
    for error in all_errors:
        print(f"- {error}")
    sys.exit(1)

print("TARGET STACK CHECK: PASS (O1/O2)")
