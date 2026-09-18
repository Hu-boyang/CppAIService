#!/usr/bin/env python3
"""Container entrypoint: wait for MySQL/RabbitMQ, then start http_server."""

from __future__ import annotations

import os
import socket
import sys
import time
from pathlib import Path

GAI_CONF = Path("/etc/gai.conf")
GAI_LINE = "precedence ::ffff:0:0/96  100\n"


def env(name: str, default: str) -> str:
    value = os.environ.get(name, default).strip()
    return value if value else default


def prefer_ipv4() -> None:
    try:
        text = GAI_CONF.read_text(encoding="utf-8") if GAI_CONF.exists() else ""
        if "precedence ::ffff:0:0/96" not in text:
            with GAI_CONF.open("a", encoding="utf-8") as fh:
                fh.write(GAI_LINE)
    except OSError:
        pass


def wait_tcp(host: str, port: str, name: str, attempts: int = 60) -> None:
    print(f"waiting for {name} at {host}:{port} ...", flush=True)
    last_error = ""
    for _ in range(attempts):
        try:
            with socket.create_connection((host, int(port)), timeout=2):
                print(f"{name} is ready", flush=True)
                return
        except OSError as exc:
            last_error = str(exc)
            time.sleep(2)
    print(f"timeout waiting for {name}: {last_error}", file=sys.stderr, flush=True)
    raise SystemExit(1)


def main() -> int:
    prefer_ipv4()
    wait_tcp(env("MYSQL_HOST", "mysql"), env("MYSQL_PORT", "3306"), "mysql")
    wait_tcp(env("RABBITMQ_HOST", "rabbitmq"), env("RABBITMQ_PORT", "5672"), "rabbitmq")

    os.chdir("/app/build")
    port = env("HTTP_PORT", "8116")
    os.execv("./http_server", ["./http_server", "-p", port])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
