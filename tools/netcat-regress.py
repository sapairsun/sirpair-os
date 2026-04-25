#!/usr/bin/env python3
"""
QEMU 串口回归：验证 netcat（TCP 回环）及后台监听 + telnet 不断连。

含：无参用法 → 后台 TCP 监听 → 管道客户端 → 第二端口后台监听 → telnet 管道，
且串口不得出现 init 的「zombie!」（后台过继子进程由 init 静默回收）。
"""
import os
import subprocess
import sys
import time

_tools_dir = os.path.dirname(os.path.abspath(__file__))
if _tools_dir not in sys.path:
    sys.path.insert(0, _tools_dir)
from qemu_regress_common import QEMU_SMP, read_some, wait_for_shell_ready


def _run_once(root, img, qemu_cmd):
    import pty
    import tty

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
        # 在 CI / 容器负载偏高时，QEMU + USB 启动偶发较慢，适当放宽等待窗口。
        if not wait_for_shell_ready(master_fd, buf, 180.0):
            print("netcat-regress: 未等到 shell 就绪", file=sys.stderr)
            if buf[0]:
                sys.stderr.buffer.write(buf[0][-4000:])
            return 1
        read_some(master_fd, buf, 4.0)
        time.sleep(2.0)

        os.write(master_fd, b"netcat\n")
        read_some(master_fd, buf, 8.0)

        os.write(master_fd, b"netcat -l -s 127.0.0.1 -p 8080 &\n")
        read_some(master_fd, buf, 6.0)
        time.sleep(3.0)

        os.write(master_fd, b"echo NCOK | netcat 127.0.0.1 8080\n")
        read_some(master_fd, buf, 45.0)

        os.write(master_fd, b"netcat -l -s 127.0.0.1 -p 9090 &\n")
        read_some(master_fd, buf, 6.0)
        time.sleep(3.0)

        os.write(master_fd, b"echo TELCHK | telnet 127.0.0.1 9090\n")
        read_some(master_fd, buf, 50.0)

        data = buf[0]
        if b"PANIC cpu" in data or b"PANIC" in data:
            print("netcat-regress: 检测到内核 PANIC", file=sys.stderr)
            return 1
        if b"zombie!" in data:
            print(
                "netcat-regress: 串口出现 init 的 zombie!（后台监听与 telnet 场景应静默回收）",
                file=sys.stderr,
            )
            sys.stderr.buffer.write(data[-8000:])
            return 1
        if "用法:".encode("utf-8") not in data:
            print("netcat-regress: 未见到「用法:」（无参应打印用法）", file=sys.stderr)
            sys.stderr.buffer.write(data[-6000:])
            return 1
        if b"NCOK" not in data:
            print(
                "netcat-regress: 未见到 NCOK（TCP 监听/客户端中继失败）",
                file=sys.stderr,
            )
            sys.stderr.buffer.write(data[-8000:])
            return 1
        if b"TELCHK" not in data:
            print(
                "netcat-regress: 未见到 TELCHK（telnet 经回连未打到后台 netcat）",
                file=sys.stderr,
            )
            sys.stderr.buffer.write(data[-8000:])
            return 1
        if b"> " not in data:
            print(
                "netcat-regress: telnet 管道场景应出现 \"> \" 提示符",
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
        print("netcat-regress: 缺少 %s" % img, file=sys.stderr)
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

    for attempt in range(4):
        rc = _run_once(root, img, qemu_cmd)
        if rc == 0:
            print("netcat-regress: 通过")
            return 0
        if attempt < 3:
            print(
                "netcat-regress: 第 %d 次尝试失败，重试…" % (attempt + 1),
                file=sys.stderr,
            )
            time.sleep(2.0)
    return 1


if __name__ == "__main__":
    sys.exit(main())
