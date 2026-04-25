#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
QEMU 串口回归：在未按回车前输入超过一行缓冲长度的字符，不得提前执行命令或出现 exec failed。
（修复 getcmd 在缓冲满时误结束的问题。）
"""
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


def main():
    cwd = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(cwd)
    os.chdir(root)

    img = "sirpair-kernel.img"
    if not os.path.isfile(img):
        print("sh-longline-regress: 缺少 %s" % img, file=sys.stderr)
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
            print("sh-longline-regress: 失败: 未等到 shell", file=sys.stderr)
            return 1

        time.sleep(0.8)
        os.write(master_fd, b"a" * 130)
        time.sleep(0.45)
        read_some(master_fd, buf, 1.5)
        if b"failed" in buf[0]:
            print("sh-longline-regress: 失败: 未按回车前出现 failed", file=sys.stderr)
            return 1
        os.write(master_fd, b"\x15")
        time.sleep(0.2)
        os.write(master_fd, b"echo LONGLINE_OK\n")
        time.sleep(0.45)
        read_some(master_fd, buf, 2.0)
        if b"LONGLINE_OK" not in buf[0]:
            print("sh-longline-regress: 失败: 未见到 LONGLINE_OK（^U 后应可正常执行）", file=sys.stderr)
            return 1

        print("sh-longline-regress: 通过")
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
