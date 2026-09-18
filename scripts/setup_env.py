#!/usr/bin/env python3
"""Bootstrap CppAIService: system tools, Docker images, Conan deps, CMake preset."""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VENV = ROOT / ".venv"
APP_ENV = ROOT / "docker" / "app.env"
APP_ENV_EXAMPLE = ROOT / "docker" / "app.env.example"

COMMAND_PACKAGES = (
    ("g++", "build-essential"),
    ("make", "build-essential"),
    ("cmake", "cmake"),
    ("python3", "python3"),
)
APT_DOCKER = (
    "docker.io",
    "docker-compose-v2",
    "docker-buildx",
)
DOCKER_IMAGES = (
    "mysql:8.0",
    "rabbitmq:3.13-management",
    "ubuntu:26.04",
)
LOCAL_RECIPES = (
    "conan/recipes/muduo",
    "conan/recipes/simpleamqpclient",
    "conan/recipes/mysql-connector-cpp-jdbc",
)


def log(message: str) -> None:
    print(message, flush=True)


def run(cmd: list[str], check: bool = True, env: dict[str, str] | None = None) -> int:
    log("+ " + " ".join(cmd))
    completed = subprocess.run(cmd, cwd=ROOT, check=False, env=env)
    if check and completed.returncode != 0:
        raise SystemExit(completed.returncode)
    return completed.returncode


def which(name: str) -> str | None:
    return shutil.which(name)


def sudo_prefix() -> list[str]:
    if hasattr(os, "geteuid") and os.geteuid() == 0:
        return []
    sudo = which("sudo")
    if sudo is None:
        raise SystemExit("安装系统依赖需要 root 权限，但找不到 sudo")
    return [sudo]


def apt_installed(package: str) -> bool:
    probe = subprocess.run(
        ["dpkg-query", "-W", "-f=${Status}", package],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
        check=False,
    )
    return probe.returncode == 0 and "install ok installed" in probe.stdout


def install_apt(packages: tuple[str, ...] | list[str]) -> None:
    missing = [pkg for pkg in packages if not apt_installed(pkg)]
    if not missing:
        return
    env = os.environ.copy()
    env["DEBIAN_FRONTEND"] = "noninteractive"
    sudo = sudo_prefix()
    log("==> 安装系统软件包: " + " ".join(missing))
    update = run(sudo + ["apt-get", "update"], env=env, check=False)
    install = 1
    if update == 0:
        install = run(sudo + ["apt-get", "install", "-y", *missing], env=env, check=False)
    if update != 0 or install != 0:
        raise SystemExit(
            "安装系统软件包需要 sudo 权限，请在终端执行: sudo apt-get install -y "
            + " ".join(missing)
        )


def ensure_build_tools() -> None:
    log("==> 检查编译工具")
    needed: list[str] = []
    seen: set[str] = set()
    for cmd, pkg in COMMAND_PACKAGES:
        if which(cmd) is None and pkg not in seen:
            needed.append(pkg)
            seen.add(pkg)
    if needed:
        install_apt(needed)
    for cmd, _pkg in COMMAND_PACKAGES:
        if which(cmd) is None:
            raise SystemExit(f"已尝试安装系统包，但仍未找到 {cmd}")


def docker_cli() -> list[str]:
    docker = which("docker")
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
    return sudo_prefix() + [docker]


def docker_daemon_ok(docker: list[str]) -> bool:
    probe = subprocess.run(
        docker + ["info"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    return probe.returncode == 0


def start_docker_daemon() -> None:
    sudo = sudo_prefix()
    if which("systemctl"):
        run(sudo + ["systemctl", "start", "docker"], check=False)
        run(sudo + ["systemctl", "enable", "docker"], check=False)
        return
    run(sudo + ["service", "docker", "start"], check=False)


def ensure_docker_group() -> None:
    if not hasattr(os, "geteuid") or os.geteuid() == 0:
        return
    user = os.environ.get("USER") or os.environ.get("LOGNAME")
    if not user:
        return
    probe = subprocess.run(
        ["id", "-nG", user],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
        check=False,
    )
    if probe.returncode == 0 and "docker" in probe.stdout.split():
        return
    log("==> 将当前用户加入 docker 组")
    run(sudo_prefix() + ["usermod", "-aG", "docker", user], check=False)
    log("提示: 重新登录后可以不使用 sudo 访问 Docker")


def ensure_docker() -> list[str]:
    log("==> 检查 Docker")
    if which("docker") is None:
        log("==> 未找到 Docker，开始安装")
        install_apt(APT_DOCKER)
        start_docker_daemon()
        ensure_docker_group()
    docker = docker_cli()
    if not docker_daemon_ok(docker):
        log("==> Docker 守护进程未运行，尝试启动")
        start_docker_daemon()
        docker = docker_cli()
    if not docker_daemon_ok(docker):
        raise SystemExit(
            "无法连接 Docker 守护进程。请先启动 Docker（WSL 可用: sudo service docker start）"
        )
    compose = subprocess.run(
        docker + ["compose", "version"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    if compose.returncode != 0:
        log("==> 缺少 docker compose 插件")
        install_apt(("docker-compose-v2", "docker-buildx"))
        compose = subprocess.run(
            docker + ["compose", "version"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
        if compose.returncode != 0:
            raise SystemExit("已安装 docker-compose-v2，但仍无法执行 docker compose")
    run(docker + ["--version"])
    run(docker + ["compose", "version"])
    return docker


def ensure_app_env() -> None:
    log("==> 检查 docker/app.env")
    if APP_ENV.exists():
        log(f"已存在 {APP_ENV}")
        return
    if not APP_ENV_EXAMPLE.exists():
        raise SystemExit(f"缺少模板文件: {APP_ENV_EXAMPLE}")
    shutil.copyfile(APP_ENV_EXAMPLE, APP_ENV)
    log(f"已从模板创建 {APP_ENV}，请按需填写 DASHSCOPE_API_KEY 等密钥")


def image_exists(docker: list[str], image: str) -> bool:
    probe = subprocess.run(
        docker + ["image", "inspect", image],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    return probe.returncode == 0


def ensure_docker_images(docker: list[str]) -> None:
    log("==> 检查 Docker 镜像")
    missing = [image for image in DOCKER_IMAGES if not image_exists(docker, image)]
    if not missing:
        log("所需镜像已在本地: " + ", ".join(DOCKER_IMAGES))
        return
    log("==> 下载缺失镜像: " + ", ".join(missing))
    failed: list[str] = []
    for image in missing:
        if run(docker + ["pull", image], check=False) != 0:
            failed.append(image)
    if failed:
        raise SystemExit("镜像拉取失败: " + ", ".join(failed) + "。请检查网络后重试")


def ensure_conan() -> str:
    log("==> 检查 Conan")
    existing = which("conan")
    if existing:
        log(f"已找到 conan: {existing}")
        return existing
    log("未找到 conan，使用项目 .venv 安装 Conan 2")
    if not apt_installed("python3-venv"):
        install_apt(("python3-venv",))
    pip = VENV / "bin" / "pip"
    conan_bin = VENV / "bin" / "conan"
    if not pip.exists():
        run([sys.executable, "-m", "venv", str(VENV)])
    run([str(pip), "install", "--upgrade", "pip"])
    run([str(pip), "install", "conan>=2,<3"])
    if not conan_bin.exists():
        raise SystemExit("Conan 安装失败")
    return str(conan_bin)


def ensure_conan_profile(conan: str) -> None:
    listed = subprocess.run(
        [conan, "profile", "list"],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    if listed.returncode == 0 and "default" in listed.stdout:
        log("==> Conan profile default 已存在")
        return
    log("==> 检测并创建 Conan profile")
    run([conan, "profile", "detect"])


def install_conan_deps(conan: str) -> None:
    log("==> 安装 Conan 依赖（缺失的包会自动下载/编译）")
    for recipe in LOCAL_RECIPES:
        run([conan, "create", str(ROOT / recipe), "--build=missing"])
    run(
        [
            conan,
            "install",
            ".",
            "--build=missing",
            "-s",
            "build_type=Release",
            "-s",
            "compiler.cppstd=17",
        ]
    )


def configure_cmake() -> None:
    cmake = which("cmake")
    if cmake is None:
        raise SystemExit("未找到 cmake，请先安装 cmake")
    toolchain = ROOT / "build" / "Release" / "generators" / "conan_toolchain.cmake"
    if not toolchain.exists():
        raise SystemExit(f"Conan 未生成 {toolchain}，请检查 conan install 是否成功")
    log("==> 配置 CMake preset conan-release")
    run([cmake, "--preset", "conan-release"])


def setup_docker() -> None:
    docker = ensure_docker()
    ensure_app_env()
    ensure_docker_images(docker)


def setup_cpp() -> None:
    ensure_build_tools()
    conan = ensure_conan()
    ensure_conan_profile(conan)
    install_conan_deps(conan)
    configure_cmake()


def run_section(name: str, fn) -> str | None:
    try:
        fn()
        return None
    except SystemExit as exc:
        if isinstance(exc.code, str) and exc.code:
            msg = exc.code
        else:
            msg = f"{name} 失败，退出码 {exc.code}"
        log("!! " + msg)
        return msg


def main() -> int:
    target = sys.argv[1] if len(sys.argv) > 1 else "all"
    if target not in ("all", "cpp", "docker"):
        print("用法: python3 scripts/setup_env.py [all|cpp|docker]", file=sys.stderr)
        return 2

    log(f"==> 配置开发环境 ({target})")
    errors: list[str] = []
    if target in ("all", "docker"):
        err = run_section("Docker", setup_docker)
        if err:
            errors.append(err)
    if target in ("all", "cpp"):
        err = run_section("C++", setup_cpp)
        if err:
            errors.append(err)

    if errors:
        log("\n环境配置未完成:")
        for item in errors:
            log("- " + item)
        return 1

    log("\n环境配置完成。")
    if target in ("all", "cpp"):
        log("下一步: 运行任务「编译并启动容器」")
    if target in ("all", "docker"):
        log("下一步: 编译完成后运行任务「启动项目容器」")
        log("若对话/语音要用云端 API，请编辑 docker/app.env")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
