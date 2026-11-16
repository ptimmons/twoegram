#!/usr/bin/env python3
"""render.py -- draw saved screens with the Apple's own glyphs.

    render.py <charset.png> <scene.scr>... <outdir>

charset.png is Virtual ]['s screenshot of CHARSET.SYSTEM (tools/charset):
every screen code, 64 to a row on rows 0, 2, 4 and 6 from column 8. Each
character cell of a 560 x 384 screenshot is 7 x 16 pixels, so each code's
glyph is cut out of it as it is, and a scene (24 x 80 screen codes, from
tests/host/shots.c) is drawn by putting those cells together: the result
matches the emulator's own screenshots pixel for pixel.

Plain Python, no imaging library: PNG is read and written with zlib.
"""
import struct
import sys
import zlib
from pathlib import Path

CW, CH, COLS, ROWS = 7, 16, 80, 24


def read_png(path):
    data = Path(path).read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    pos, idat, w = 8, b"", 0
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            assert depth == 8 and ctype in (2, 6) and not interlace, "only 8-bit RGB(A), not interlaced"
            bpp = 4 if ctype == 6 else 3
        elif kind == b"IDAT":
            idat += body
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev, i = [], bytearray(stride), 0
    for _ in range(h):
        f, line = raw[i], bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        if bpp == 3:  # to RGBA
            line = bytearray(b"".join(bytes(line[j:j + 3]) + b"\xff" for j in range(0, stride, 3)))
        rows.append(line)
        prev = rows[-1] if bpp == 4 else bytearray(raw[i - stride:i])
    return w, h, rows


def write_png(path, w, h, rows):
    raw = b"".join(b"\x00" + bytes(r) for r in rows)

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    Path(path).write_bytes(b"\x89PNG\r\n\x1a\n"
                           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw, 9))
                           + chunk(b"IEND", b""))


def glyphs(charset):
    w, h, rows = read_png(charset)
    assert (w, h) == (COLS * CW, ROWS * CH), f"expected a 560 x 384 screenshot, not {w} x {h}"
    out = []
    for code in range(256):
        row, col = (code // 64) * 2, 8 + code % 64
        x0, y0 = col * CW * 4, row * CH
        out.append([bytes(rows[y0 + y][x0:x0 + CW * 4]) for y in range(CH)])
    return out


def render(g, scr, png):
    codes = Path(scr).read_bytes()
    assert len(codes) == ROWS * COLS, f"{scr}: not 24 x 80 screen codes"
    rows = []
    for r in range(ROWS):
        line = codes[r * COLS:(r + 1) * COLS]
        for y in range(CH):
            rows.append(b"".join(g[c][y] for c in line))
    write_png(png, COLS * CW, ROWS * CH, rows)


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    g = glyphs(sys.argv[1])
    out = Path(sys.argv[-1])
    out.mkdir(parents=True, exist_ok=True)
    for scr in sys.argv[2:-1]:
        png = out / (Path(scr).stem + ".png")
        render(g, scr, png)
        print(f"  {png}")


if __name__ == "__main__":
    main()
