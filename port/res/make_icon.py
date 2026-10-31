"""Generates port/res/ngb.ico: the launcher's crest in vermilion on lacquer black.

Original artwork drawn by code (no fonts, images or game data). Stdlib only: a small
supersampling rasteriser renders every size and the ICO is assembled by hand (32-bit BMP
entries with an AND mask up to 128 px, a PNG entry for 256 px, as Windows does since Vista).
The crest (a ring with two straight swords crossed at +-45 degrees) uses the same geometry as
the launcher (launcher/kamon.c); keep both in step.

    python port/res/make_icon.py                    # writes port/res/ngb.ico
    python port/res/make_icon.py --png preview.png  # also a 256 px preview
"""

from __future__ import annotations

import argparse
import math
import struct
import zlib
from pathlib import Path

SIZES = (16, 24, 32, 48, 64, 128, 256)

RGB = tuple[float, float, float]
RGBA = tuple[float, float, float, float]

LACQUER: RGB = (19.0, 17.0, 15.0)
LACQUER_EDGE: RGB = (42.0, 39.0, 36.0)
VERMILION: RGB = (210.0, 63.0, 47.0)

CORNER = 0.21  # corner radius of the square
MARGIN = 0.02
CREST_RADIUS = 0.36  # of the unit canvas


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


def _box(a: float, b: float, a0: float, a1: float, hw: float) -> float:
    return max(abs(b) - hw, max(a0 - a, a - a1))


def _sword(u: float, v: float, s: float, c: float, thick: float) -> float:
    a, b = -u * s + v * c, u * c + v * s
    tip_hw = 0.048 * thick * (a + 0.84) / 0.12
    d = max(abs(b) - tip_hw, max(-0.84 - a, a + 0.72))
    d = min(d, _box(a, b, -0.72, 0.32, 0.048 * thick))
    d = min(d, _box(a, b, 0.32, 0.32 + 0.09 * thick, 0.18))
    return min(d, _box(a, b, 0.32 + 0.09 * thick, 0.75, 0.044 * thick))


def crest_sdf(u: float, v: float, thick: float) -> float:
    """Signed distance to the crest on the unit disc (kamon.c: kamon_coverage)."""
    s45 = math.sqrt(0.5)
    d = abs(math.hypot(u, v) - 0.90) - 0.05 * thick
    return min(d, _sword(u, v, s45, s45, thick), _sword(u, v, -s45, s45, thick))


def sample(x: float, y: float, size: int) -> RGBA:
    """Colour (r, g, b, a) of a point of the unit canvas; a in 0..1, rgb in 0..255."""
    d = _square_sdf(x, y)
    if d > 0:
        return (0.0, 0.0, 0.0, 0.0)
    color = LACQUER
    if d > -0.02:  # a faint lighter edge so the tile reads on dark taskbars
        color = _lerp(color, LACQUER_EDGE, _clamp((d + 0.02) / 0.02))
    thick = 1.9 if size <= 16 else 1.6 if size <= 32 else 1.0
    radius = 0.43 if size <= 24 else CREST_RADIUS  # fill tiny icons
    u, v = (x - 0.5) / radius, (y - 0.5) / radius
    aa = 1.0 / (size * radius)
    cover = _clamp(0.5 - crest_sdf(u, v, thick) / aa)
    if cover > 0:
        color = _lerp(color, VERMILION, cover)
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
