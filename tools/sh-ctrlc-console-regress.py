#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""管道 more 后按中断键，控制台须恢复回显，shell 能正常 echo。"""
import os
import pty
import subprocess
import sys
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
        print("sh-ctrlc-console-regress: 缺少 %s" % img, file=sys.stderr)
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
        if not wait_for_shell_ready(master_fd, buf, 90.0):
            print("sh-ctrlc-console-regress: 未等到 shell 就绪", file=sys.stderr)
            return 1

        os.write(master_fd, b"cd bin\n")
        read_some(master_fd, buf, 2.0)
        os.write(master_fd, b"ls | more\n")
        read_some(master_fd, buf, 3.0)
        # 一次中断：应结束 more 并恢复控制台（内核重置 + 管道右进程为前台）
        os.write(master_fd, b"\x03")
        read_some(master_fd, buf, 3.0)
        os.write(master_fd, b"echo SH_CTRLC_TTY_OK\n")
        read_some(master_fd, buf, 3.0)
        data = buf[0]

        if b"SH_CTRLC_TTY_OK" not in data:
            print("sh-ctrlc-console-regress: 未见 echo 输出（回显可能仍关闭）", file=sys.stderr)
            sys.stderr.buffer.write(data[-6000:])
            return 1
        if b"more: read error" in data[-2000:]:
            print("sh-ctrlc-console-regress: more 异常报错", file=sys.stderr)
            return 1
        return 0
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()


if __name__ == "__main__":
    sys.exit(main())
