"""Prototype of the launcher's procedural ink-wash night landscape (stdlib only).

Reference prototype for port/launcher/ink.c (never used at runtime; kept so the C port can be compared).

Technique (from the inkwash research): ridged fBm silhouettes per depth band, dark crest ink
fading downward into mist, air perspective toward the sky tone, horizontal mist bands, a low
frequency macro mask that leaves blank areas, boundary noise, paper fibre and a vignette.
Writes a PNG. Usage: python inkland.py OUT.png [W H SEED]
"""

import math
import struct
import sys
import zlib

W = int(sys.argv[2]) if len(sys.argv) > 2 else 1240
H = int(sys.argv[3]) if len(sys.argv) > 3 else 700
SEED = int(sys.argv[4]) if len(sys.argv) > 4 else 7


def mulberry32(a):
    def rnd():
        nonlocal a
        a = (a + 0x6D2B79F5) & 0xFFFFFFFF
        t = a
        t = ((t ^ (t >> 15)) * (t | 1)) & 0xFFFFFFFF
        t ^= (t + (((t ^ (t >> 7)) * (t | 61)) & 0xFFFFFFFF)) & 0xFFFFFFFF
        return ((t ^ (t >> 14)) & 0xFFFFFFFF) / 4294967296.0

    return rnd


rnd = mulberry32(SEED)
PERM = [rnd() for _ in range(1024)]


def hash2(ix, iy):
    return PERM[(ix * 73 + iy * 151 + (ix * iy) % 977) & 1023]


def smooth(t):
    return t * t * (3 - 2 * t)


def vnoise1(x, salt):
    i = math.floor(x)
    f = smooth(x - i)
    a = PERM[(i * 31 + salt) & 1023]
    b = PERM[((i + 1) * 31 + salt) & 1023]
    return a + (b - a) * f


def vnoise2(x, y):
    ix, iy = math.floor(x), math.floor(y)
    fx, fy = smooth(x - ix), smooth(y - iy)
    a, b = hash2(ix, iy), hash2(ix + 1, iy)
    c, d = hash2(ix, iy + 1), hash2(ix + 1, iy + 1)
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy


def ridged1(x, salt, octaves=5):
    s, amp, f, norm = 0.0, 1.0, 1.0, 0.0
    for o in range(octaves):
        n = vnoise1(x * f, salt + o * 97)
        r = 1.0 - abs(2.0 * n - 1.0)
        s += amp * r * r
        norm += amp
        amp *= 0.5
        f *= 2.02
    return s / norm


def fbm2(x, y, octaves=4):
    s, amp, f, norm = 0.0, 1.0, 1.0, 0.0
    for _ in range(octaves):
        s += amp * vnoise2(x * f, y * f)
        norm += amp
        amp *= 0.5
        f *= 2.02
    return s / norm


def mix(a, b, t):
    return a + (b - a) * t


# Palette (linear-ish 0..255 per channel), warm lacquer night.
BG = (19, 17, 15)
SKY_HI = (40, 36, 32)
MIST = (54, 49, 43)

# Bands far -> near: base y (fraction of H), height scale, roughness salt, crest tone, fade px
BANDS = [
    (0.50, 0.20, 11, (34, 31, 28), 70.0),
    (0.59, 0.18, 23, (26, 23, 21), 60.0),
    (0.70, 0.16, 37, (18, 16, 14), 55.0),
    (0.84, 0.12, 41, (11, 10, 9), 45.0),
]

# Silhouettes: ridged fBm, then two smoothing passes.
sil = []
for base, hs, salt, _, _ in BANDS:
    xs = [
        H * base - H * hs * (0.35 + 0.95 * x / W) * (ridged1(x / 340.0, salt) * 1.7 - 0.3)
        for x in range(W)
    ]
    for _ in range(2):
        xs = [(xs[max(i - 1, 0)] + 2 * xs[i] + xs[min(i + 1, W - 1)]) / 4 for i in range(W)]
    sil.append(xs)

MOON = (W * 0.76, H * 0.25, H * 0.062)
rows = []
for y in range(H):
    row = bytearray([0])
    for x in range(W):
        # Sky: luminous band in the middle, darker top and bottom; moon halo.
        t = y / H
        sky = 1.0 - min(1.0, abs(t - 0.48) * 2.1)
        r, g, b = (mix(BG[i], SKY_HI[i], sky * 0.9) for i in range(3))
        dx, dy = x - MOON[0], y - MOON[1]
        dist = math.sqrt(dx * dx + dy * dy)
        halo = max(0.0, 1.0 - dist / (MOON[2] * 5.5)) ** 2 * 0.35
        r, g, b = r + halo * 30, g + halo * 27, b + halo * 22
        if dist < MOON[2] + 1.5:
            cov = min(1.0, MOON[2] + 1.5 - dist)
            tex = 0.92 + 0.08 * fbm2(x / 18.0, y / 18.0, 3)
            r, g, b = (
                mix(r, 150 * tex, cov * 0.32),
                mix(g, 138 * tex, cov * 0.32),
                mix(b, 120 * tex, cov * 0.32),
            )

        # Bands, far to near; each overwrites with its own ink.
        for (base, hs, salt, crest, fade), s in zip(BANDS, sil):
            edge = s[x] + (vnoise2(x / 3.0, y / 3.0) - 0.5) * 2.2  # boundary noise
            if y < edge - 1:
                continue
            cov = min(1.0, y - edge + 1)
            depth = max(0.0, y - s[x])
            ink = math.exp(-depth / fade)
            # Wash texture and the macro mask that opens blank areas.
            tex = 0.75 + 0.5 * fbm2(x / 70.0 + salt, y / 150.0, 4)
            macro = smooth(
                min(1.0, max(0.0, fbm2(x / 420.0 + salt * 3, y / 300.0, 2) * 1.8 - 0.35))
            )
            k = min(1.0, ink * tex * (0.55 + 0.45 * macro))
            cr, cg, cb = (mix(MIST[i], crest[i], k) for i in range(3))
            r, g, b = mix(r, cr, cov), mix(g, cg, cov), mix(b, cb, cov)

        # Horizontal mist bands that cut the ranges apart.
        mband = (
            max(0.0, 1.0 - abs(t - 0.66) / 0.05) * 0.22
            + max(0.0, 1.0 - abs(t - 0.79) / 0.04) * 0.16
        )
        mband *= 0.6 + 0.4 * vnoise2(x / 160.0, 3.0)
        r, g, b = mix(r, MIST[0], mband), mix(g, MIST[1], mband), mix(b, MIST[2], mband)

        # Paper fibre: fine grain plus stretched horizontal fibres; vignette.
        fib = (hash2(x, y) - 0.5) * 3.0 + (vnoise2(x / 40.0, y * 1.3) - 0.5) * 2.0
        vx, vy = (x / W - 0.5) * 1.6, (y / H - 0.5) * 1.6
        vig = 1.0 - 0.28 * (vx * vx + vy * vy)
        for v in (r, g, b):
            row.append(max(0, min(255, int((v + fib) * vig))))
    rows.append(bytes(row))


def png(path, w, h, rows):
    raw = b"".join(rows)

    def chunk(tag, data):
        return (
            struct.pack(">I", len(data))
            + tag
            + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
        )

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


png(sys.argv[1], W, H, rows)
