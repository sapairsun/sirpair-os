#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""QEMU 串口回归：执行 /home/lua_regress.lua（含 print 与字符串），避免 Sirpair sh 无法解析括号。"""
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

# 与 tools/lua_regress.lua 输出一致（含唯一标记，避免与内核串口日志数字混淆）
EXPECT = (b"LUA_REGRESS_START", b"2", b"Hello", b"ab")


def _run_once(root, img, qemu_cmd):
    """单次启动 QEMU 并跑 lua；成功返回 0，失败返回 1。"""
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
            print("lua-regress: 未等到 shell 就绪", file=sys.stderr)
            return 1
        # 与 mv-regress 相同：shell 就绪后 QEMU 可能立即复位虚拟 EHCI，稍候再 exec
        time.sleep(3.0)

        os.write(master_fd, b"lua /home/lua_regress.lua\n")
        read_some(master_fd, buf, 20.0)
        data = buf[0]
        if b"PANIC" in data:
            print("lua-regress: 检测到内核 PANIC，将重试", file=sys.stderr)
            return 1
        for needle in EXPECT:
            if needle not in data:
                print(
                    "lua-regress: 串口输出中未找到 %r" % (needle,),
                    file=sys.stderr,
                )
                sys.stderr.buffer.write(data[-8000:])
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
        print("lua-regress: 缺少 %s" % img, file=sys.stderr)
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
        rc = _run_once(root, img, qemu_cmd)
        if rc == 0:
            print("lua-regress: 通过")
            return 0
        if attempt < 2:
            print(
                "lua-regress: 第 %d 次尝试失败，重试…" % (attempt + 1),
                file=sys.stderr,
            )
            time.sleep(1.0)
    return 1


if __name__ == "__main__":
    sys.exit(main())
