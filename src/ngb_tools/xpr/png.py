"""Escritor PNG mínimo con la stdlib (zlib + crc32): RGBA de 8 bits, sin entrelazado.

Un PNG es la firma de 8 bytes y una lista de chunks `u32 BE longitud, tipo[4], datos,
u32 BE CRC-32(tipo + datos)`. Bastan tres:

- `IHDR`: ancho, alto, profundidad 8, tipo de color 6 (RGBA), compresión 0 (deflate),
  filtro 0 y entrelazado 0;
- `IDAT`: las filas comprimidas con zlib, cada una precedida de su byte de filtro. Se usa
  el filtro 0 (ninguno): más simple y suficiente para inspeccionar texturas;
- `IEND`: vacío, marca el final.

Especificación: https://www.w3.org/TR/png-3/
"""

from __future__ import annotations

import struct
import zlib
from typing import Final

SIGNATURE: Final = b"\x89PNG\r\n\x1a\n"
_BIT_DEPTH: Final = 8
_COLOR_RGBA: Final = 6
_FILTER_NONE: Final = b"\x00"


def _chunk(kind: bytes, body: bytes) -> bytes:
    crc = zlib.crc32(kind + body)
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", crc)


def encode_png(width: int, height: int, rgba: bytes, *, level: int = 9) -> bytes:
    """PNG de `width`x`height` a partir de píxeles RGBA8 fila a fila."""
    if width <= 0 or height <= 0:
        raise ValueError(f"dimensiones inválidas: {width}x{height}")
    stride = 4 * width
    if len(rgba) != stride * height:
        raise ValueError(f"{len(rgba)} bytes para {width}x{height} RGBA ({stride * height})")
    header = struct.pack(">IIBBBBB", width, height, _BIT_DEPTH, _COLOR_RGBA, 0, 0, 0)
    raw = b"".join(_FILTER_NONE + rgba[y * stride : (y + 1) * stride] for y in range(height))
    return (
        SIGNATURE
        + _chunk(b"IHDR", header)
        + _chunk(b"IDAT", zlib.compress(raw, level))
        + _chunk(b"IEND", b"")
    )
