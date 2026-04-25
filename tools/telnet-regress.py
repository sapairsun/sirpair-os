#!/usr/bin/env python3
"""
QEMU 串口回归：验证 telnet 用户程序与网络栈。

步骤：shell 就绪 → 先本机 echo-server + telnet 127.0.0.1（两轮 \\r、重连、^C）
→ 再 dhcp-client → ping → telnet 无参数 → telnet 10.0.2.2:23（expect connect failed）。
本机回环不依赖 DHCP；先测本机可避免前序网络命令与当前栈组合时的干扰。
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
        # 与 echo-server-regress 一致：先读入若干启动串再等待标记，避免 PTY 首包未到即超时
        read_some(master_fd, buf, 4.0)
        time.sleep(2.0)
        if not wait_for_shell_ready(master_fd, buf, 240.0):
            print("telnet-regress: 未等到 shell 就绪", file=sys.stderr)
            return 1
        read_some(master_fd, buf, 4.0)
        time.sleep(2.0)

        os.write(master_fd, b"echo-server\n")
        read_some(master_fd, buf, 6.0)

        # 与 echo-server-regress.py 一致：先 TCP/UDP 预热栈，再本机 telnet；否则仅起 19998 时
        # 在部分环境下易出现「首行回显后会话即断」的假失败（echo-server-regress 已通过同序验证）。
        os.write(master_fd, b"echo-server tcp 127.0.0.1 18080 &\n")
        read_some(master_fd, buf, 6.0)
        time.sleep(3.0)

        os.write(master_fd, b"ps\n")
        read_some(master_fd, buf, 12.0)
        n_es = _count_ps_name_echo_server(buf[0])
        if n_es < 0:
            print("telnet-regress: ps 输出中未找到表头 NAME", file=sys.stderr)
            sys.stderr.buffer.write(buf[0][-6000:])
            return 1
        if n_es != 1:
            print(
                "telnet-regress: 仅启动 tcp 监听后，ps 中 echo-server 进程数应为 1，实际 %d"
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

        # 本机 telnet：与 echo-server-regress.py 已验证段一致（18083 + TELCRRET/TEL2ND），再测重连
        os.write(master_fd, b"echo-server tcp 127.0.0.1 18083 &\n")
        read_some(master_fd, buf, 6.0)
        time.sleep(3.0)
        os.write(master_fd, b"telnet 127.0.0.1 18083\n")
        read_some(master_fd, buf, 12.0)
        time.sleep(2.0)
        os.write(master_fd, b"TELCRRET\r")
        read_some(master_fd, buf, 35.0)
        os.write(master_fd, b"TEL2ND\r")
        read_some(master_fd, buf, 30.0)
        # 第三行及回显：若 fdready(0) 恒真则 read 阻塞、套接字回显延迟到下次键入
        os.write(master_fd, b"TEL3RD\r")
        read_some(master_fd, buf, 30.0)
        # 连续两行 \\r 行尾（与交互式「第一行/第二行」一致），回显顺序须正确、不得粘连错乱
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

        # 第一次会话结束后，监听槽须回到 LISTEN，第二次 connect 才能成功
        os.write(master_fd, b"telnet 127.0.0.1 18083\n")
        read_some(master_fd, buf, 14.0)
        time.sleep(2.0)
        os.write(master_fd, b"TEL_RECONN\r")
        read_some(master_fd, buf, 28.0)
        os.write(master_fd, b"\x03")
        read_some(master_fd, buf, 8.0)
        os.write(master_fd, b"\n")
        read_some(master_fd, buf, 8.0)

        if os.environ.get("TELNET_REGRESS_MINIMAL", "") != "1":
            os.write(master_fd, b"dhcp-client\n")
            read_some(master_fd, buf, 45.0)
            os.write(master_fd, b"ping 10.0.2.2 1\n")
            read_some(master_fd, buf, 25.0)
            # 绑定本机网卡地址的服务须能被 telnet 同一地址接入（软件回环，不经 ARP）
            os.write(master_fd, b"echo-server tcp 10.0.2.15 18090 &\n")
            read_some(master_fd, buf, 10.0)
            time.sleep(2.5)
            os.write(master_fd, b"telnet 10.0.2.15 18090\n")
            read_some(master_fd, buf, 14.0)
            time.sleep(2.0)
            os.write(master_fd, b"OWNIP_OK\r")
            read_some(master_fd, buf, 30.0)
            os.write(master_fd, b"\x03")
            read_some(master_fd, buf, 8.0)
            os.write(master_fd, b"\n")
            read_some(master_fd, buf, 10.0)
            os.write(master_fd, b"telnet\n")
            read_some(master_fd, buf, 8.0)
            if os.environ.get("TELNET_REGRESS_SKIP_FAIL_TELNET", "") != "1":
                os.write(master_fd, b"telnet 10.0.2.2 23\n")
                read_some(master_fd, buf, 35.0)

        data = buf[0]
        _minimal = os.environ.get("TELNET_REGRESS_MINIMAL", "") == "1"
        _skip_fail_telnet = os.environ.get("TELNET_REGRESS_SKIP_FAIL_TELNET", "") == "1"
        _wan_after_local = not _minimal and not _skip_fail_telnet
        if b"PANIC cpu" in data:
            print("telnet-regress: 检测到内核 PANIC", file=sys.stderr)
            return 1
        if b"ES_TCP" not in data or b"UDP_OK" not in data:
            print(
                "telnet-regress: 未见到 ES_TCP/UDP_OK（TCP/UDP 预热失败，后续本机 telnet 不可靠）",
                file=sys.stderr,
            )
            sys.stderr.buffer.write(data[-8000:])
            return 1
        if not _minimal:
            if b"usage: telnet" not in data:
                print("telnet-regress: 未见到 usage: telnet", file=sys.stderr)
                sys.stderr.buffer.write(data[-6000:])
                return 1
            if b"ping 10.0.2.2: tx=" not in data or b"lost=0" not in data:
                print("telnet-regress: ping 10.0.2.2 未成功", file=sys.stderr)
                sys.stderr.buffer.write(data[-6000:])
                return 1
            if b"OWNIP_OK" not in data:
                print(
                    "telnet-regress: 对本机 10.0.2.15 的 telnet 未见到回显 OWNIP_OK",
                    file=sys.stderr,
                )
                sys.stderr.buffer.write(data[-8000:])
                return 1
            if _wan_after_local and b"telnet: connect failed" not in data:
                print("telnet-regress: 未见到 telnet: connect failed（对 10.0.2.2:23 无服务时期望失败）", file=sys.stderr)
                sys.stderr.buffer.write(data[-6000:])
                return 1
        if b"usage: echo-server" not in data:
            print(
                "telnet-regress: 未见到 echo-server 用法行（无参应打印 usage: echo-server …）",
                file=sys.stderr,
            )
            sys.stderr.buffer.write(data[-6000:])
            return 1
        if (
            b"exec TEL2ND failed" in data
            or b"exec TEL_RECONN failed" in data
            or b"exec TEL3RD failed" in data
            or b"exec TWNH1 failed" in data
            or b"exec TWNH2 failed" in data
            or b"exec nihao failed" in data
            or b"exec haha failed" in data
            or b"exec cao failed" in data
        ):
            print(
                "telnet-regress: 本机 telnet 会话已退出，标记被 shell 当命令执行（应仍在 telnet 内回显）",
                file=sys.stderr,
            )
            sys.stderr.buffer.write(data[-8000:])
            return 1
        if (
            b"TELCRRET" not in data
            or b"TEL2ND" not in data
            or b"TEL3RD" not in data
            or b"TWNH1" not in data
            or b"TWNH2" not in data
        ):
            print(
                "telnet-regress: 本机回环 telnet 未见到 TELCRRET/TEL2ND/TEL3RD/TWNH1/TWNH2（多行回显应即时）",
                file=sys.stderr,
            )
            sys.stderr.buffer.write(data[-8000:])
            return 1
        _i1 = data.find(b"TWNH1")
        _i2 = data.find(b"TWNH2")
        if _i1 < 0 or _i2 < 0 or _i1 >= _i2:
            print(
                "telnet-regress: TWNH1/TWNH2 在串口中顺序异常（第二行回显不应错乱到第一行之前）",
                file=sys.stderr,
            )
            sys.stderr.buffer.write(data[-8000:])
            return 1
        if data.count(b"> nihao") < 2 or data.count(b"> haha") < 2 or data.count(b"> cao") < 2:
            print(
                "telnet-regress: nihao/haha/cao 连续交互显示异常（应各出现输入+回显两次提示）",
                file=sys.stderr,
            )
            sys.stderr.buffer.write(data[-8000:])
            return 1
        if b"TEL_RECONN" not in data:
            print(
                "telnet-regress: 未见到 TEL_RECONN（断开后应对同一端口再次 connect 成功）",
                file=sys.stderr,
            )
            sys.stderr.buffer.write(data[-8000:])
            return 1
        if data.count(b"> ") < 4:
            print(
                "telnet-regress: 串口应多次出现 telnet 输入/输出提示符 \"> \"（当前计数不足）",
                file=sys.stderr,
            )
            sys.stderr.buffer.write(data[-8000:])
            return 1
        _max_fail = 1 if _wan_after_local else 0
        if data.count(b"telnet: connect failed") > _max_fail:
            print(
                "telnet-regress: 本机第二次 telnet 不应失败（允许的 connect failed 次数上限为 %d）"
                % _max_fail,
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
        print("telnet-regress: 缺少 %s" % img, file=sys.stderr)
        return 1

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

    for attempt in range(3):
        rc = _run_once(root, img, qemu_cmd)
        if rc == 0:
            print("telnet-regress: 通过")
            return 0
        if attempt < 2:
            print(
                "telnet-regress: 第 %d 次尝试失败，重试…" % (attempt + 1),
                file=sys.stderr,
            )
            time.sleep(2.0)
    return 1


if __name__ == "__main__":
    sys.exit(main())
