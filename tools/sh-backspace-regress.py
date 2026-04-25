#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
QEMU 串口回归：原始输入（902h）下退格须擦除帧缓冲字形，且空行连按退格不得擦掉提示符。
"""
import os
import pty
import re
import subprocess
import sys
import time
import tty

_tools_dir = os.path.dirname(os.path.abspath(__file__))
if _tools_dir not in sys.path:
    sys.path.insert(0, _tools_dir)
from qemu_regress_common import QEMU_SMP, read_some, wait_for_shell_ready


def main():
    cwd = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(cwd)
    os.chdir(root)

    img = "sirpair-kernel.img"
    if not os.path.isfile(img):
        print("sh-backspace-regress: 缺少 %s" % img, file=sys.stderr)
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
        if not wait_for_shell_ready(master_fd, buf, 180):
            print("sh-backspace-regress: 失败: 未等到 shell", file=sys.stderr)
            return 1

        time.sleep(0.8)

        # 路径1：输入后退格再执行命令，应无残留字符干扰
        os.write(master_fd, b"abcd")
        time.sleep(0.12)
        os.write(master_fd, b"\x7f\x7f\x7f\x7f")
        time.sleep(0.12)
        os.write(master_fd, b"echo BS_LINE_OK\n")
        time.sleep(0.4)
        read_some(master_fd, buf, 2.0)

        # 路径2：空行连按退格，再打印标记；提示符须仍在
        os.write(master_fd, b"\x7f" * 48)
        time.sleep(0.15)
        os.write(master_fd, b"echo BS_PROMPT_OK\n")
        time.sleep(0.5)
        read_some(master_fd, buf, 2.5)
        data = buf[0]

        if b"BS_LINE_OK" not in data:
            print("sh-backspace-regress: 失败: 未见到 BS_LINE_OK 输出", file=sys.stderr)
            return 1
        if b"BS_PROMPT_OK" not in data:
            print("sh-backspace-regress: 失败: 未见到 BS_PROMPT_OK 输出", file=sys.stderr)
            return 1
        if not re.search(rb"root@[^\n]*#", data):
            print("sh-backspace-regress: 失败: 未检测到提示符 root@…#", file=sys.stderr)
            return 1

        print("sh-backspace-regress: 通过")
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


if __name__ == "__main__":
    sys.exit(main())
