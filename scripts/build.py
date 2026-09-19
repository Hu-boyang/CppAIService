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
USER_PRESETS = ROOT / "CMakeUserPresets.json"


def run(cmd: list[str]) -> None:
    print("+ " + " ".join(cmd), flush=True)
    completed = subprocess.run(cmd, cwd=ROOT, check=False)
    if completed.returncode != 0:
        raise SystemExit(completed.returncode)


def drop_stale_user_presets() -> None:
    # Committed CMakePresets.json already defines conan-release. A leftover
    # Conan CMakeUserPresets.json includes the same name and CMake 4 rejects it.
    if USER_PRESETS.exists():
        print("==> 移除过期的 CMakeUserPresets.json，避免 Duplicate preset: conan-release", flush=True)
        USER_PRESETS.unlink()


def main() -> int:
    drop_stale_user_presets()
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
