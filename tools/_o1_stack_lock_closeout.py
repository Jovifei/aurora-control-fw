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


# 1. Keil AC6 release baseline is Level 1 / -O1. O0 was measured unsafe for the 1 KiB stack.
replace_once(
    "project/AuroraControl.uvprojx",
    "<Optim>0</Optim>",
    "<Optim>1</Optim>",
)

# 2. Lock Keil optimization in permanent architecture gate.
checker = read("tools/check_architecture.py")
old = '''        for forbidden in ["service\\\\", "board\\\\board.c", "app\\\\src\\\\app.c", "project\\\\keil"]:\n            if forbidden in joined:\n                errors.append(f"Keil仍引用旧路径: {forbidden}")\n'''
new = '''        for forbidden in ["service\\\\", "board\\\\board.c", "app\\\\src\\\\app.c", "project\\\\keil"]:\n            if forbidden in joined:\n                errors.append(f"Keil仍引用旧路径: {forbidden}")\n        optim = project.find(".//Cads/Optim")\n        if (optim is None) or ((optim.text or "").strip() != "1"):\n            errors.append("Keil AC6发布优化必须固定Level 1 (-O1)；O0已通过目标栈审计判定不安全")\n'''
if checker.count(old) != 1:
    raise SystemExit("Keil checker insertion point not found exactly once")
write("tools/check_architecture.py", checker.replace(old, new, 1))

# 3. Replace stack checker with dual O1/O2 gate. O1 mirrors Keil release baseline; O2 catches optimizer-sensitive regressions.
stack_checker = r'''#!/usr/bin/env python3
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
'''
write("tools/check_target_stack.py", stack_checker)

# 4. Update permanent contract to require O1/O2 and Keil O1 lock.
contract_path = "tests/test_runtime_stack_contract.py"
contract = read(contract_path)
contract = contract.replace(
    '        self.assertIn("MAX_RUNTIME_POLL_FRAME = 256", stack_check)\n        self.assertIn("nested IRQ reserve", stack_check)\n',
    '        self.assertIn("MAX_RUNTIME_POLL_FRAME = 256", stack_check)\n'
    '        self.assertIn("OPTIMIZATION_LEVELS = (\\"-O1\\", \\"-O2\\")", stack_check)\n'
    '        self.assertIn("nested IRQ reserve", stack_check)\n'
    '        keil = (ROOT / "project/AuroraControl.uvprojx").read_text(encoding="utf-8")\n'
    '        self.assertIn("<Optim>1</Optim>", keil)\n'
    '        self.assertNotIn("<Optim>0</Optim>", keil)\n',
    1,
)
write(contract_path, contract)

# 5. Record rationale in project README and audit doc.
project_readme = read("project/README.md").rstrip()
project_readme += r'''

## AC6优化级别

产品目标固定使用 **Optimization Level 1 (`-O1`)**。G32F031启动文件栈为1024B；二次目标栈审计中，Clang Cortex-M0+ O0按当前中断嵌套预算投影为1776B，明确超过1KB；O1为824B、O2为828B（均已含128B额外保守裕量）。因此O0只允许临时诊断，不是可发布构建配置。永久质量门同时验证O1与O2，最终Keil MAP和实板栈水位仍为发布证据。
'''
write("project/README.md", project_readme + "\n")

doc_path = "docs/48-v0.10.3-Flash安全持久化与最近三次远端提交审核.md"
doc = read(doc_path).rstrip()
doc += r'''

## Cortex-M0+ 1KB栈与Keil发布优化级别

继续按目标机资源做静态栈审计后，发现原Keil工程 `<Optim>0</Optim>` 对AC6等价于Level 0 / `-O0`。在已经把Flash与协议大对象移出调用栈后，Clang `--target=arm-none-eabi -mcpu=cortex-m0plus -fstack-usage` 测得：

- O0：主首方链1184B，四级IRQ预算464B，加128B额外裕量后1776B / 1024B，**失败**；
- O1：主首方链408B，IRQ预算288B，加裕量后824B / 1024B，**通过**；
- O2：主首方链400B，IRQ预算300B，加裕量后828B / 1024B，**通过**。

因此产品Keil AC6构建固定为Optimization Level 1 (`-O1`)；O0不得作为发布配置。永久架构门锁定 `<Optim>1</Optim>`，目标栈门同时验证O1与O2。该静态门是fail-closed回归约束，不替代真实Keil AC6 MAP/callgraph与实板stack watermark。
'''
write(doc_path, doc + "\n")

print("Keil O1与O1/O2双栈门禁已应用")
