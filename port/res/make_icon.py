"""Generates port/res/ngb.ico: the launcher's emblem.

A brush dragon (龍) in cream with a vermilion seal, on lacquer black.

The dragon is the glyph of Yuji Boku (SIL OFL 1.1, port/res/fonts), drawn by the launcher itself so
that icon and window match: build the launcher, export the glyph, then run this script.

    build/port/ngb-launcher.exe --emblem-mask build/emblem-mask.bmp
    python port/res/make_icon.py                    # reads it, writes port/res/ngb.ico
    python port/res/make_icon.py --png preview.png  # also a 256 px preview

Stdlib only: the ICO is assembled by hand (32-bit BMP entries with an AND mask up to 128 px, a PNG
entry for 256 px, as Windows does since Vista).
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
CREAM: RGB = (239.0, 231.0, 214.0)
GLYPH_BOX = (0.10, 0.08, 0.80)  # x, y, side of the emblem square inside the tile (unit canvas)
SEAL = 0.14  # side of the seal, relative to the emblem square (launcher: emblem_draw)

_MASK: dict[str, object] = {"size": 0, "cov": []}  # set by load_mask: size, coverage 0..1


def load_mask(path: Path) -> None:
    """Reads the 32-bit BMP that `ngb-launcher.exe --emblem-mask` writes (white glyph on black)."""
    data = path.read_bytes()
    off = struct.unpack_from("<I", data, 10)[0]
    w, h = struct.unpack_from("<ii", data, 18)
    if w not in (-h, h):
        raise SystemExit(f"{path}: expected a square mask")
    _MASK["size"] = w
    _MASK["cov"] = [data[off + i * 4 + 2] / 255.0 for i in range(w * w)]


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


def _glyph(u: float, v: float) -> float:
    """Bilinear coverage of the dragon at (u, v) of the emblem square (0..1)."""
    n = int(_MASK["size"])  # type: ignore[call-overload]
    cov: list[float] = _MASK["cov"]  # type: ignore[assignment]
    x, y = u * n - 0.5, v * n - 0.5
    if x < 0 or y < 0 or x >= n - 1 or y >= n - 1:
        return 0.0
    x0, y0 = int(x), int(y)
    fx, fy = x - x0, y - y0
    a, b = cov[y0 * n + x0], cov[y0 * n + x0 + 1]
    c, d = cov[(y0 + 1) * n + x0], cov[(y0 + 1) * n + x0 + 1]
    return (a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy


def sample(x: float, y: float, size: int) -> RGBA:
    """Colour (r, g, b, a) of a point of the unit canvas; a in 0..1, rgb in 0..255."""
    d = _square_sdf(x, y)
    if d > 0:
        return (0.0, 0.0, 0.0, 0.0)
    color = LACQUER
    if d > -0.02:  # a faint lighter edge so the tile reads on dark taskbars
        color = _lerp(color, LACQUER_EDGE, _clamp((d + 0.02) / 0.02))
    bx, by, side = GLYPH_BOX
    u, v = (x - bx) / side, (y - by) / side
    seal = SEAL if size > 24 else 0.20  # a bigger seal keeps a dot of colour at 16 px
    if 1 - seal <= u <= 1 and 1 - seal <= v <= 1:
        return (VERMILION[0], VERMILION[1], VERMILION[2], 1.0)
    color = _lerp(color, CREAM, _glyph(u, v))
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
    root = Path(__file__).resolve().parents[2]
    load_mask(root / "build" / "emblem-mask.bmp")
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
