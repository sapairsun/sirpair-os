# SPDX-License-Identifier: MIT
"""QEMU 串口 PTY 回归共用：等待 shell 就绪（多标记 + 安全超时）。"""
import os
import select
import time

# 由 docker-build.sh 传入 SIRPAIR_QEMU_SMP；未设时默认 1（QEMU TCG+USB 多 vCPU 易挂死，真机四核不受影响）
QEMU_SMP = os.environ.get("SIRPAIR_QEMU_SMP", "1")

# init 打印与 shell 提示符（根目录）；二者任一出现即视为可输入命令
SHELL_MARKERS = (b"init: starting sh", b"root@/# ")


def read_some(fd, buf, timeout_sec):
    if timeout_sec <= 0:
        return True
    end = time.time() + timeout_sec
    while time.time() < end:
        remaining = end - time.time()
        if remaining <= 0:
            break
        r, _, _ = select.select([fd], [], [], min(0.2, remaining))
        if not r:
            continue
        try:
            chunk = os.read(fd, 16384)
        except OSError:
            time.sleep(0.05)
            continue
        if not chunk:
            time.sleep(0.03)
            continue
        buf[0] += chunk
    return True


def wait_for_shell_ready(fd, buf, overall_timeout_sec, markers=SHELL_MARKERS):
    end = time.time() + overall_timeout_sec
    while time.time() < end:
        for needle in markers:
            if needle in buf[0]:
                return True
        remaining = end - time.time()
        read_some(fd, buf, max(0.01, min(0.3, remaining)))
    for needle in markers:
        if needle in buf[0]:
            return True
    return False
