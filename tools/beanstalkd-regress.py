#!/usr/bin/env python3
"""QEMU 串口回归：启动 beanstalkd 后运行 bstest，检测 BSTEST_OK。"""
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

EXPECT = (b"BSTEST_OK",)


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
            print("beanstalkd-regress: 未等到 shell 就绪", file=sys.stderr)
            return 1
        # USB EHCI 可能在 shell 就绪后仍打印 “resetting ehci HC”，稍等再发命令
        read_some(master_fd, buf, 4.0)
        time.sleep(2.0)

        # 单进程 bsregress：fork+exec beanstalkd，避免 shell 连续两次 exec 与 EHCI 竞态
        os.write(master_fd, b"bsregress\n")
        read_some(master_fd, buf, 120.0)
        data = buf[0]
        if b"PANIC" in data:
            print("beanstalkd-regress: 检测到内核 PANIC", file=sys.stderr)
            return 1
        for needle in EXPECT:
            if needle not in data:
                print(
                    "beanstalkd-regress: 串口输出中未找到 %r" % (needle,),
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
        print("beanstalkd-regress: 缺少 %s" % img, file=sys.stderr)
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
            print("beanstalkd-regress: 通过")
            return 0
        if attempt < 2:
            print(
                "beanstalkd-regress: 第 %d 次尝试失败，重试…" % (attempt + 1),
                file=sys.stderr,
            )
            time.sleep(2.0)
    return 1


if __name__ == "__main__":
    sys.exit(main())
