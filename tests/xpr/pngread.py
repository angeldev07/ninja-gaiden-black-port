"""Minimal strict PNG reader, for the tests only: validates everything `png` writes.

Checks the signature, the CRC of each chunk, that IHDR comes first and IEND last, and
decompresses the concatenated IDATs. Only accepts what the writer produces: non-interlaced
RGBA8 with filter 0 on every row.
"""

from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass

SIGNATURE = b"\x89PNG\r\n\x1a\n"


class PngError(Exception):
    """The PNG is not valid or uses something this reader does not support."""


@dataclass(frozen=True, slots=True)
class DecodedPng:
    width: int
    height: int
    rgba: bytes
    chunk_types: tuple[bytes, ...]


def iter_chunks(data: bytes) -> list[tuple[bytes, bytes]]:
    if not data.startswith(SIGNATURE):
        raise PngError("PNG signature missing")
    chunks: list[tuple[bytes, bytes]] = []
    pos = len(SIGNATURE)
    while pos < len(data):
        if pos + 12 > len(data):
            raise PngError(f"chunk truncated at {pos}")
        (length,) = struct.unpack_from(">I", data, pos)
        kind = data[pos + 4 : pos + 8]
        body = data[pos + 8 : pos + 8 + length]
        (crc,) = struct.unpack_from(">I", data, pos + 8 + length)
        if zlib.crc32(kind + body) != crc:
            raise PngError(f"bad CRC in chunk {kind!r}")
        chunks.append((kind, body))
        pos += 12 + length
    return chunks


def read_png(data: bytes) -> DecodedPng:
    chunks = iter_chunks(data)
    kinds = tuple(kind for kind, _ in chunks)
    if not kinds or kinds[0] != b"IHDR" or kinds[-1] != b"IEND":
        raise PngError(f"invalid chunk order: {kinds}")
    width, height, depth, color, compression, filtering, interlace = struct.unpack(
        ">IIBBBBB", chunks[0][1]
    )
    if (depth, color, compression, filtering, interlace) != (8, 6, 0, 0, 0):
        raise PngError("only non-interlaced RGBA8")
    raw = zlib.decompress(b"".join(body for kind, body in chunks if kind == b"IDAT"))
    stride = 1 + 4 * width
    if len(raw) != stride * height:
        raise PngError(f"IDAT of {len(raw)} bytes; expected {stride * height}")
    rows = []
    for y in range(height):
        row = raw[y * stride : (y + 1) * stride]
        if row[0] != 0:
            raise PngError(f"row {y} with filter {row[0]}")
        rows.append(row[1:])
    return DecodedPng(width, height, b"".join(rows), kinds)
