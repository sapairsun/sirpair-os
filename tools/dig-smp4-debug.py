#!/usr/bin/env python3
import os
import pty
import select
import subprocess
import sys
import time
import tty


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    os.chdir(root)
    img = "sirpair-kernel.img"
    if not os.path.isfile(img):
        print("缺少映像: %s" % img, file=sys.stderr)
        return 1

    master_fd, slave_fd = pty.openpty()
    tty.setraw(slave_fd)
    cmd = [
        "timeout",
        "420",
        "qemu-system-i386",
        "-cpu",
        "SandyBridge,-x2apic,-tsc-deadline,-avx,-syscall,-lm",
        "-smp",
        "4",
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
    proc = subprocess.Popen(
        cmd,
        stdin=slave_fd,
        stdout=slave_fd,
        stderr=subprocess.STDOUT,
        close_fds=True,
        cwd=root,
        env=dict(os.environ, TERM="dumb"),
    )
    os.close(slave_fd)

    buf = bytearray()
    start = time.time()
    sent = False
    while time.time() - start < 390:
        r, _, _ = select.select([master_fd], [], [], 0.2)
        if r:
            try:
                chunk = os.read(master_fd, 65536)
            except OSError:
                chunk = b""
            if chunk:
                buf.extend(chunk)
        if (not sent) and (b"root@/# " in buf):
            os.write(master_fd, b"ifconfig\n")
            time.sleep(0.3)
            os.write(master_fd, b"dig www.baidu.com\n")
            time.sleep(0.3)
            os.write(master_fd, b"echo done_dig\n")
            sent = True
        if b"PANIC cpu" in buf or b"kill proc" in buf:
            break
        if sent and b"done_dig" in buf:
            break

    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except Exception:
            proc.kill()

    os.close(master_fd)
    out_path = os.path.join(root, "dig-smp4-debug.log")
    with open(out_path, "wb") as f:
        f.write(buf)
    print("日志已写入: %s (字节=%d)" % (out_path, len(buf)))
    if b"PANIC cpu" in buf:
        print("检测到内核恐慌", file=sys.stderr)
        return 2
    if b"kill proc" in buf:
        print("检测到进程异常杀死", file=sys.stderr)
        return 3
    if b"done_dig" not in buf:
        print("未完成 dig 交互", file=sys.stderr)
        return 4
    print("四核 dig 调试通过")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
