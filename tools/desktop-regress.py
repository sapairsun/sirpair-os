#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
桌面程序 desktop：静态检查 + QEMU 串口最小冒烟。
  1) build/_desktop 存在；fs.img 中可发现 desktop 程序名
  2) 启动 shell 后执行 desktop，应出现魔数 DM_START，再按 q 退出后出现 DM_END 与 shell
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

# 控制台会把 \n 显示为 \r\n，匹配时勿依赖单一换行符
DM_START = b"V6D0"
DM_END = b"V6D1"


def static_check(root):
    exe = os.path.join(root, "build", "_desktop")
    fsimg = os.path.join(root, "build", "fs.img")
    if not os.path.isfile(exe):
        return "缺少 build/_desktop"
    if not os.path.isfile(fsimg):
        return "缺少 build/fs.img"
    with open(fsimg, "rb") as f:
        blob = f.read()
    if b"desktop" not in blob:
        return "fs.img 中未发现 desktop 串"
    with open(exe, "rb") as f:
        exeb = f.read()
    if b"Sirpair" not in exeb:
        return "build/_desktop 中未发现关于窗口正文片段"
    return None


def main():
    cwd = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(cwd)
    os.chdir(root)

    err = static_check(root)
    if err:
        print("desktop-regress: 静态检查失败: %s" % err, file=sys.stderr)
        return 1

    img = "sirpair-kernel.img"
    if not os.path.isfile(img):
        print("desktop-regress: missing %s" % img, file=sys.stderr)
        return 1

    qemu_cmd = [
        "timeout",
        "120",
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
            print("desktop-regress: FAIL: shell not ready", file=sys.stderr)
            return 1

        # EHCI/USB 盘首访可能较慢，稍候再启动桌面以免 exec 竞态
        time.sleep(2.5)
        read_some(master_fd, buf, 2.0)

        os.write(master_fd, b"/bin/desktop\n")
        time.sleep(2.0)
        read_some(master_fd, buf, 25.0)

        if DM_START not in buf[0]:
            tail = buf[0][-1200:] if len(buf[0]) > 1200 else buf[0]
            print("desktop-regress: FAIL: 未收到桌面启动标记 tail=%r" % (tail,), file=sys.stderr)
            return 1

        os.write(master_fd, b"q")
        time.sleep(1.2)
        read_some(master_fd, buf, 5.0)

        if DM_END not in buf[0]:
            print("desktop-regress: FAIL: 未收到退出魔数", file=sys.stderr)
            return 1

        if b"root@/# " not in buf[0][-800:]:
            # 允许提示符在缓冲任意位置；若末尾没有则再读
            read_some(master_fd, buf, 3.0)
        if b"root@/# " not in buf[0]:
            print("desktop-regress: FAIL: 退出后未恢复 shell 提示符", file=sys.stderr)
            return 1

        print("desktop-regress: ok")
        return 0
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()
            proc.wait()


if __name__ == "__main__":
    sys.exit(main())
