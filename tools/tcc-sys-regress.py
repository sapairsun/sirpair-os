#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""可选：QEMU 串口下运行 /bin/tcc_sys_regress（对 /home/t01.c…t08.c 依次 fork/exec tcc）。

说明：部分 QEMU 配置在 EHCI「processing error - resetting」后，首次 exec 大型 tcc 可能长时间无串口输出；
若本脚本超时失败，可在真机或稳定存储上手动执行 tcc_sys_regress。CI 以 docker-build.sh 中静态校验为准。
"""
import os
import pty
import shutil
import subprocess
import sys
import time
import tty

_tools_dir = os.path.dirname(os.path.abspath(__file__))
if _tools_dir not in sys.path:
    sys.path.insert(0, _tools_dir)
from qemu_regress_common import QEMU_SMP, read_some, wait_for_shell_ready

EXPECT = tuple(("TCCSYS%02d" % i).encode() for i in range(1, 9))


def _run_once(root, img, qemu_cmd):
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
            print("tcc-sys-regress: 未等到 shell 就绪", file=sys.stderr)
            return 1
        if b"resetting ehci" in buf[0]:
            time.sleep(20.0)
        else:
            time.sleep(6.0)

        os.write(master_fd, b"/bin/tcc_sys_regress\n")
        read_some(master_fd, buf, 300.0)
        data = buf[0]
        if b"PANIC" in data:
            print("tcc-sys-regress: 检测到内核 PANIC，将重试", file=sys.stderr)
            return 1
        for needle in EXPECT:
            if needle not in data:
                print(
                    "tcc-sys-regress: 串口输出中未找到 %r" % (needle,),
                    file=sys.stderr,
                )
                sys.stderr.buffer.write(data[-16000:])
                return 1
        return 0
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()
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
        print("tcc-sys-regress: 缺少 %s" % img, file=sys.stderr)
        return 1

    qemu_cmd = []
    to = shutil.which("timeout")
    if to:
        qemu_cmd.extend([to, "420"])
    qemu_cmd.extend(
        [
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
    )

    for attempt in range(3):
        rc = _run_once(root, img, qemu_cmd)
        if rc == 0:
            print("tcc-sys-regress: 通过")
            return 0
        if attempt < 2:
            print(
                "tcc-sys-regress: 第 %d 次尝试失败，重试…" % (attempt + 1),
                file=sys.stderr,
            )
            time.sleep(1.0)
    return 1


if __name__ == "__main__":
    sys.exit(main())
