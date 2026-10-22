"""Generates port/res/ngb.ico: a chrome shuriken crossed by a red slash, on black.

Original artwork drawn by code (no fonts, images or game data). Stdlib only: a small
supersampling rasteriser renders every size and the ICO is assembled by hand (32-bit BMP
entries with an AND mask up to 128 px, a PNG entry for 256 px, as Windows does since Vista).
The emblem uses the same geometry as the launcher's artwork (launcher/art.c).

    python port/res/make_icon.py                    # writes port/res/ngb.ico
    python port/res/make_icon.py --png preview.png  # also a 256 px preview
"""

from __future__ import annotations

import argparse
import itertools
import math
import struct
import zlib
from pathlib import Path

SIZES = (16, 24, 32, 48, 64, 128, 256)

Vec = tuple[float, float]
RGB = tuple[float, float, float]
RGBA = tuple[float, float, float, float]
Tri = tuple[Vec, Vec, Vec]

# Palette (0..255)
BLACK: RGB = (5.0, 5.0, 9.0)
RED_GLOW: RGB = (225.0, 22.0, 32.0)
RED_EDGE: RGB = (255.0, 70.0, 70.0)
HOLE: RGB = (6.0, 4.0, 6.0)
HOLE_RING: RGB = (190.0, 24.0, 32.0)

# Facet colours as (tip, inner vertex, centre): lit and shadowed side of every blade.
LIT: tuple[RGB, RGB, RGB] = ((246, 249, 253), (176, 184, 200), (24, 26, 34))
SHADE: tuple[RGB, RGB, RGB] = ((196, 204, 216), (52, 56, 70), (8, 8, 12))

TIP_RADIUS = 0.40  # shuriken tips (the canvas is 1 x 1)
CORNER = 0.21  # corner radius of the square
MARGIN = 0.02
RIM = 0.03  # width of the chrome rim


def _facets() -> list[tuple[Tri, bool]]:
    """The 8 triangles (centre, tip, inner vertex) of the star; True for the lit side."""
    out: list[tuple[Tri, bool]] = []
    for k in range(4):
        tip = math.radians(45 + 90 * k)
        tip_pt = (0.5 + TIP_RADIUS * math.cos(tip), 0.5 - TIP_RADIUS * math.sin(tip))
        for side in (1, -1):
            ang = tip + (0.80 if side > 0 else -0.72)
            ir = TIP_RADIUS * (0.30 if side > 0 else 0.40)
            inner = (0.5 + ir * math.cos(ang), 0.5 - ir * math.sin(ang))
            out.append((((0.5, 0.5), tip_pt, inner), side > 0))
    return out


FACETS = _facets()


def _lerp(a: RGB, b: RGB, t: float) -> RGB:
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t)


def _clamp(v: float, lo: float = 0.0, hi: float = 1.0) -> float:
    return lo if v < lo else hi if v > hi else v


def _square_sdf(x: float, y: float) -> float:
    """Signed distance (negative inside) to the rounded square."""
    half = 0.5 - MARGIN
    qx = abs(x - 0.5) - (half - CORNER)
    qy = abs(y - 0.5) - (half - CORNER)
    return math.hypot(max(qx, 0.0), max(qy, 0.0)) + min(max(qx, qy), 0.0) - CORNER


def _bary(p: Vec, tri: Tri) -> tuple[float, float, float] | None:
    """Barycentric weights of p in the triangle, or None when p is outside."""
    (ax, ay), (bx, by), (cx, cy) = tri
    den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
    if abs(den) < 1e-12:
        return None
    w0 = ((by - cy) * (p[0] - cx) + (cx - bx) * (p[1] - cy)) / den
    w1 = ((cy - ay) * (p[0] - cx) + (ax - cx) * (p[1] - cy)) / den
    w2 = 1.0 - w0 - w1
    if w0 < 0 or w1 < 0 or w2 < 0:
        return None
    return w0, w1, w2


def _chrome(t: float) -> RGB:
    """Vertical brushed-chrome ramp: bright sky, dark horizon, bright ground."""
    stops = (
        (0.00, (252.0, 253.0, 255.0)),
        (0.26, (178.0, 186.0, 200.0)),
        (0.45, (92.0, 99.0, 114.0)),
        (0.50, (18.0, 20.0, 28.0)),
        (0.53, (140.0, 150.0, 168.0)),
        (0.78, (214.0, 220.0, 230.0)),
        (1.00, (120.0, 128.0, 144.0)),
    )
    t = _clamp(t)
    for (t0, c0), (t1, c1) in itertools.pairwise(stops):
        if t <= t1:
            return _lerp(c0, c1, (t - t0) / (t1 - t0))
    return stops[-1][1]


def _add(base: RGB, extra: RGB, k: float) -> RGB:
    return (
        min(255.0, base[0] + extra[0] * k),
        min(255.0, base[1] + extra[1] * k),
        min(255.0, base[2] + extra[2] * k),
    )


def sample(x: float, y: float, size: int) -> RGBA:
    """Colour (r, g, b, a) of a point of the unit canvas; a in 0..1, rgb in 0..255."""
    d = _square_sdf(x, y)
    if d > 0:
        return (0.0, 0.0, 0.0, 0.0)
    # Black background with a deep red mist behind the emblem.
    r = math.hypot(x - 0.5, y - 0.5)
    mist = max(0.0, 1.0 - r / 0.62) ** 2
    color = _add(BLACK, RED_GLOW, 0.85 * mist)
    # Horizontal speed lines (they only show from 48 px up).
    if size >= 48:
        for ly, half, strength in ((0.30, 0.004, 0.30), (0.69, 0.006, 0.25), (0.80, 0.003, 0.2)):
            edge = max(0.0, 1.0 - abs(y - ly) / half)
            fade = math.sin(math.pi * _clamp((x - 0.02) / 0.96)) ** 0.8
            color = _add(color, RED_GLOW, edge * strength * fade)
    # Slash behind the star: thin at both ends, white-hot core, red halo.
    ax, ay, bx, by = -0.12, 0.84, 1.12, 0.16
    dx, dy = bx - ax, by - ay
    t = _clamp(((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy))
    dist = math.hypot(x - (ax + dx * t), y - (ay + dy * t))
    width = 0.5 * 0.036 * math.sin(math.pi * t) ** 0.55
    halo = math.exp(-((dist / 0.05) ** 2)) * math.sin(math.pi * t) ** 0.5
    color = _add(color, RED_GLOW, 0.55 * halo)
    # Facets of the shuriken, lit like chrome.
    for tri, lit in FACETS:
        w = _bary((x, y), tri)
        if w is None:
            continue
        tip, inner, centre = LIT if lit else SHADE
        color = (
            centre[0] * w[0] + tip[0] * w[1] + inner[0] * w[2],
            centre[1] * w[0] + tip[1] * w[1] + inner[1] * w[2],
            centre[2] * w[0] + tip[2] * w[1] + inner[2] * w[2],
        )
        # thin red edge along the blade outline (the side opposite the centre vertex)
        if w[0] < 0.03:
            color = _lerp(color, RED_EDGE, 0.7)
        break
    # The slash cuts across the star.
    cover = _clamp(0.5 + (width - dist) * size) if width > 0 else 0.0
    if cover > 0:
        hot = _clamp(1.0 - dist / (width + 1e-6))
        core = _lerp((255.0, 50.0, 50.0), (255.0, 250.0, 240.0), hot**0.6)
        color = _lerp(color, core, cover)
    # Centre hole with a red ring.
    if r < 0.060:
        color = HOLE if r < 0.040 else _lerp(HOLE_RING, RED_EDGE, 0.5)
    # Chrome rim around the square.
    if d > -RIM:
        k = _clamp((d + RIM) / RIM)
        rim = _chrome(y)
        color = _lerp(color, rim, 0.9 if k > 0.25 else 0.9 * k / 0.25)
        if d > -0.006:
            color = _lerp(color, (10.0, 6.0, 8.0), 0.6)
    return (color[0], color[1], color[2], 1.0)


def render(size: int) -> list[tuple[int, int, int, int]]:
    """RGBA pixels (top row first) with an NxN supersample (alpha-weighted average)."""
    ss = 4 if size <= 64 else 3
    pixels: list[tuple[int, int, int, int]] = []
    for py in range(size):
        for px in range(size):
            acc = [0.0, 0.0, 0.0, 0.0]
            for sy in range(ss):
                for sx in range(ss):
                    u = (px + (sx + 0.5) / ss) / size
                    v = (py + (sy + 0.5) / ss) / size
                    r, g, b, a = sample(u, v, size)
                    acc[0] += r * a
                    acc[1] += g * a
                    acc[2] += b * a
                    acc[3] += a
            alpha = acc[3] / (ss * ss)
            if acc[3] > 0:
                pixels.append(
                    (
                        round(acc[0] / acc[3]),
                        round(acc[1] / acc[3]),
                        round(acc[2] / acc[3]),
                        round(alpha * 255),
                    )
                )
            else:
                pixels.append((0, 0, 0, 0))
    return pixels


def encode_png(size: int, pixels: list[tuple[int, int, int, int]]) -> bytes:
    raw = bytearray()
    for y in range(size):
        raw.append(0)  # filter "none"
        for r, g, b, a in pixels[y * size : (y + 1) * size]:
            raw += bytes((r, g, b, a))

    def chunk(tag: bytes, data: bytes) -> bytes:
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    header = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", header)
        + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + chunk(b"IEND", b"")
    )


def encode_bmp_entry(size: int, pixels: list[tuple[int, int, int, int]]) -> bytes:
    """An ICO DIB: header with doubled height, BGRA pixels bottom-up and an AND mask."""
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    body = bytearray()
    for y in range(size - 1, -1, -1):
        for r, g, b, a in pixels[y * size : (y + 1) * size]:
            body += bytes((b, g, r, a))
    mask_row = ((size + 31) // 32) * 4
    return header + bytes(body) + bytes(mask_row * size)  # AND mask 0: the alpha channel rules


def build_ico() -> tuple[bytes, bytes]:
    entries: list[tuple[int, bytes]] = []
    preview = b""
    for size in SIZES:
        pixels = render(size)
        if size == 256:
            data = encode_png(size, pixels)
            preview = data
        else:
            data = encode_bmp_entry(size, pixels)
        entries.append((size, data))
    out = bytearray(struct.pack("<HHH", 0, 1, len(entries)))
    offset = 6 + 16 * len(entries)
    for size, data in entries:
        out += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    for _, data in entries:
        out += data
    return bytes(out), preview


def main() -> None:
    parser = argparse.ArgumentParser(description=(__doc__ or "").splitlines()[0])
    parser.add_argument("--out", type=Path, default=Path(__file__).with_name("ngb.ico"))
    parser.add_argument("--png", type=Path, help="256 px PNG preview (keep it out of the repo)")
    args = parser.parse_args()
    ico, preview = build_ico()
    args.out.write_bytes(ico)
    print(f"{args.out}: {len(ico)} bytes, sizes {SIZES}")
    if args.png:
        args.png.write_bytes(preview)
        print(f"{args.png}: preview")


if __name__ == "__main__":
    main()
