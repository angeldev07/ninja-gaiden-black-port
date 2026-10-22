"""Minimal PNG writer using the stdlib (zlib + crc32): 8-bit RGBA, non-interlaced.

A PNG is the 8-byte signature and a list of chunks `u32 BE length, type[4], data,
u32 BE CRC-32(type + data)`. Three are enough:

- `IHDR`: width, height, depth 8, color type 6 (RGBA), compression 0 (deflate),
  filter 0 and interlace 0;
- `IDAT`: the rows compressed with zlib, each preceded by its filter byte. Filter 0
  (none) is used: simpler and enough for inspecting textures;
- `IEND`: empty, marks the end.

Specification: https://www.w3.org/TR/png-3/
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
    """PNG of `width`x`height` from RGBA8 pixels, row by row."""
    if width <= 0 or height <= 0:
        raise ValueError(f"invalid dimensions: {width}x{height}")
    stride = 4 * width
    if len(rgba) != stride * height:
        raise ValueError(f"{len(rgba)} bytes for {width}x{height} RGBA ({stride * height})")
    header = struct.pack(">IIBBBBB", width, height, _BIT_DEPTH, _COLOR_RGBA, 0, 0, 0)
    raw = b"".join(_FILTER_NONE + rgba[y * stride : (y + 1) * stride] for y in range(height))
    return (
        SIGNATURE
        + _chunk(b"IHDR", header)
        + _chunk(b"IDAT", zlib.compress(raw, level))
        + _chunk(b"IEND", b"")
    )
