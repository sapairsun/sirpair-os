#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""在 /bin 下执行管道输入 more，确保 tty 打开 /console 而非误用 fd0（管道）。"""
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
        print("more-pipe-regress: 缺少 %s" % img, file=sys.stderr)
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
            print("more-pipe-regress: 未等到 shell 就绪", file=sys.stderr)
            return 1

        os.write(master_fd, b"cd bin\n")
        read_some(master_fd, buf, 2.0)
        os.write(master_fd, b"echo MORE_PIPE_OK | more\n")
        read_some(master_fd, buf, 2.0)
        # 须在此刻检查：后续 ls|more 输出量很大，会淹没缓冲区末尾
        if b"MORE_PIPE_OK" not in buf[0]:
            print("more-pipe-regress: 未见 echo|more 输出", file=sys.stderr)
            tail = buf[0][-4000:] if len(buf[0]) > 4000 else buf[0]
            sys.stderr.buffer.write(tail)
            return 1
        # 单行落在首屏内，more 不等待；再测 ls|more 首屏+滚动
        os.write(master_fd, b"ls | more\n")
        read_some(master_fd, buf, 2.0)
        # 方向键：ANSI CSI 与内核单字节键码（与 vi/game 双路径一致）
        os.write(master_fd, b"\x1b[B\x1b[A")
        os.write(master_fd, b"\xe3\xe2")
        read_some(master_fd, buf, 1.0)
        for _ in range(48):
            os.write(master_fd, b"\n")
            read_some(master_fd, buf, 0.12)
        read_some(master_fd, buf, 2.0)
        data = buf[0]

        # 全量检查：滚动多屏后 NAME/TYPE 不一定仍在「末尾 8KB」内
        if b"NAME" not in data and b"TYPE" not in data:
            print("more-pipe-regress: 未见 ls 列表（首屏）", file=sys.stderr)
            sys.stderr.buffer.write(data[-4000:])
            return 1
        if b"more: read error" in data:
            print("more-pipe-regress: more 报错", file=sys.stderr)
            sys.stderr.buffer.write(data[-4000:])
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
