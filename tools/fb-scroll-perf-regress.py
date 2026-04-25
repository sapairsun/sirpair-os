#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
QEMU 串口回归：执行 fb-scroll-bench 超一屏滚动压测，确保命令可运行并产出 ticks 指标。
该回归用于性能趋势追踪，避免滚屏路径回退成明显更慢或卡住。
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


def parse_stat(blob):
    m = re.search(rb"fb-scroll-bench: lines (\d+) \| delta (\d+) ticks \| us_per_line (\d+)", blob)
    if not m:
        return None
    return int(m.group(1)), int(m.group(2)), int(m.group(3))


def main():
    cwd = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(cwd)
    os.chdir(root)

    img = "sirpair-kernel.img"
    if not os.path.isfile(img):
        print("fb-scroll-perf-regress: 缺少 %s" % img, file=sys.stderr)
        return 1

    qemu_cmd = [
        "timeout",
        "260",
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
            print("fb-scroll-perf-regress: 未等到 shell 就绪", file=sys.stderr)
            return 1

        os.write(master_fd, b"fb-scroll-bench 2200\n")
        read_some(master_fd, buf, 3.0)
        poll_end = time.time() + 120.0
        stat = None
        while time.time() < poll_end:
            read_some(master_fd, buf, 0.5)
            stat = parse_stat(buf[0])
            if stat is not None:
                break
        if stat is None:
            print("fb-scroll-perf-regress: 未解析到压测统计行", file=sys.stderr)
            sys.stderr.buffer.write(buf[0][-6000:])
            return 1

        lines, delta, us_per_line = stat
        if lines < 1000:
            print("fb-scroll-perf-regress: 行数异常 %d" % lines, file=sys.stderr)
            return 1
        if delta <= 0 or delta > 5000:
            print("fb-scroll-perf-regress: ticks 异常 %d" % delta, file=sys.stderr)
            return 1
        if us_per_line <= 0 or us_per_line > 500000:
            print("fb-scroll-perf-regress: us_per_line 异常 %d" % us_per_line, file=sys.stderr)
            return 1

        print(
            "fb-scroll-perf-regress: 通过 (lines=%d, delta=%d, us_per_line=%d)"
            % (lines, delta, us_per_line)
        )
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
