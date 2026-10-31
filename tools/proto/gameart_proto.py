"""Prototype of the launcher's game-art grading and hero composition (stdlib only).

Reference prototype for port/launcher/gameart.c (never used at runtime; kept so the C port can be compared).

Reads exported PNGs of the user's own copy, grades them into the lacquer palette and writes the
hero layer (1240x700) plus the logo masks tinted cream. Usage:
    python -I gameart_proto.py KEYART.png LOGO.png BLACK.png OUTDIR
"""
import math
import struct
import sys
import zlib


def read_png(path):
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    pos, idat, w = 8, b"", 0
    while pos < len(data):
        n, tag = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if tag == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
            assert depth == 8 and ctype in (2, 6)
            bpp = 3 if ctype == 2 else 4
        elif tag == b"IDAT":
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = w * bpp
    out, prev = [], bytearray(stride)
    i = 0
    for _ in range(h):
        f = raw[i]
        line = bytearray(raw[i + 1:i + 1 + stride])
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
        out.append(line)
        prev = line
    px = [[tuple(r[x * bpp:x * bpp + bpp]) + ((255,) if bpp == 3 else ()) for x in range(w)] for r in out]
    return w, h, px


def write_png(path, w, h, rows, alpha=False):
    def chunk(tag, d):
        return struct.pack(">I", len(d)) + tag + d + struct.pack(">I", zlib.crc32(tag + d) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + bytes(r) for r in rows)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6 if alpha else 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


BG = (19, 17, 15)
CREAM = (239, 231, 214)
VERM = (210, 63, 47)


def smoothstep(e0, e1, x):
    t = max(0.0, min(1.0, (x - e0) / (e1 - e0)))
    return t * t * (3 - 2 * t)


def grade(r, g, b):
    """Lacquer grade: warm duotone from bg to cream, a little chroma kept, reds toward vermilion."""
    r, g, b = r / 255, g / 255, b / 255
    lum = 0.299 * r + 0.587 * g + 0.114 * b
    t = smoothstep(0.04, 0.92, lum) ** 1.15  # crush blacks, soft shoulder
    out = [BG[i] / 255 + (CREAM[i] / 255 * 0.86 - BG[i] / 255) * t for i in range(3)]
    k = 0.14
    out = [out[0] + k * (r - lum), out[1] + k * (g - lum), out[2] + k * (b - lum)]
    red = max(0.0, min(1.0, (r - max(g, b) - 0.14) * 3.0))
    out = [out[i] + (VERM[i] / 255 * (0.4 + 0.6 * t) - out[i]) * red * 0.8 for i in range(3)]
    return [max(0, min(255, int(v * 255 + 0.5))) for v in out]


def sample(px, w, h, x, y):
    """Bilinear sample, clamped."""
    x = max(0.0, min(w - 1.001, x))
    y = max(0.0, min(h - 1.001, y))
    x0, y0 = int(x), int(y)
    fx, fy = x - x0, y - y0
    a, b, c, d = px[y0][x0], px[y0][x0 + 1], px[y0 + 1][x0], px[y0 + 1][x0 + 1]
    return [(a[i] * (1 - fx) + b[i] * fx) * (1 - fy) + (c[i] * (1 - fx) + d[i] * fx) * fy for i in range(4)]


def hero(key_png, out_png, W=1240, H=700):
    w, h, px = read_png(key_png)
    # Used crop of demo.spr sprite 0: (74,0)-(1014,480). Scale to 660 px tall, subject at x~870.
    cx0, cx1, cy1 = 74, 1014, 480
    s = 660 / cy1
    ox = 870 - (560 - cx0) * s
    oy = 40
    rows = []
    for y in range(H):
        row = bytearray()
        for x in range(W):
            sx = (x - ox) / s + cx0
            sy = (y - oy) / s
            if cx0 <= sx < cx1 and 0 <= sy < cy1:
                r, g, b, _ = sample(px, w, h, sx, sy)
                c = grade(r, g, b)
            else:
                c = list(BG)
            # Left darkening for the title, top and bottom fades into the bars.
            fl = smoothstep(0.30, 0.66, x / W)
            fe = smoothstep(cx0 * s + ox - 1, cx0 * s + ox + 160, x)  # soften the crop's left edge
            ft = smoothstep(0.0, 0.16, y / H) * (1 - 0.65 * smoothstep(0.80, 1.0, y / H))
            k = fl * fe * ft
            row += bytes(int(BG[i] + (c[i] - BG[i]) * k) for i in range(3))
        rows.append(row)
    write_png(out_png, W, H, rows)


def logo(png, out_png, color=CREAM):
    w, h, px = read_png(png)
    rows = []
    for y in range(h):
        row = bytearray()
        for x in range(w):
            r, g, b, a = px[y][x]
            # Keep the letterforms, drop the red glow: alpha curve plus a luminance gate.
            lum = (0.299 * r + 0.587 * g + 0.114 * b) / 255
            redglow = max(0.0, (r - g) / 255)
            m = smoothstep(0.55, 0.9, a / 255) * (1 - smoothstep(0.25, 0.6, redglow) * (1 - lum))
            row += bytes(color) + bytes([int(255 * m)])
        rows.append(row)
    write_png(out_png, w, h, rows, alpha=True)


if __name__ == "__main__":
    key, ng, black, out = sys.argv[1:5]
    hero(key, out + "/hero-ryu.png")
    logo(ng, out + "/logo-ng.png")
    logo(black, out + "/logo-black.png", VERM)
