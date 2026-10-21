"""Escritor PNG mínimo (RGB de 8 bits, sin filtros), con zlib de la stdlib."""

from __future__ import annotations

import struct
import zlib
from collections.abc import Sequence
from typing import Final

SIGNATURE: Final = b"\x89PNG\r\n\x1a\n"
_RGB: Final = 2


def _chunk(kind: bytes, payload: bytes) -> bytes:
    crc = zlib.crc32(kind + payload) & 0xFFFFFFFF
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", crc)


def encode_rgb(width: int, height: int, rows: Sequence[bytes | bytearray]) -> bytes:
    """PNG de `height` filas de `width` píxeles RGB (3 bytes por píxel)."""
    if len(rows) != height or any(len(row) != 3 * width for row in rows):
        raise ValueError("las filas no cuadran con el tamaño de la imagen")
    header = struct.pack(">IIBBBBB", width, height, 8, _RGB, 0, 0, 0)
    raw = b"".join(b"\x00" + bytes(row) for row in rows)
    return (
        SIGNATURE
        + _chunk(b"IHDR", header)
        + _chunk(b"IDAT", zlib.compress(raw, 6))
        + _chunk(b"IEND", b"")
    )
