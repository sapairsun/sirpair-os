#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""复现：进入 home 后执行 lua &，前台须仍能 ls（不抢 stdin）。"""
import os
import pty
import subprocess
import sys
import time
import tty

_tools_dir = os.path.dirname(os.path.abspath(__file__))
if _tools_dir not in sys.path:
    sys.path.insert(0, _tools_dir)
from qemu_regress_common import QEMU_SMP, read_some, wait_for_shell_ready


def _run_once(root, qemu_cmd):
    master_fd, slave_fd = pty.openpty()
    tty.setraw(slave_fd)

    env = os.environ.copy()
    env["TERM"] = "dumb"

    proc = subprocess.Popen(
        qemu_cmd,
        stdin=slave_fd,
        stdout=slave_fd,
        stderr=subprocess.STDOUT,
        close_fds=True,
        cwd=root,
        env=env,
    )
    os.close(slave_fd)

    buf = [b""]

    try:
        if not wait_for_shell_ready(master_fd, buf, 90.0):
            print("sh-background-regress: 未等到 shell 就绪", file=sys.stderr)
            return 1

        # 减轻 QEMU 下 EHCI+U 盘映像竞态：过早 cd 易触发控制器复位与后续内核异常
        time.sleep(3.0)
        os.write(master_fd, b"cd home\n")
        read_some(master_fd, buf, 4.0)
        os.write(master_fd, b"lua &\n")
        read_some(master_fd, buf, 2.5)
        os.write(master_fd, b"ls\n")
        read_some(master_fd, buf, 3.0)
        data = buf[0]

        if b"PANIC" in data:
            print("sh-background-regress: 检测到内核 PANIC，将重试", file=sys.stderr)
            return 1

        tail = data[-6000:] if len(data) > 6000 else data
        if b"exec ls failed" in tail:
            print("sh-background-regress: ls 在后台 lua 后执行失败", file=sys.stderr)
            sys.stderr.buffer.write(tail[-4000:])
            return 1
        # ls 列宽会截断长文件名（如 lua_regress.lua -> lua_regress.lu）
        if b"lua_regress" not in tail:
            print("sh-background-regress: ls 输出中未找到 lua_regress", file=sys.stderr)
            sys.stderr.buffer.write(tail[-4000:])
            return 1
        return 0
    finally:
        try:
            proc.terminate()
            proc.wait(timeout=5)
        except Exception:
            try:
                proc.kill()
            except Exception:
                pass
        try:
            os.close(master_fd)
        except Exception:
            pass


def main():
    cwd = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(cwd)
    os.chdir(root)

    img = "sirpair-kernel.img"
    if not os.path.isfile(img):
        print("sh-background-regress: 缺少 %s" % img, file=sys.stderr)
        return 1

    qemu_cmd = [
        "timeout",
        "220",
        "qemu-system-i386",
        "-cpu",
        "SandyBridge,-x2apic,-tsc-deadline,-avx,-syscall,-lm",
        "-smp",
        QEMU_SMP,
        "-m",
        "512",
        "-nographic",
        "-usb",
        "-device",
        "usb-ehci,id=ehci",
        "-device",
        "usb-storage,bus=ehci.0,drive=usbdisk,bootindex=1",
        "-drive",
        "if=none,id=usbdisk,file=%s,format=raw" % img,
        "-device",
        "e1000e,netdev=net0",
        "-netdev",
        "user,id=net0",
        "-rtc",
        "base=localtime,clock=host",
    ]

    for attempt in range(3):
        rc = _run_once(root, qemu_cmd)
        if rc == 0:
            print("sh-background-regress: 通过")
            return 0
        if attempt < 2:
            print(
                "sh-background-regress: 第 %d 次尝试失败，重试…" % (attempt + 1),
                file=sys.stderr,
            )
            time.sleep(1.0)
    return 1


if __name__ == "__main__":
    sys.exit(main())
