#!/usr/bin/env python3
# 生成与常见 Windows 默认指针相近的 32×32 箭头（白填充、深灰描边、透明底），
# 输出为 C 数组，供 user/desktop.c 嵌入。热点为左上角 (0,0) 即箭尖。
# 修改后请运行本脚本，将输出替换进 user/desktop.c 中对应 static 数组与 draw_cursor。
import math
import sys

W = H = 32


def rgb332(r, g, b):
    return ((r & 0xE0) | ((g & 0xE0) >> 3) | ((b & 0xC0) >> 6)) & 0xFF


def inside_arrow(px, py):
    """箭身：直角三角形 dx,dy>=0, dx+dy<=22；箭杆：接在三角形底边。"""
    dx, dy = float(px), float(py)
    if dx < 0 or dy < 0:
        return False
    if dx + dy <= 22:
        return True
    if 23 <= dy <= 30 and 0 <= dx <= 4:
        return True
    return False


def dist_to_edge(px, py):
    """到形状边界的近似符号距离（用于描边）。"""
    if not inside_arrow(px, py):
        return -1
    best = 99.0
    for ox in (-1, 0, 1):
        for oy in (-1, 0, 1):
            if ox == 0 and oy == 0:
                continue
            if not inside_arrow(px + ox, py + oy):
                best = min(best, 1.0)
    if best < 99:
        return 0
    for ox in (-2, -1, 0, 1, 2):
        for oy in (-2, -1, 0, 1, 2):
            if ox == 0 and oy == 0:
                continue
            if not inside_arrow(px + ox, py + oy):
                best = min(best, math.hypot(ox, oy))
    return 1


def main():
    mask = []
    pix = []
    for y in range(H):
        for x in range(W):
            if not inside_arrow(x, y):
                mask.append(0)
                pix.append(0)
                continue
            edge = False
            for ox, oy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                if not inside_arrow(x + ox, y + oy):
                    edge = True
                    break
            mask.append(1)
            if edge:
                pix.append(rgb332(0x30, 0x30, 0x30))
            else:
                pix.append(rgb332(0xF8, 0xF8, 0xF8))
    line = "static const uchar g_cursor_mask[%d] = {\n" % (W * H)
    for i in range(0, len(mask), 16):
        chunk = mask[i : i + 16]
        line += "  " + ",".join("%d" % v for v in chunk) + ",\n"
    line += "};\n"
    line += "static const uchar g_cursor_rgb332[%d] = {\n" % (W * H)
    for i in range(0, len(pix), 16):
        chunk = pix[i : i + 16]
        line += "  " + ",".join("0x%02x" % v for v in chunk) + ",\n"
    line += "};\n"
    sys.stdout.write(line)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
