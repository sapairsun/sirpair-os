#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
在映像内 /home 下执行：tcc ./kk.c -o ./kk && ./kk
验证 TinyCC 能正确打开 /tcc/lib/libtcc1_rt.o、/tcc/lib/libsirpairrt.a 并完成链接（含 fprintf 与 64 位除法辅助符号）。
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


def _run_once(root, qemu_cmd):
    master_fd, slave_fd = pty.openpty()
    tty.setraw(slave_fd)

    env = os.environ.copy()
    env["TERM"] = "dumb"

    proc = subprocess.Popen(
        qemu_cmd,
        stdin=slave_fd,
        stdout=slave_fd,
        stderr=subprocess.DEVNULL,
        close_fds=True,
        cwd=root,
        env=env,
    )
    os.close(slave_fd)

    buf = [b""]

    try:
        if not wait_for_shell_ready(master_fd, buf, 180.0):
            print("tcc-home-kk-regress: 未等到 shell", file=sys.stderr)
            return 1

        time.sleep(3.0)
        os.write(master_fd, b"cd /home\n")
        read_some(master_fd, buf, 4.0)
        os.write(master_fd, b"tcc ./kk.c -o ./kk\n")
        read_some(master_fd, buf, 12.0)
        tail = buf[0][-8000:] if len(buf[0]) > 8000 else buf[0]

        bad = (
            b"libtcc1_rt.o' not found",
            b"libsirpairrt.a' not found",
            b"undefined symbol",
            b"file '/tcc/lib/libtcc1",
            b"file '/tcc/libsirpairrt",
        )
        for b in bad:
            if b in tail:
                print("tcc-home-kk-regress: 编译失败，串口片段:", file=sys.stderr)
                sys.stderr.buffer.write(tail[-4000:])
                sys.stderr.buffer.write(b"\n")
                return 1

        os.write(master_fd, b"./kk\n")
        read_some(master_fd, buf, 6.0)
        data = buf[0]
        if b"OK" not in data:
            tail2 = data[-6000:] if len(data) > 6000 else data
            print("tcc-home-kk-regress: 未在输出中看到 OK", file=sys.stderr)
            sys.stderr.buffer.write(tail2[-4000:])
            sys.stderr.buffer.write(b"\n")
            return 1
        if b"trap 14" in data and b"kk" in data:
            print("tcc-home-kk-regress: 运行 kk 后出现页故障（main 须 exit 而非 return）", file=sys.stderr)
            sys.stderr.buffer.write(data[-4000:])
            sys.stderr.buffer.write(b"\n")
            return 1

        print("tcc-home-kk-regress: 通过")
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
        print("tcc-home-kk-regress: 缺少 %s" % img, file=sys.stderr)
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
        rc = _run_once(root, qemu_cmd)
        if rc == 0:
            return 0
        if attempt < 2:
            print(
                "tcc-home-kk-regress: 第 %d 次尝试失败，重试…" % (attempt + 1),
                file=sys.stderr,
            )
            time.sleep(1.5)
    return 1


if __name__ == "__main__":
    sys.exit(main())
