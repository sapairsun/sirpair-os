#!/usr/bin/env python3
"""
QEMU 串口回归：echo-server TCP/UDP 回环（127.0.0.1）与 udp_line_client。
"""
import os
import subprocess
import sys
import time

_tools_dir = os.path.dirname(os.path.abspath(__file__))
if _tools_dir not in sys.path:
    sys.path.insert(0, _tools_dir)
from qemu_regress_common import QEMU_SMP, read_some, wait_for_shell_ready


def _last_ps_table_chunk(blob):
    """
    取串口缓冲中最后一次 ps 输出块。
    勿用 rfind(b'NAME')：会命中表头 ... SIZE NAME 后紧跟 root@ 的碎片，导致下一行即 root@、
    在解析到任何 PID 行之前就 break，收集不到 echo-server 的 pid。
    """
    marker = b"\nPID   PPID"
    idx = blob.rfind(marker)
    if idx >= 0:
        return blob[idx + 1 :]
    idx = blob.rfind(b"PID   PPID")
    if idx >= 0:
        return blob[idx:]
    idx = blob.rfind(b"NAME")
    if idx < 0:
        return b""
    return blob[idx:]


def _count_ps_name_echo_server(blob):
    """解析 ps 表格（NAME 列末字段为 echo-server 的行数）。"""
    chunk = _last_ps_table_chunk(blob)
    if not chunk:
        return -1
    lines = chunk.splitlines()
    if not lines:
        return -1
    n = 0
    for line in lines[1:]:
        line = line.strip()
        if not line:
            continue
        if line.startswith(b"root@"):
            break
        parts = line.split()
        if len(parts) >= 6 and parts[-1] == b"echo-server":
            if parts[3] == b"ZOMBIE":
                continue
            n += 1
    return n


def _collect_echo_server_pids(blob):
    """解析 ps，返回 NAME 为 echo-server 的 PID 列表。"""
    chunk = _last_ps_table_chunk(blob)
    if not chunk:
        return []
    lines = chunk.splitlines()
    if not lines:
        return []
    out = []
    for line in lines[1:]:
        line = line.strip()
        if not line:
            continue
        if line.startswith(b"root@"):
            break
        parts = line.split()
        if len(parts) >= 6 and parts[-1] == b"echo-server":
            if parts[3] == b"ZOMBIE":
                continue
            try:
                out.append(int(parts[0]))
            except ValueError:
                pass
    return out


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
        # 容器负载较高时 USB 启动链会抖动，放宽 shell 就绪等待窗口。
        if not wait_for_shell_ready(master_fd, buf, 180.0):
            print("echo-server-regress: 未等到 shell 就绪", file=sys.stderr)
            if buf[0]:
                sys.stderr.buffer.write(buf[0][-4000:])
            return 1
        read_some(master_fd, buf, 4.0)
        time.sleep(2.0)

        os.write(master_fd, b"echo-server\n")
        read_some(master_fd, buf, 6.0)

        os.write(master_fd, b"echo-server tcp 127.0.0.1 18080 &\n")
        read_some(master_fd, buf, 6.0)
        time.sleep(3.0)

        os.write(master_fd, b"ps\n")
        read_some(master_fd, buf, 12.0)
        n_es = _count_ps_name_echo_server(buf[0])
        if n_es < 0:
            print("echo-server-regress: ps 输出中未找到表头 NAME", file=sys.stderr)
            sys.stderr.buffer.write(buf[0][-6000:])
            return 1
        if n_es != 1:
            print(
                "echo-server-regress: 仅启动 tcp 监听后，ps 中 echo-server 进程数应为 1，实际 %d"
                % n_es,
                file=sys.stderr,
            )
            sys.stderr.buffer.write(buf[0][-8000:])
            return 1

        os.write(master_fd, b"echo ES_TCP | netcat 127.0.0.1 18080\n")
        read_some(master_fd, buf, 45.0)

        os.write(master_fd, b"echo-server udp 127.0.0.1 18081 &\n")
        read_some(master_fd, buf, 6.0)
        time.sleep(3.0)

        os.write(master_fd, b"udp_line_client 127.0.0.1 18081 UDP_OK\n")
        read_some(master_fd, buf, 45.0)

        # telnet 常发 \r 作为行尾：须能回显（与仅 \n 的 netcat 管道区分）
        os.write(master_fd, b"echo-server tcp 127.0.0.1 18083 &\n")
        read_some(master_fd, buf, 6.0)
        time.sleep(3.0)
        os.write(master_fd, b"telnet 127.0.0.1 18083\n")
        read_some(master_fd, buf, 12.0)
        time.sleep(2.0)
        os.write(master_fd, b"TELCRRET\r")
        read_some(master_fd, buf, 35.0)
        # 同一 telnet 会话内第二次发送：若 relay 在 stdin EOF 上误退出，则不会见到 TEL2ND
        os.write(master_fd, b"TEL2ND\r")
        read_some(master_fd, buf, 30.0)
        os.write(master_fd, b"TEL3RD\r")
        read_some(master_fd, buf, 30.0)
        os.write(master_fd, b"TWNH1\r")
        read_some(master_fd, buf, 18.0)
        os.write(master_fd, b"TWNH2\r")
        read_some(master_fd, buf, 18.0)
        os.write(master_fd, b"nihao\r")
        read_some(master_fd, buf, 16.0)
        os.write(master_fd, b"haha\r")
        read_some(master_fd, buf, 16.0)
        os.write(master_fd, b"cao\r")
        read_some(master_fd, buf, 16.0)
        os.write(master_fd, b"\x03")
        read_some(master_fd, buf, 8.0)
        os.write(master_fd, b"\n")
        read_some(master_fd, buf, 12.0)

        os.write(master_fd, b"ps\n")
        read_some(master_fd, buf, 10.0)
        for pid in _collect_echo_server_pids(buf[0]):
            os.write(master_fd, ("kill %d\n" % pid).encode())
            read_some(master_fd, buf, 6.0)
        os.write(master_fd, b"ps\n")
        read_some(master_fd, buf, 10.0)
        if _count_ps_name_echo_server(buf[0]) != 0:
            print(
                "echo-server-regress: kill 后仍有 echo-server 进程（应能结束阻塞中的 read）",
                file=sys.stderr,
            )
            sys.stderr.buffer.write(buf[0][-8000:])
            return 1

        data = buf[0]
        if b"PANIC cpu" in data or b"PANIC" in data:
            print("echo-server-regress: 检测到内核 PANIC", file=sys.stderr)
            return 1
        if b"zombie!" in data:
            print(
                "echo-server-regress: 串口出现 init 的 zombie!",
                file=sys.stderr,
            )
            sys.stderr.buffer.write(data[-8000:])
            return 1
        if b"usage: echo-server" not in data:
            print(
                "echo-server-regress: 未见到 echo-server 用法行（无参应打印 usage: echo-server …）",
                file=sys.stderr,
            )
            sys.stderr.buffer.write(data[-6000:])
            return 1
        if b"ES_TCP" not in data:
            print(
                "echo-server-regress: 未见到 ES_TCP（TCP 行回显失败）",
                file=sys.stderr,
            )
            sys.stderr.buffer.write(data[-8000:])
            return 1
        if b"UDP_OK" not in data:
            print(
                "echo-server-regress: 未见到 UDP_OK（UDP 回显失败）",
                file=sys.stderr,
            )
            sys.stderr.buffer.write(data[-8000:])
            return 1
        if b"TELCRRET" not in data:
            print(
                "echo-server-regress: 未见到 TELCRRET（telnet \\r 行尾 TCP 回显失败）",
                file=sys.stderr,
            )
            sys.stderr.buffer.write(data[-8000:])
            return 1
        if (
            b"exec TEL2ND failed" in data
            or b"exec TEL3RD failed" in data
            or b"exec TWNH1 failed" in data
            or b"exec TWNH2 failed" in data
            or b"exec nihao failed" in data
            or b"exec haha failed" in data
            or b"exec cao failed" in data
        ):
            print(
                "echo-server-regress: TEL2ND 被 shell 执行（telnet 应仍保持会话）",
                file=sys.stderr,
            )
            sys.stderr.buffer.write(data[-8000:])
            return 1
        if b"TEL2ND" not in data:
            print(
                "echo-server-regress: 未见到 TEL2ND（telnet 应在多轮对端回显后仍保持连接）",
                file=sys.stderr,
            )
            sys.stderr.buffer.write(data[-8000:])
            return 1
        if b"TEL3RD" not in data:
            print(
                "echo-server-regress: 未见到 TEL3RD（第三行回显应即时，非下次键入才出现）",
                file=sys.stderr,
            )
            sys.stderr.buffer.write(data[-8000:])
            return 1
        if b"TWNH1" not in data or b"TWNH2" not in data:
            print(
                "echo-server-regress: 未见到 TWNH1/TWNH2（连续 \\r 行尾回显）",
                file=sys.stderr,
            )
            sys.stderr.buffer.write(data[-8000:])
            return 1
        _j1 = data.find(b"TWNH1")
        _j2 = data.find(b"TWNH2")
        if _j1 < 0 or _j2 < 0 or _j1 >= _j2:
            print(
                "echo-server-regress: TWNH1/TWNH2 串口顺序异常",
                file=sys.stderr,
            )
            sys.stderr.buffer.write(data[-8000:])
            return 1
        if data.count(b"> nihao") < 2 or data.count(b"> haha") < 2 or data.count(b"> cao") < 2:
            print(
                "echo-server-regress: nihao/haha/cao 连续交互显示异常（应各出现输入+回显两次提示）",
                file=sys.stderr,
            )
            sys.stderr.buffer.write(data[-8000:])
            return 1
        if data.count(b"> ") < 4:
            print(
                "echo-server-regress: 串口应多次出现 telnet 提示符 \"> \"（当前计数不足）",
                file=sys.stderr,
            )
            sys.stderr.buffer.write(data[-8000:])
            return 1
        # 不应出现 init 异常退出（进程表等问题）
        if b"init: fork failed" in data or b"init exiting" in data:
            print(
                "echo-server-regress: 检测到 init fork 失败或 init 退出（进程表泄漏？）",
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
        print("echo-server-regress: 缺少 %s" % img, file=sys.stderr)
        return 1

    # 各阶段 read_some 累计可达数分钟；过短会导致 QEMU 在 kill 断言前被 SIGTERM
    qemu_cmd = [
        "timeout",
        "600",
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
            print("echo-server-regress: 通过")
            return 0
        if attempt < 3:
            print(
                "echo-server-regress: 第 %d 次尝试失败，重试…" % (attempt + 1),
                file=sys.stderr,
            )
            time.sleep(2.0)
    return 1


if __name__ == "__main__":
    sys.exit(main())
