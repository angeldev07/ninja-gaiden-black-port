"""Genera port/res/ngb.ico: un shuriken dorado sobre un cuadrado redondeado carmesí.

Arte original, dibujado por código (sin fuentes, imágenes ni datos del juego). Solo stdlib:
un rasterizador con supersampling escribe cada tamaño y el ICO se arma a mano (BMP de 32 bits
con máscara AND hasta 128 px, PNG para 256 px, como hace Windows desde Vista).

    python port/res/make_icon.py                    # escribe port/res/ngb.ico
    python port/res/make_icon.py --png vista.png    # además, vista previa de 256 px
"""

from __future__ import annotations

import argparse
import math
import struct
import zlib
from pathlib import Path

SIZES = (16, 24, 32, 48, 64, 128, 256)

# Paleta (RGB 0..255). El fondo reutiliza el carmesí del botón PLAY del launcher (#7a1414).
BG_TOP = (150, 28, 28)
BG_BOTTOM = (58, 8, 8)
BORDER = (214, 176, 98)
GOLD_LIGHT = (246, 228, 170)
GOLD_DARK = (178, 132, 54)
HOLE = (34, 6, 6)

TIP_RADIUS = 0.405  # radio de las puntas (el lienzo mide 1 x 1)
INNER_RADIUS = 0.125  # radio de los vértices cóncavos entre puntas
HOLE_RADIUS = 0.060
RING_RADIUS = 0.082
CORNER = 0.21  # radio de las esquinas del cuadrado


def _star_triangles() -> list[tuple[tuple[float, float], ...]]:
    """Los 8 triángulos (centro, punta, vértice interior) con las puntas en las diagonales."""
    tris = []
    for k in range(4):
        tip = math.radians(45 + 90 * k)
        tip_pt = (0.5 + TIP_RADIUS * math.cos(tip), 0.5 - TIP_RADIUS * math.sin(tip))
        for side in (-1, 1):
            inner = tip + side * math.radians(45)
            inner_pt = (
                0.5 + INNER_RADIUS * math.cos(inner),
                0.5 - INNER_RADIUS * math.sin(inner),
            )
            tris.append(((0.5, 0.5), tip_pt, inner_pt, side))
    return tris


TRIANGLES = _star_triangles()


def _in_triangle(px: float, py: float, a, b, c) -> bool:
    d1 = (px - b[0]) * (a[1] - b[1]) - (a[0] - b[0]) * (py - b[1])
    d2 = (px - c[0]) * (b[1] - c[1]) - (b[0] - c[0]) * (py - c[1])
    d3 = (px - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (py - a[1])
    neg = d1 < 0 or d2 < 0 or d3 < 0
    pos = d1 > 0 or d2 > 0 or d3 > 0
    return not (neg and pos)


def _rounded_square_sdf(x: float, y: float, margin: float) -> float:
    """Distancia con signo (negativa dentro) a un cuadrado redondeado de lado 1 - 2 * margin."""
    half = 0.5 - margin
    qx = abs(x - 0.5) - (half - CORNER)
    qy = abs(y - 0.5) - (half - CORNER)
    return math.hypot(max(qx, 0.0), max(qy, 0.0)) + min(max(qx, qy), 0.0) - CORNER


def _lerp(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def sample(x: float, y: float) -> tuple[float, float, float, float]:
    """Color (r, g, b, a) del punto (x, y) del lienzo unitario; a en 0..1, rgb en 0..255."""
    if _rounded_square_sdf(x, y, 0.02) > 0:
        return (0.0, 0.0, 0.0, 0.0)
    # Fondo: degradado vertical con un brillo suave arriba a la izquierda.
    color = _lerp(BG_TOP, BG_BOTTOM, y)
    glow = max(0.0, 1.0 - math.hypot(x - 0.3, y - 0.2) / 0.55) * 0.18
    color = _lerp(color, (255, 140, 120), glow)
    # Filete dorado interior.
    d = _rounded_square_sdf(x, y, 0.02)
    if d > -0.022:
        color = _lerp(color, BORDER, 0.85)
    elif d > -0.032:
        color = _lerp(color, BORDER, 0.25)
    # Sombra del shuriken (desplazada abajo a la derecha) y el shuriken.
    for dx, dy, shadow in ((0.012, 0.016, True), (0.0, 0.0, False)):
        for a, b, c, side in TRIANGLES:
            if _in_triangle(x - dx, y - dy, a, b, c):
                if shadow:
                    color = _lerp(color, (10, 0, 0), 0.55)
                else:
                    shade = GOLD_LIGHT if side > 0 else GOLD_DARK
                    # Un poco más oscuro hacia el centro: da relieve a las cuchillas.
                    r = math.hypot(x - 0.5, y - 0.5) / TIP_RADIUS
                    color = _lerp(shade, (255, 250, 225) if side > 0 else (120, 80, 28), 0.35 * r)
                break
    # Agujero central con aro.
    r = math.hypot(x - 0.5, y - 0.5)
    if r < RING_RADIUS:
        color = _lerp(GOLD_DARK, GOLD_LIGHT, 0.5) if r > HOLE_RADIUS else HOLE
    return (color[0], color[1], color[2], 1.0)


def render(size: int) -> list[tuple[int, int, int, int]]:
    """Píxeles RGBA (fila superior primero) con supersampling NxN (alfa promediado)."""
    ss = 4 if size <= 64 else 3
    pixels = []
    for py in range(size):
        for px in range(size):
            acc = [0.0, 0.0, 0.0, 0.0]
            for sy in range(ss):
                for sx in range(ss):
                    u = (px + (sx + 0.5) / ss) / size
                    v = (py + (sy + 0.5) / ss) / size
                    r, g, b, a = sample(u, v)
                    acc[0] += r * a
                    acc[1] += g * a
                    acc[2] += b * a
                    acc[3] += a
            n = ss * ss
            alpha = acc[3] / n
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


def encode_png(size: int, pixels) -> bytes:
    raw = bytearray()
    for y in range(size):
        raw.append(0)  # filtro "ninguno"
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


def encode_bmp_entry(size: int, pixels) -> bytes:
    """DIB de un ICO: cabecera con alto doble, píxeles BGRA de abajo arriba y máscara AND."""
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    body = bytearray()
    for y in range(size - 1, -1, -1):
        for r, g, b, a in pixels[y * size : (y + 1) * size]:
            body += bytes((b, g, r, a))
    mask_row = ((size + 31) // 32) * 4
    return header + bytes(body) + bytes(mask_row * size)  # AND a 0: manda el canal alfa


def build_ico() -> tuple[bytes, bytes]:
    entries = []
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
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", type=Path, default=Path(__file__).with_name("ngb.ico"))
    parser.add_argument("--png", type=Path, help="vista previa PNG de 256 px (fuera del repo)")
    args = parser.parse_args()
    ico, preview = build_ico()
    args.out.write_bytes(ico)
    print(f"{args.out}: {len(ico)} bytes, tamaños {SIZES}")
    if args.png:
        args.png.write_bytes(preview)
        print(f"{args.png}: vista previa")


if __name__ == "__main__":
    main()
