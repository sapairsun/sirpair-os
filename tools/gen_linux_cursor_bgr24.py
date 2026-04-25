#!/usr/bin/env python3
# 从 GNOME Adwaita 默认左指针（Ubuntu 包 adwaita-icon-theme-full 中的 left_ptr Xcursor）
# 解析为 24×24 BGR 与掩码，写入 user/desktop_cursor_linux.inc。
# 依赖：项目内 tools/adwaita_left_ptr.xcursor（由 deb 提取，见下方说明）。
#
# 更新光标数据：确保 tools/adwaita_left_ptr.xcursor 存在后执行：
#   python3 tools/gen_linux_cursor_bgr24.py > user/desktop_cursor_linux.inc
import os
import struct
import sys

# 与桌面 COL_DESK_BG 一致，用于半透明边缘与背景混合
BG_R, BG_G, BG_B = 0x70, 0x78, 0x90

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)
XCURSOR_PATH = os.path.join(REPO_ROOT, "tools", "adwaita_left_ptr.xcursor")


def blend(a, c, bg):
    return (c * a + bg * (255 - a) + 127) // 255


def parse_xcursor_find_size(path, want):
    """返回 (w,h,xhot,yhot, pixels ARGB list of bytes per pixel as uint32)"""
    with open(path, "rb") as f:
        data = f.read()
    if data[0:4] != b"Xcur":
        raise SystemExit("不是 Xcursor 文件")
    _hsize, _ver, ntoc = struct.unpack("<III", data[4:16])
    best = None
    for i in range(ntoc):
        t, sub, pos = struct.unpack("<III", data[16 + i * 12 : 16 + i * 12 + 12])
        if t != 0xFFFD0002:
            continue
        if sub != want:
            continue
        clen, ctype, csub, cver = struct.unpack("<IIII", data[pos : pos + 16])
        if ctype != 0xFFFD0002:
            continue
        w, h, xh, yh, delay = struct.unpack("<IIIII", data[pos + 16 : pos + 36])
        if w != want or h != want:
            continue
        pix_off = pos + 36
        npix = w * h
        pixels = []
        for j in range(npix):
            o = pix_off + j * 4
            u = struct.unpack("<I", data[o : o + 4])[0]
            pixels.append(u)
        best = (w, h, xh, yh, pixels)
        break
    if best is None:
        raise SystemExit("未找到 %d×%d 图像块" % (want, want))
    return best


def main():
    if not os.path.isfile(XCURSOR_PATH):
        raise SystemExit("缺少 %s（请从 adwaita-icon-theme-full 提取 left_ptr）" % XCURSOR_PATH)

    w, h, xhot, yhot, pixels = parse_xcursor_find_size(XCURSOR_PATH, 24)
    mask = []
    pix_bgr = []
    for u in pixels:
        a = (u >> 24) & 0xFF
        r = (u >> 16) & 0xFF
        g = (u >> 8) & 0xFF
        b = u & 0xFF
        if a == 0:
            mask.append(0)
            pix_bgr.extend((0, 0, 0))
            continue
        br = blend(a, r, BG_R)
        bg = blend(a, g, BG_G)
        bb = blend(a, b, BG_B)
        mask.append(1)
        pix_bgr.extend((bb & 0xFF, bg & 0xFF, br & 0xFF))

    assert len(mask) == w * h
    assert len(pix_bgr) == w * h * 3

    out = []
    out.append(
        "/* Adwaita 默认左指针 24×24（BGR + 掩码），由 tools/gen_linux_cursor_bgr24.py 从 Xcursor 生成 */\n"
    )
    out.append("#define CURSOR_HOT_X %d\n" % xhot)
    out.append("#define CURSOR_HOT_Y %d\n" % yhot)
    out.append("static const uchar g_cursor_mask[%d] = {\n" % (w * h))
    for i in range(0, len(mask), 16):
        chunk = mask[i : i + 16]
        out.append("  " + ",".join("%d" % v for v in chunk) + ",\n")
    out.append("};\n")
    out.append("static const uchar g_cursor_bgr[%d] = {\n" % (w * h * 3))
    for i in range(0, len(pix_bgr), 16):
        chunk = pix_bgr[i : i + 16]
        out.append("  " + ",".join("0x%02x" % v for v in chunk) + ",\n")
    out.append("};\n")
    sys.stdout.write("".join(out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
