#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
QEMU 串口回归：dig 解析域名（UDP/DNS），并确认 shell 在子进程结束后仍可用。
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


def _dump_fail_log(root, data):
    try:
        with open(os.path.join(root, "dig-regress-last-fail.log"), "wb") as f:
            f.write(data)
    except Exception:
        pass


def _dig_run_once(root, qemu_cmd):
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
        if not wait_for_shell_ready(master_fd, buf, 220):
            print("dig-regress: 失败: 未等到 shell", file=sys.stderr)
            return 1

        def send(chunk):
            os.write(master_fd, chunk)
            time.sleep(0.2)

        send(b"dig www.baidu.com\n")
        time.sleep(4.0)
        send(b"echo after_dig_ok\n")
        time.sleep(1.0)
        read_some(master_fd, buf, 4.0)
        data = buf[0]

        # 勿仅用 b"PANIC"：串口噪声可能偶然出现相同字节，易误判。
        if b"PANIC cpu" in data or b"kfree: bad addr v=" in data:
            _dump_fail_log(root, data)
            print("dig-regress: 失败: 内核恐慌或 kfree 校验失败", file=sys.stderr)
            return 1
        if b"after_dig_ok" not in data:
            _dump_fail_log(root, data)
            print("dig-regress: 失败: 未执行到结尾标记（shell 可能崩溃）", file=sys.stderr)
            return 1
        if b"www.baidu.com" not in data or b" A " not in data:
            _dump_fail_log(root, data)
            print("dig-regress: 失败: 未出现 DNS A 记录输出", file=sys.stderr)
            return 1

        print("dig-regress: 通过")
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
        print("dig-regress: 缺少 %s" % img, file=sys.stderr)
        return 1

    qemu_cmd = [
        "timeout",
        "240",
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
        rc = _dig_run_once(root, qemu_cmd)
        if rc == 0:
            return 0
        if attempt < 2:
            print(
                "dig-regress: 第 %d 次尝试失败，重试…" % (attempt + 1),
                file=sys.stderr,
            )
            time.sleep(1.0)
    return 1


if __name__ == "__main__":
    sys.exit(main())
