#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
QEMU 串口回归：验证 mv 重命名、移入目录、覆盖文件、多文件移入目录。
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
        print("mv-regress: 缺少 %s" % img, file=sys.stderr)
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
            print("mv-regress: 失败: 未等到 shell", file=sys.stderr)
            return 1
        # EHCI 复位后若立刻发串口命令，易与提示符或下一行粘连（echomkdir 等）
        time.sleep(12.0)

        def send(chunk):
            os.write(master_fd, chunk)
            time.sleep(0.4)

        send(b"mkdir mvt1\n")
        send(b"echo abc > mva\n")
        send(b"mv mva mvb\n")
        send(b"cat mvb\n")
        send(b"mv mvb mvt1\n")
        send(b"cat mvt1/mvb\n")
        send(b"echo MVX > mvc1\n")
        send(b"echo MVY > mvc2\n")
        send(b"mv mvc1 mvc2\n")
        send(b"cat mvc2\n")
        send(b"mkdir mvt2\n")
        send(b"echo a > nmv1\n")
        send(b"echo b > nmv2\n")
        send(b"mv nmv1 nmv2 mvt2\n")
        send(b"ls mvt2\n")
        send(b"mkdir mv_empty\n")
        send(b"mv mv_empty mvt2\n")
        send(b"echo done_mv\n")
        time.sleep(2.0)
        read_some(master_fd, buf, 45.0)
        data = buf[0]

        if not re.search(rb"abc", data):
            print("mv-regress: 失败: 重命名或 cat 后未出现 abc", file=sys.stderr)
            return 1
        if data.count(b"abc") < 2:
            print("mv-regress: 失败: 移入目录后 cat 应两次出现 abc", file=sys.stderr)
            return 1
        if b"MVX" not in data:
            print("mv-regress: 失败: 覆盖目标文件后 cat 应出现 MVX", file=sys.stderr)
            return 1
        tail = data.split(b"ls mvt2")[-1]
        if b"nmv1" not in tail or b"nmv2" not in tail:
            print("mv-regress: 失败: ls mvt2 输出中未出现 nmv1/nmv2", file=sys.stderr)
            return 1
        if b"cannot move directory" not in data:
            print("mv-regress: 失败: 移动目录应打印 cannot move directory", file=sys.stderr)
            return 1
        if b"done_mv" not in data:
            print("mv-regress: 失败: 未执行到结尾标记", file=sys.stderr)
            return 1

        print("mv-regress: 通过")
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
