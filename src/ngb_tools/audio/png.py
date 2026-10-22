"""Minimal PNG writer (8-bit RGB, no filters), using the stdlib zlib."""

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
    """PNG of `height` rows of `width` RGB pixels (3 bytes per pixel)."""
    if len(rows) != height or any(len(row) != 3 * width for row in rows):
        raise ValueError("the rows do not match the image size")
    header = struct.pack(">IIBBBBB", width, height, 8, _RGB, 0, 0, 0)
    raw = b"".join(b"\x00" + bytes(row) for row in rows)
    return (
        SIGNATURE
        + _chunk(b"IHDR", header)
        + _chunk(b"IDAT", zlib.compress(raw, 6))
        + _chunk(b"IEND", b"")
    )
