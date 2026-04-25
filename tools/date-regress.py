#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
QEMU 串口回归：验证 date --regress 单次 exec 内输出的默认日期行、两次 REG-EPOCH、ISO 日期。
（避免 QEMU 首次用户程序后 EHCI 复位导致第二次 exec 无法从 USB 盘加载。）
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
        print("date-regress: 缺少 %s" % img, file=sys.stderr)
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
            print("date-regress: 失败: 未等到 shell", file=sys.stderr)
            return 1
        time.sleep(3.0)

        os.write(master_fd, b"date --regress\n")
        time.sleep(0.5)
        read_some(master_fd, buf, 1.0)
        poll_end = time.time() + 60.0
        while time.time() < poll_end:
            read_some(master_fd, buf, 0.35)
            if b"REG-EPOCH2 " in buf[0]:
                break
        data = buf[0]

        if not re.search(rb"UTC \d{4}", data):
            print("date-regress: 失败: 默认日期行未含 UTC 与四位年份", file=sys.stderr)
            return 1

        m1 = re.search(rb"REG-EPOCH1 (\d+)", data)
        m2 = re.search(rb"REG-EPOCH2 (\d+)", data)
        if not m1 or not m2:
            print("date-regress: 失败: 未解析 REG-EPOCH1/REG-EPOCH2", file=sys.stderr)
            return 1
        e1 = int(m1.group(1))
        e2 = int(m2.group(1))
        if e1 < 946684800 or e2 < 946684800:
            print("date-regress: 失败: 时间戳不在合理 Unix 秒范围", file=sys.stderr)
            return 1
        if e2 < e1:
            print(
                "date-regress: 失败: 时间戳应随墙钟递增 (e1=%d e2=%d)" % (e1, e2),
                file=sys.stderr,
            )
            return 1

        if not re.search(rb"\d{4}-\d{2}-\d{2}", data):
            print("date-regress: 失败: 未输出 ISO 日期 (%%Y-%%m-%%d)", file=sys.stderr)
            return 1

        print("date-regress: 通过 (epoch %d -> %d)" % (e1, e2))
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
