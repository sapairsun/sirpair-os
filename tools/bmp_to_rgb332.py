#!/usr/bin/env python3
import struct
import sys


def u16(buf, off):
    return struct.unpack_from("<H", buf, off)[0]


def u32(buf, off):
    return struct.unpack_from("<I", buf, off)[0]


def s32(buf, off):
    return struct.unpack_from("<i", buf, off)[0]


def main():
    if len(sys.argv) not in (3, 5):
        print("usage: bmp_to_rgb332.py <input.bmp> <output.raw> [dst_w dst_h]", file=sys.stderr)
        return 1

    src = sys.argv[1]
    dst = sys.argv[2]
    data = open(src, "rb").read()

    if len(data) < 54 or data[0:2] != b"BM":
        raise SystemExit("invalid bmp file")

    off_bits = u32(data, 10)
    dib_size = u32(data, 14)
    if dib_size < 40:
        raise SystemExit("unsupported dib header")

    width = s32(data, 18)
    height = s32(data, 22)
    planes = u16(data, 26)
    bpp = u16(data, 28)
    comp = u32(data, 30)

    if planes != 1 or comp != 0 or bpp not in (24, 32):
        raise SystemExit("only uncompressed 24/32bpp bmp is supported")

    top_down = height < 0
    src_w = width
    src_h = -height if top_down else height
    if src_w <= 0 or src_h <= 0:
        raise SystemExit("invalid bmp dimensions")

    if len(sys.argv) == 5:
        dst_w = int(sys.argv[3])
        dst_h = int(sys.argv[4])
    else:
        dst_w = 160
        dst_h = 100
    src_stride = ((src_w * bpp + 31) // 32) * 4

    out = bytearray(dst_w * dst_h)
    for y in range(dst_h):
        sy = (y * src_h) // dst_h
        if not top_down:
            sy = src_h - 1 - sy
        row_base = off_bits + sy * src_stride
        for x in range(dst_w):
            sx = (x * src_w) // dst_w
            p = row_base + sx * (bpp // 8)
            b = data[p + 0]
            g = data[p + 1]
            r = data[p + 2]
            out[y * dst_w + x] = (r & 0xE0) | ((g & 0xE0) >> 3) | ((b & 0xC0) >> 6)

    with open(dst, "wb") as f:
        f.write(out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
