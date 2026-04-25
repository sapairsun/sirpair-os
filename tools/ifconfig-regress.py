#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
QEMU 串口回归：验证 ifconfig 能列出 eth0、掩码行与 mtu。
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


def _ifconfig_run_once(root, qemu_cmd):
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
            print("ifconfig-regress: 失败: 未等到 shell", file=sys.stderr)
            return 1

        def send(chunk):
            os.write(master_fd, chunk)
            time.sleep(0.15)

        send(b"ifconfig\n")
        send(b"ifconfig lo\n")
        send(b"ifconfig eth0\n")
        send(b"ifconfig -a\n")
        send(b"echo done_ifconfig\n")
        time.sleep(2.5)
        read_some(master_fd, buf, 5.0)
        data = buf[0]

        if b"PANIC" in data:
            print("ifconfig-regress: 失败: 内核 PANIC", file=sys.stderr)
            return 1
        if b"eth0:" not in data:
            print("ifconfig-regress: 失败: 未出现 eth0:", file=sys.stderr)
            return 1
        if b"lo:" not in data or b"127.0.0.1" not in data:
            print("ifconfig-regress: 失败: 未出现回环 lo / 127.0.0.1", file=sys.stderr)
            return 1
        if b"mtu 1500" not in data:
            print("ifconfig-regress: 失败: 未出现 mtu 1500", file=sys.stderr)
            return 1
        if b"inet" not in data:
            print("ifconfig-regress: 失败: 未出现 inet 地址行", file=sys.stderr)
            return 1
        if b"done_ifconfig" not in data:
            print("ifconfig-regress: 失败: 未执行到结尾标记", file=sys.stderr)
            return 1

        print("ifconfig-regress: 通过")
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
        print("ifconfig-regress: 缺少 %s" % img, file=sys.stderr)
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
        rc = _ifconfig_run_once(root, qemu_cmd)
        if rc == 0:
            return 0
        if attempt < 2:
            print(
                "ifconfig-regress: 第 %d 次尝试失败，重试…" % (attempt + 1),
                file=sys.stderr,
            )
            time.sleep(1.0)
    return 1


if __name__ == "__main__":
    sys.exit(main())
