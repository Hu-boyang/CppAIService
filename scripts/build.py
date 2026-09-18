#!/usr/bin/env python3
"""Configure Conan/CMake if missing, then build http_server."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOLCHAIN = ROOT / "build" / "Release" / "generators" / "conan_toolchain.cmake"
CACHE = ROOT / "build" / "Release" / "CMakeCache.txt"
SETUP = ROOT / "scripts" / "setup_env.py"


def run(cmd: list[str]) -> None:
    print("+ " + " ".join(cmd), flush=True)
    completed = subprocess.run(cmd, cwd=ROOT, check=False)
    if completed.returncode != 0:
        raise SystemExit(completed.returncode)


def main() -> int:
    if not TOOLCHAIN.exists():
        print("==> 首次构建：Conan 工具链不存在，先配置开发环境", flush=True)
        run([sys.executable, str(SETUP), "cpp"])
    elif not CACHE.exists():
        print("==> 首次构建：配置 CMake preset conan-release", flush=True)
        run(["cmake", "--preset", "conan-release"])

    run(["cmake", "--build", "--preset", "conan-release", "--target", "http_server"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
