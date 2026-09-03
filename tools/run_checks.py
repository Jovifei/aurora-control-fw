#!/usr/bin/env python3
"""Fail-closed运行架构、编码、Host编译/测试、Sanitizer和目标端资源门禁。"""
from pathlib import Path
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[1]

def run(args: list[str]) -> None:
    print("+", " ".join(args), flush=True)
    subprocess.run(args, cwd=root, check=True)

def require(tool: str) -> None:
    if shutil.which(tool) is None:
        raise SystemExit(f"required tool missing: {tool}")

for tool in ("cmake", "ninja", "gcc", "clang"):
    require(tool)
run([sys.executable, "tools/check_architecture.py"])
run([sys.executable, "tools/check_code_style.py"])
run([sys.executable, "tools/check_text_encoding.py"])
run([sys.executable, "-m", "unittest", "discover", "-s", "tests", "-p", "test_*.py", "-v"])
for compiler, build in (("gcc", "build-gcc"), ("clang", "build-clang")):
    run(["cmake", "-S", ".", "-B", build, "-G", "Ninja", f"-DCMAKE_C_COMPILER={compiler}"])
    run(["cmake", "--build", build])
    run(["ctest", "--test-dir", build, "--output-on-failure"])
run(["cmake", "-S", ".", "-B", "build-sanitize", "-G", "Ninja", "-DCMAKE_C_COMPILER=clang",
     "-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer"])
run(["cmake", "--build", "build-sanitize"])
run(["ctest", "--test-dir", "build-sanitize", "--output-on-failure"])
run([sys.executable, "tools/check_target_syntax.py"])
run([sys.executable, "tools/check_target_stack.py"])
run([sys.executable, "tools/check_target_ram.py"])
print("ALL CHECKS PASSED")
