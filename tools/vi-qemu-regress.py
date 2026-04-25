#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
在 Docker/Linux 下用 PTY 驱动 QEMU 串口，对 vi 做最小交互回归：
  1) 启动到 shell
  2) 无参数启动 vi，按 e 进入插入态应出现 INSERT
  3) 经 :set nu 等操作后回到普通模式，再按 i 应再次出现 INSERT（同一会话，避免二次启动 vi）
  4) 退出 :q!
失败则非零退出（供 docker-build.sh 调用）。
"""
import os
import pty
import re
import signal
import subprocess
import sys
import time
import tty

_tools_dir = os.path.dirname(os.path.abspath(__file__))
if _tools_dir not in sys.path:
    sys.path.insert(0, _tools_dir)
from qemu_regress_common import QEMU_SMP, read_some, wait_for_shell_ready

MARK_INSERT = b"INSERT"


def last_status_col(data):
    """解析串口输出里最后一次出现的 ", Col N" 中的 N（无则返回 None）。"""
    last_n = None
    needle = b", Col "
    pos = 0
    while True:
        i = data.find(needle, pos)
        if i < 0:
            break
        j = i + len(needle)
        k = j
        while k < len(data) and 48 <= data[k] <= 57:
            k += 1
        if k > j:
            last_n = int(data[j:k])
        pos = i + 1
    return last_n


def last_status_ln_pair(data):
    """
    解析最后一次状态栏中的 Ln A/B，返回 (A,B)；无则返回 None。
    """
    m = None
    for m in re.finditer(rb"Ln (\d+)/(\d+)", data):
        pass
    if not m:
        return None
    return int(m.group(1)), int(m.group(2))


def assert_readme_first_line_visible(data):
    l1 = b"VI_README_L1"
    l2 = b"VI_README_L2"
    i1 = data.find(l1)
    i2 = data.find(l2)
    if i1 < 0:
        return "README 首行缺失"
    if i2 < 0:
        return "README 第二行缺失"
    if i1 > i2:
        return "README 首行/次行顺序异常"
    return None


def main():
    cwd = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(cwd)
    os.chdir(root)

    img = "sirpair-kernel.img"
    if not os.path.isfile(img):
        print("vi-qemu-regress: missing %s" % img, file=sys.stderr)
        return 1

    # 与 Makefile 的 qemu-nox 一致：仅用 -nographic，避免 mon:stdio 与串口复用导致按键丢失
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

    master_fd, slave_fd = pty.openpty()
    # 规范模式会缓冲单键，导致 vi 收不到 e/i；从端需原始模式
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
            print("vi-qemu-regress: FAIL: shell not ready", file=sys.stderr)
            return 1

        # 准备 README 场景：构造确定内容，验证首行不会被丢失/跳过。
        os.write(master_fd, b"echo VI_README_L1 > /home/README\n")
        time.sleep(0.8)
        read_some(master_fd, buf, 2.0)
        os.write(master_fd, b"echo VI_README_L2 >> /home/README\n")
        time.sleep(0.8)
        read_some(master_fd, buf, 2.0)
        os.write(master_fd, b"vi /home/README\n")
        time.sleep(1.2)
        read_some(master_fd, buf, 5.0)
        if b"[New File]" in buf[0]:
            print("vi-qemu-regress: FAIL: vi /home/README 显示 [New File]", file=sys.stderr)
            return 1
        ln = last_status_ln_pair(buf[0])
        if ln is None:
            print("vi-qemu-regress: FAIL: 未解析到状态栏 Ln A/B", file=sys.stderr)
            return 1
        if ln[1] < 10:
            print(
                "vi-qemu-regress: FAIL: README 总行数异常过小 (%d)" % (ln[1],),
                file=sys.stderr,
            )
            return 1
        e = assert_readme_first_line_visible(buf[0])
        if e:
            print("vi-qemu-regress: FAIL: %s" % e, file=sys.stderr)
            sys.stderr.buffer.write(buf[0][-6000:])
            return 1

        # :行号 —— 跳转到指定行（普通模式下 :10 表示第 10 行）
        os.write(master_fd, b":10\n")
        time.sleep(0.9)
        read_some(master_fd, buf, 3.0)
        ln10 = last_status_ln_pair(buf[0])
        if ln10 is None or ln10[0] != 10:
            print(
                "vi-qemu-regress: FAIL: :10 后状态栏行号应为 10 (got %r)"
                % (ln10,),
                file=sys.stderr,
            )
            return 1
        os.write(master_fd, b":1\n")
        time.sleep(0.9)
        read_some(master_fd, buf, 3.0)
        ln1 = last_status_ln_pair(buf[0])
        if ln1 is None or ln1[0] != 1:
            print(
                "vi-qemu-regress: FAIL: :1 后状态栏行号应为 1 (got %r)" % (ln1,),
                file=sys.stderr,
            )
            return 1

        os.write(master_fd, b"\x1b:q!\n")
        time.sleep(0.6)
        read_some(master_fd, buf, 2.0)

        # 无文件名启动 vi，避免 USB 读文件阶段与 EHCI reset 竞态导致首屏永不输出
        os.write(master_fd, b"vi\n")
        time.sleep(1.2)
        read_some(master_fd, buf, 5.0)
        os.write(master_fd, b"e")
        time.sleep(0.8)
        read_some(master_fd, buf, 2.0)
        if MARK_INSERT not in buf[0]:
            print("vi-qemu-regress: FAIL: key 'e' did not show INSERT", file=sys.stderr)
            tail = buf[0][-2500:] if len(buf[0]) > 2500 else buf[0]
            print("vi-qemu-regress: buflen=%d tail=%r" % (len(buf[0]), tail), file=sys.stderr)
            return 1

        # 插入一个字符后状态栏列号应变为 2（依赖内核 CUP 与重绘）
        os.write(master_fd, b"a")
        time.sleep(0.7)
        read_some(master_fd, buf, 2.0)
        if b", Col 2" not in buf[0]:
            print("vi-qemu-regress: FAIL: status Col did not advance after typing", file=sys.stderr)
            return 1

        # 左方向键：串口常见为 ESC [ D；真机 PS/2 为单字节 0xe4（须与 user/vi.c read_key 一致）
        os.write(master_fd, b"\x1b[D")
        time.sleep(0.6)
        read_some(master_fd, buf, 2.0)
        if last_status_col(buf[0]) != 1:
            print(
                "vi-qemu-regress: FAIL: ANSI left arrow did not reach Col 1 (last=%r)"
                % (last_status_col(buf[0]),),
                file=sys.stderr,
            )
            return 1
        os.write(master_fd, b"\x1b[C")
        time.sleep(0.6)
        read_some(master_fd, buf, 2.0)
        if last_status_col(buf[0]) != 2:
            print(
                "vi-qemu-regress: FAIL: ANSI right arrow did not return to Col 2 (last=%r)"
                % (last_status_col(buf[0]),),
                file=sys.stderr,
            )
            return 1

        # SS3（应用光标键模式）：ESC O D / ESC O C，旧版 read_key 会把 O 后字节当普通字符插入
        os.write(master_fd, b"\x1bOD")
        time.sleep(0.6)
        read_some(master_fd, buf, 2.0)
        if last_status_col(buf[0]) != 1:
            print(
                "vi-qemu-regress: FAIL: SS3 left (ESC O D) did not reach Col 1 (last=%r)"
                % (last_status_col(buf[0]),),
                file=sys.stderr,
            )
            return 1
        os.write(master_fd, b"\x1bOC")
        time.sleep(0.6)
        read_some(master_fd, buf, 2.0)
        if last_status_col(buf[0]) != 2:
            print(
                "vi-qemu-regress: FAIL: SS3 right (ESC O C) did not return to Col 2 (last=%r)"
                % (last_status_col(buf[0]),),
                file=sys.stderr,
            )
            return 1

        # CSI 带参数：ESC [ 1 ; 5 D / C（修饰键方向键），旧版会丢字节导致错位
        os.write(master_fd, b"\x1b[1;5D")
        time.sleep(0.6)
        read_some(master_fd, buf, 2.0)
        if last_status_col(buf[0]) != 1:
            print(
                "vi-qemu-regress: FAIL: CSI param left did not reach Col 1 (last=%r)"
                % (last_status_col(buf[0]),),
                file=sys.stderr,
            )
            return 1
        os.write(master_fd, b"\x1b[1;5C")
        time.sleep(0.6)
        read_some(master_fd, buf, 2.0)
        if last_status_col(buf[0]) != 2:
            print(
                "vi-qemu-regress: FAIL: CSI param right did not return to Col 2 (last=%r)"
                % (last_status_col(buf[0]),),
                file=sys.stderr,
            )
            return 1

        os.write(master_fd, b"\xe4")
        time.sleep(0.6)
        read_some(master_fd, buf, 2.0)
        if last_status_col(buf[0]) != 1:
            print(
                "vi-qemu-regress: FAIL: PS/2-style left (0xe4) did not reach Col 1 (last=%r)"
                % (last_status_col(buf[0]),),
                file=sys.stderr,
            )
            return 1

        # 光标须在列 2 时退格才删首字符；先右移回列 2
        os.write(master_fd, b"\x1b[C")
        time.sleep(0.5)
        read_some(master_fd, buf, 2.0)
        if last_status_col(buf[0]) != 2:
            print(
                "vi-qemu-regress: FAIL: could not restore Col 2 before backspace test (last=%r)"
                % (last_status_col(buf[0]),),
                file=sys.stderr,
            )
            return 1

        # 插入模式下退格应删字符并使状态栏列号回到 1（内核须在 902h 下传递 \\b/0x7f）
        os.write(master_fd, b"\x08")
        time.sleep(0.7)
        read_some(master_fd, buf, 2.0)
        if last_status_col(buf[0]) != 1:
            print(
                "vi-qemu-regress: FAIL: backspace did not return to Col 1 (last=%r)"
                % (last_status_col(buf[0]),),
                file=sys.stderr,
            )
            return 1

        # :set nu 行号为绿色（ANSI 32）；须在插入态退格等之后执行，避免处于普通模式时退格无定义
        os.write(master_fd, b"\x1b:set nu\n")
        time.sleep(0.9)
        read_some(master_fd, buf, 2.0)
        if b"\x1b[32m" not in buf[0]:
            print("vi-qemu-regress: FAIL: set nu 未产生绿色行号序列", file=sys.stderr)
            return 1

        # 同一会话内再测 i -> INSERT（:set nu 后已回普通模式）。清空 buf 以免此前「e」产生的 INSERT 误判。
        buf[0] = b""
        os.write(master_fd, b"i")
        time.sleep(0.9)
        read_some(master_fd, buf, 4.0)
        if MARK_INSERT not in buf[0]:
            print("vi-qemu-regress: FAIL: key 'i' did not show INSERT", file=sys.stderr)
            tail = buf[0][-2500:] if len(buf[0]) > 2500 else buf[0]
            print("vi-qemu-regress: buflen=%d tail=%r" % (len(buf[0]), tail), file=sys.stderr)
            return 1

        os.write(master_fd, b"\x1b:q!\n")
        time.sleep(0.5)
    finally:
        try:
            proc.send_signal(signal.SIGTERM)
        except OSError:
            pass
        try:
            proc.wait(timeout=8)
        except subprocess.TimeoutExpired:
            proc.kill()

    print("vi-qemu-regress: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
