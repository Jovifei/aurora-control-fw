#!/usr/bin/env python3
"""运行架构、代码规范、Host编译/测试、Sanitizer和目标端语法门禁。"""
from pathlib import Path
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
skipped: list[str] = []


def run(args: list[str]) -> None:
    print("+", " ".join(args), flush=True)
    subprocess.run(args, cwd=root, check=True)


run([sys.executable, "tools/check_architecture.py"])
run([sys.executable, "tools/check_code_style.py"])
run([sys.executable, "-m", "unittest", "discover", "-s", "tests",
     "-p", "test_*.py", "-v"])

cmake_available = shutil.which("cmake") is not None
ctest_available = shutil.which("ctest") is not None
for compiler, build in [("gcc", "build-gcc"), ("clang", "build-clang")]:
    if shutil.which(compiler) is None or not cmake_available or not ctest_available:
        missing = [name for name, present in [
            (compiler, shutil.which(compiler) is not None),
            ("cmake", cmake_available),
            ("ctest", ctest_available),
        ] if not present]
        print(f"skip {compiler}: missing {', '.join(missing)}")
        skipped.append(f"{compiler} build/CTest")
        continue
    run(["cmake", "-S", ".", "-B", build, "-G", "Ninja",
         f"-DCMAKE_C_COMPILER={compiler}"])
    run(["cmake", "--build", build])
    run(["ctest", "--test-dir", build, "--output-on-failure"])

if (shutil.which("clang") is not None and cmake_available and
        ctest_available):
    run([
        "cmake", "-S", ".", "-B", "build-sanitize", "-G", "Ninja",
        "-DCMAKE_C_COMPILER=clang",
        "-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer",
    ])
    run(["cmake", "--build", "build-sanitize"])
    run(["ctest", "--test-dir", "build-sanitize", "--output-on-failure"])

run([sys.executable, "tools/check_target_syntax.py"])
if skipped:
    print("CHECKS INCOMPLETE: " + ", ".join(skipped))
    sys.exit(2)
print("ALL CHECKS PASSED")
