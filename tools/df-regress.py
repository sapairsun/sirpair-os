#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
QEMU 串口回归：验证 df 输出与 Linux 风格表头及根挂载点。
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


def _df_run_once(root, qemu_cmd):
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
            print("df-regress: 失败: 未等到 shell", file=sys.stderr)
            return 1

        def send(chunk):
            os.write(master_fd, chunk)
            time.sleep(0.15)

        send(b"df\n")
        send(b"echo done_df\n")
        time.sleep(2.0)
        read_some(master_fd, buf, 5.0)
        data = buf[0]

        if b"PANIC" in data:
            print("df-regress: 失败: 内核 PANIC", file=sys.stderr)
            return 1
        if b"Filesystem" not in data or b"1K-blocks" not in data:
            print("df-regress: 失败: 缺少表头 Filesystem / 1K-blocks", file=sys.stderr)
            return 1
        if b"Mounted on" not in data:
            print("df-regress: 失败: 缺少表头 Mounted on", file=sys.stderr)
            return 1
        if b"/dev/root" not in data:
            print("df-regress: 失败: 未出现 /dev/root", file=sys.stderr)
            return 1
        if not re.search(rb"/dev/root\s+\d+\s+\d+\s+\d+\s+\d+%\s+/", data):
            print("df-regress: 失败: 数据行格式不符合预期", file=sys.stderr)
            return 1
        if b"done_df" not in data:
            print("df-regress: 失败: 未执行到结尾标记", file=sys.stderr)
            return 1

        print("df-regress: 通过")
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
        print("df-regress: 缺少 %s" % img, file=sys.stderr)
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
        rc = _df_run_once(root, qemu_cmd)
        if rc == 0:
            return 0
        if attempt < 2:
            print(
                "df-regress: 第 %d 次尝试失败，重试…" % (attempt + 1),
                file=sys.stderr,
            )
            time.sleep(1.0)
    return 1


if __name__ == "__main__":
    sys.exit(main())
