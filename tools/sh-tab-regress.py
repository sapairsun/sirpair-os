#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""回归：shell 行编辑下按 Tab 须能补全；pw+Tab+回车 应能执行 pwd；ki+Tab+Tab 应补成连续 kill 而非 ki+空格+ll。"""
import os
import pty
import re
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
        print("sh-tab-regress: 缺少 %s" % img, file=sys.stderr)
        return 1

    qemu_cmd = [
        "timeout",
        "120",
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
            print("sh-tab-regress: 未等到 shell 就绪", file=sys.stderr)
            return 1

        # pw + Tab 应补全为 pwd，回车后输出含绝对路径
        os.write(master_fd, b"pw\t\n")
        read_some(master_fd, buf, 5.0)
        data = buf[0]
        tail = data[-8000:] if len(data) > 8000 else data
        if b"PANIC" in tail or b"trap 14" in tail:
            print("sh-tab-regress: 内核异常", file=sys.stderr)
            sys.stderr.buffer.write(tail[-4000:])
            return 1
        if b"/" not in tail:
            print("sh-tab-regress: pwd 输出中未见到路径字符", file=sys.stderr)
            sys.stderr.buffer.write(tail[-4000:])
            return 1

        # ki + Tab + Tab + 回车：应补全为 kill 并执行（无参打印 usage），且串口历史中不得出现 ki 与 ll 被空格分开的旧 bug
        os.write(master_fd, b"ki\t\t\n")
        read_some(master_fd, buf, 3.0)
        data = buf[0]
        tail2 = data[-12000:] if len(data) > 12000 else data
        if b"PANIC" in tail2 or b"trap 14" in tail2:
            print("sh-tab-regress: ki Tab 后内核异常", file=sys.stderr)
            sys.stderr.buffer.write(tail2[-4000:])
            return 1
        if re.search(b"ki +ll", tail2):
            print("sh-tab-regress: 仍存在 ki 与 ll 之间空格间隙", file=sys.stderr)
            sys.stderr.buffer.write(tail2[-4000:])
            return 1
        if b"usage: kill" not in tail2 and b"kill pid" not in tail2:
            print("sh-tab-regress: kill 未执行或未见 usage", file=sys.stderr)
            sys.stderr.buffer.write(tail2[-4000:])
            return 1

        print("sh-tab-regress: ok")
        return 0
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()


if __name__ == "__main__":
    sys.exit(main())
