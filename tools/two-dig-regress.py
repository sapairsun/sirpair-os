#!/usr/bin/env python3
import os
import pty
import select
import subprocess
import sys
import time
import tty

from qemu_regress_common import QEMU_SMP, read_some, wait_for_shell_ready


def run_once(root, qemu_cmd):
    master_fd, slave_fd = pty.openpty()
    tty.setraw(slave_fd)
    proc = subprocess.Popen(
        qemu_cmd,
        stdin=slave_fd,
        stdout=slave_fd,
        stderr=subprocess.STDOUT,
        close_fds=True,
        cwd=root,
        env=dict(os.environ, TERM="dumb"),
    )
    os.close(slave_fd)
    buf = [b""]
    try:
        if not wait_for_shell_ready(master_fd, buf, 320):
            return 1, buf[0], "未等到命令行"
        os.write(master_fd, b"dig www.baidu.com\n")
        time.sleep(5.0)
        read_some(master_fd, buf, 4.0)
        os.write(master_fd, b"dig www.baidu.com\n")
        time.sleep(6.0)
        read_some(master_fd, buf, 6.0)
        os.write(master_fd, b"echo done_two_dig\n")
        time.sleep(1.0)
        read_some(master_fd, buf, 4.0)
        data = buf[0]
        if b"PANIC cpu" in data:
            return 2, data, "出现内核恐慌"
        if b"kill proc" in data:
            return 3, data, "出现进程异常终止"
        if data.count(b"www.baidu.com A ") < 2:
            return 4, data, "两次解析结果不足"
        if b"done_two_dig" not in data:
            return 5, data, "命令行未恢复"
        return 0, data, "通过"
    finally:
        try:
            proc.terminate()
            proc.wait(timeout=3)
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
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    os.chdir(root)
    qemu_cmd = [
        "timeout", "260",
        "qemu-system-i386",
        "-cpu", "SandyBridge,-x2apic,-tsc-deadline,-avx,-syscall,-lm",
        "-smp", QEMU_SMP,
        "-m", "512",
        "-nographic",
        "-usb",
        "-device", "usb-ehci,id=ehci",
        "-device", "usb-storage,bus=ehci.0,drive=usbdisk,bootindex=1",
        "-drive", "if=none,id=usbdisk,file=sirpair-kernel.img,format=raw",
        "-device", "e1000e,netdev=net0",
        "-netdev", "user,id=net0",
        "-rtc", "base=localtime,clock=host",
    ]
    rc, data, msg = run_once(root, qemu_cmd)
    with open(os.path.join(root, "two-dig-last.log"), "wb") as f:
        f.write(data)
    if rc == 0:
        print("two-dig-regress: 通过")
    else:
        print("two-dig-regress: 失败: %s" % msg, file=sys.stderr)
    return rc


if __name__ == "__main__":
    sys.exit(main())
