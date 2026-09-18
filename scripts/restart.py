#!/usr/bin/env python3
"""Stop / start / restart the CppAI Docker Compose stack."""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
COMPOSE_CONTAINERS = ("cppai-http", "cppai-mysql", "cppai-rabbitmq")
LEGACY_CONTAINERS = ("ai-httpserver-2",)


def docker_prefix() -> list[str]:
    docker = shutil.which("docker")
    if docker is None:
        raise SystemExit("未找到 docker 命令")
    probe = subprocess.run(
        [docker, "info"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    if probe.returncode == 0:
        return [docker]
    sudo = shutil.which("sudo")
    if sudo is None:
        raise SystemExit("无法访问 Docker，且系统没有 sudo")
    return [sudo, docker]


def run(cmd: list[str], check: bool = True) -> int:
    print("+", " ".join(cmd))
    completed = subprocess.run(cmd, cwd=ROOT, check=False)
    if check and completed.returncode != 0:
        raise SystemExit(completed.returncode)
    return completed.returncode


def stop_stack(docker: list[str]) -> None:
    print("==> 停止已有容器")
    run(docker + ["compose", "down", "--remove-orphans"], check=False)
    leftover = list(LEGACY_CONTAINERS + COMPOSE_CONTAINERS)
    run(docker + ["rm", "-f", *leftover], check=False)


def compose_up(docker: list[str], env: dict[str, str], pull: str) -> int:
    cmd = docker + ["compose", "up", "--build", "--pull", pull, "-d"]
    print("+", " ".join(cmd))
    completed = subprocess.run(cmd, cwd=ROOT, env=env, check=False)
    return completed.returncode


def start_stack(docker: list[str]) -> None:
    print("==> 重新构建并启动")
    binary = ROOT / "build" / "Release" / "http_server"
    mtime = str(int(binary.stat().st_mtime)) if binary.exists() else "0"
    env = os.environ.copy()
    env["BINARY_MTIME"] = mtime
    # Bake/BuildKit 会向镜像站查 ubuntu:26.04 元数据；docker.1ms.run 超时则构建失败。
    # 关闭 Bake 并用旧版 builder，直接使用本机已有镜像。
    env["COMPOSE_BAKE"] = "false"
    env["DOCKER_BUILDKIT"] = "0"
    print("+ BINARY_MTIME=" + mtime)
    print("+ 优先使用本地镜像（不访问 Docker Hub）")
    code = compose_up(docker, env, "never")
    if code != 0:
        print("==> 本地镜像不足，改为按需在线拉取")
        env["DOCKER_BUILDKIT"] = "1"
        env.pop("COMPOSE_BAKE", None)
        code = compose_up(docker, env, "missing")
    if code != 0:
        raise SystemExit(code)
    print("==> 当前状态")
    run(docker + ["compose", "ps"], check=False)
    print("\n打开 http://127.0.0.1:8116")


def stack_running(docker: list[str]) -> bool:
    for name in COMPOSE_CONTAINERS:
        probe = subprocess.run(
            docker + ["inspect", "-f", "{{.State.Running}}", name],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            check=False,
        )
        if probe.returncode == 0 and probe.stdout.strip() == "true":
            return True
    return False


def ensure_stack(docker: list[str]) -> None:
    if stack_running(docker):
        print("==> 容器已在运行，重新启动")
        stop_stack(docker)
    else:
        print("==> 容器未运行，启动")
    start_stack(docker)


def main() -> int:
    action = sys.argv[1] if len(sys.argv) > 1 else "restart"
    aliases = {
        "down": "stop",
        "up": "start",
        "stop": "stop",
        "start": "start",
        "restart": "restart",
        "ensure": "ensure",
    }
    if action not in aliases:
        print("用法: python3 scripts/restart.py [stop|start|restart|ensure]", file=sys.stderr)
        return 2

    docker = docker_prefix()
    action = aliases[action]
    if action == "stop":
        stop_stack(docker)
    elif action == "start":
        start_stack(docker)
    elif action == "ensure":
        ensure_stack(docker)
    else:
        stop_stack(docker)
        start_stack(docker)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
