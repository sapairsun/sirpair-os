#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
QEMU 串口回归：登录 shell 后执行「ls bin | grep vi」，须出现 /bin 下名为 vi 的可执行文件列表行。
说明：与 docker-build.sh / qemu_regress_common 一致，默认四核（可用环境变量调整）。
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

# ls 对普通文件着色为绿色后紧跟填充后的文件名「vi」与类型列 [FILE]
_VI_FILE_LINE = re.compile(rb"\x1b\[0;32mvi\s+\[FILE\]")


def main():
    cwd = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(cwd)
    os.chdir(root)

    img = "sirpair-kernel.img"
    if not os.path.isfile(img):
        print("ls-bin-grep-vi-regress: 缺少 %s" % img, file=sys.stderr)
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
            print("ls-bin-grep-vi-regress: 失败: 未等到 shell", file=sys.stderr)
            return 1

        time.sleep(0.8)
        os.write(master_fd, b"ls bin | grep vi\n")
        time.sleep(1.0)
        read_some(master_fd, buf, 5.0)
        data = buf[0]

        if not _VI_FILE_LINE.search(data):
            print(
                "ls-bin-grep-vi-regress: 失败: 未见 vi 可执行文件行（绿色 vi + [FILE]）",
                file=sys.stderr,
            )
            return 1

        print("ls-bin-grep-vi-regress: 通过")
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
