#!/usr/bin/env python3
from pathlib import Path
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[1]

def run(args: list[str]) -> None:
    print("+", " ".join(args), flush=True)
    subprocess.run(args, cwd=root, check=True)

run([sys.executable, "tools/check_architecture.py"])
for compiler, build in [("gcc", "build-gcc"), ("clang", "build-clang")]:
    if shutil.which(compiler) is None:
        print(f"skip {compiler}: not installed")
        continue
    run(["cmake", "-S", ".", "-B", build, "-G", "Ninja", f"-DCMAKE_C_COMPILER={compiler}"])
    run(["cmake", "--build", build])
    run(["ctest", "--test-dir", build, "--output-on-failure"])
run([sys.executable, "tools/check_target_syntax.py"])
print("ALL CHECKS PASSED")
