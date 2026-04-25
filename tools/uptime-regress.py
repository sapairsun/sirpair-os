#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
QEMU 串口回归：验证 uptime --drift 在单次 exec 内报告的 ticks 增量约 200（100Hz）。
说明：默认四核与合并映像/USB 栈一致；若遇不稳定可设环境变量 SIRPAIR_QEMU_SMP=1 做单核对照。
真机多核不受影响。EHCI 复位后亦不宜依赖「连续两次 exec」。
故 uptime 用「单次进程内两次采样」而非两次运行 uptime。
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


def parse_delta(data):
    """解析 uptime-drift 行中的 delta 整数。"""
    m = re.search(rb"uptime-drift: ticks \d+ \| ticks \d+ \| delta (\d+)", data)
    if m:
        return int(m.group(1))
    return None


def main():
    cwd = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(cwd)
    os.chdir(root)

    img = "sirpair-kernel.img"
    if not os.path.isfile(img):
        print("uptime-regress: 缺少 %s" % img, file=sys.stderr)
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
            print("uptime-regress: 失败: 未等到 shell", file=sys.stderr)
            return 1

        os.write(master_fd, b"uptime --drift\n")
        time.sleep(0.5)
        read_some(master_fd, buf, 1.0)
        poll_end = time.time() + 40.0
        while time.time() < poll_end:
            read_some(master_fd, buf, 0.35)
            delta = parse_delta(buf[0])
            if delta is not None:
                break
        if parse_delta(buf[0]) is None:
            print("uptime-regress: 失败: 未解析到 uptime-drift 行", file=sys.stderr)
            return 1

        delta = parse_delta(buf[0])
        if delta < 50 or delta > 600:
            print(
                "uptime-regress: 失败: ticks 增量 %d 不在 [50,600]（期望约 200 @100Hz）"
                % delta,
                file=sys.stderr,
            )
            return 1

        print("uptime-regress: 通过 (delta=%d)" % delta)
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
