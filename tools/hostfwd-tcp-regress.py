#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
验证：模拟器用户态网络下，局域网其它机器无法直连访客私网地址（如 10.0.2.15），
须通过运行模拟器的宿主机做端口转发（QEMU hostfwd）。本脚本在容器/宿主机侧用
本机套接字连接转发端口，等价于「外部经宿主机」访问访客上绑定本机网卡地址的 TCP 服务。

真机双机直连时，两机须在同一三层可达网段且路由/防火墙正确；内核路径与经转发的
模拟器路径一致（对端以太网源地址一般为网关或客户端网卡）。

说明：默认未纳入 docker-build.sh test-full（环境差异大）；需要时在已构建 sirpair-kernel.img
后执行：python3 tools/hostfwd-tcp-regress.py
"""
import os
import socket
import subprocess
import sys
import time

_tools_dir = os.path.dirname(os.path.abspath(__file__))
if _tools_dir not in sys.path:
    sys.path.insert(0, _tools_dir)
from qemu_regress_common import QEMU_SMP, read_some, wait_for_shell_ready

# 与 -netdev user,hostfwd=... 一致：宿主机监听端口 -> 访客 10.0.2.15:服务端口（与常见 8080 场景一致）
HOSTFWD_HOST_PORT = 28080
GUEST_SVC_PORT = 8080


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
        if not wait_for_shell_ready(master_fd, buf, 240.0):
            print("hostfwd-tcp-regress: 未等到 shell 就绪", file=sys.stderr)
            if buf[0]:
                sys.stderr.buffer.write(buf[0][-4000:])
            return 1
        read_some(master_fd, buf, 4.0)
        time.sleep(2.0)

        os.write(master_fd, b"dhcp-client\n")
        read_some(master_fd, buf, 50.0)
        if b"10.0.2.15" not in buf[0]:
            print("hostfwd-tcp-regress: DHCP 未得到 10.0.2.15", file=sys.stderr)
            return 1

        # 与用户场景一致：绑定 DHCP 得到的网卡地址。
        os.write(
            master_fd,
            ("echo-server tcp 10.0.2.15 %d &\n" % GUEST_SVC_PORT).encode("ascii"),
        )
        read_some(master_fd, buf, 12.0)
        time.sleep(4.0)
        os.write(master_fd, b"ps\n")
        read_some(master_fd, buf, 15.0)
        if b"echo-server" not in buf[0]:
            print(
                "hostfwd-tcp-regress: ps 中未见 echo-server（后台服务未启动？）",
                file=sys.stderr,
            )
            sys.stderr.buffer.write(buf[0][-12000:])
            return 1
        time.sleep(4.0)

        sk = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sk.settimeout(35.0)
        try:
            sk.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        except OSError:
            pass
        try:
            sk.connect(("127.0.0.1", HOSTFWD_HOST_PORT))
        except OSError as e:
            print(
                "hostfwd-tcp-regress: 本机连接转发端口失败（检查 QEMU hostfwd）: %s"
                % (e,),
                file=sys.stderr,
            )
            sys.stderr.buffer.write(buf[0][-6000:])
            return 1
        sk.sendall(b"HFWCHK\r\n")
        chunk = b""
        deadline = time.time() + 32.0
        while time.time() < deadline:
            sk.settimeout(2.0)
            try:
                part = sk.recv(512)
            except socket.timeout:
                continue
            if not part:
                break
            chunk += part
            if b"HFWCHK" in chunk:
                break
        sk.close()

        if b"HFWCHK" not in chunk:
            print(
                "hostfwd-tcp-regress: 未收到回显 HFWCHK（经 hostfwd 的 TCP 路径异常）",
                file=sys.stderr,
            )
            sys.stderr.buffer.write(buf[0][-8000:])
            return 1

        data = buf[0]
        if b"PANIC cpu" in data or b"PANIC" in data:
            print("hostfwd-tcp-regress: 检测到内核 PANIC", file=sys.stderr)
            return 1

        print("hostfwd-tcp-regress: 通过")
        return 0
    finally:
        try:
            proc.terminate()
        except OSError:
            pass
        try:
            proc.wait(timeout=8)
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
        print("hostfwd-tcp-regress: 缺少 %s" % img, file=sys.stderr)
        return 1

    # hostfwd：显式绑定宿主机 127.0.0.1，访客地址省略（默认 10.0.2.15），与文档一致。
    qemu_cmd = [
        "timeout",
        "400",
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
        "user,id=net0,hostfwd=tcp::%d-:%d" % (HOSTFWD_HOST_PORT, GUEST_SVC_PORT),
        "-rtc",
        "base=localtime,clock=host",
    ]

    return _run_once(root, img, qemu_cmd)


if __name__ == "__main__":
    sys.exit(main())
