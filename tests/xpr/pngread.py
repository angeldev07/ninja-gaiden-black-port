"""Lector PNG mínimo y estricto, solo para los tests: valida todo lo que escribe `png`.

Comprueba la firma, el CRC de cada chunk, que IHDR vaya primero e IEND al final, y
descomprime los IDAT concatenados. Solo admite lo que produce el escritor: RGBA8 sin
entrelazado y filtro 0 en cada fila.
"""

from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass

SIGNATURE = b"\x89PNG\r\n\x1a\n"


class PngError(Exception):
    """El PNG no es válido o usa algo que este lector no admite."""


@dataclass(frozen=True, slots=True)
class DecodedPng:
    width: int
    height: int
    rgba: bytes
    chunk_types: tuple[bytes, ...]


def iter_chunks(data: bytes) -> list[tuple[bytes, bytes]]:
    if not data.startswith(SIGNATURE):
        raise PngError("firma PNG ausente")
    chunks: list[tuple[bytes, bytes]] = []
    pos = len(SIGNATURE)
    while pos < len(data):
        if pos + 12 > len(data):
            raise PngError(f"chunk truncado en {pos}")
        (length,) = struct.unpack_from(">I", data, pos)
        kind = data[pos + 4 : pos + 8]
        body = data[pos + 8 : pos + 8 + length]
        (crc,) = struct.unpack_from(">I", data, pos + 8 + length)
        if zlib.crc32(kind + body) != crc:
            raise PngError(f"CRC incorrecto en el chunk {kind!r}")
        chunks.append((kind, body))
        pos += 12 + length
    return chunks


def read_png(data: bytes) -> DecodedPng:
    chunks = iter_chunks(data)
    kinds = tuple(kind for kind, _ in chunks)
    if not kinds or kinds[0] != b"IHDR" or kinds[-1] != b"IEND":
        raise PngError(f"orden de chunks inválido: {kinds}")
    width, height, depth, color, compression, filtering, interlace = struct.unpack(
        ">IIBBBBB", chunks[0][1]
    )
    if (depth, color, compression, filtering, interlace) != (8, 6, 0, 0, 0):
        raise PngError("solo RGBA8 sin entrelazado")
    raw = zlib.decompress(b"".join(body for kind, body in chunks if kind == b"IDAT"))
    stride = 1 + 4 * width
    if len(raw) != stride * height:
        raise PngError(f"IDAT de {len(raw)} bytes; se esperaban {stride * height}")
    rows = []
    for y in range(height):
        row = raw[y * stride : (y + 1) * stride]
        if row[0] != 0:
            raise PngError(f"fila {y} con filtro {row[0]}")
        rows.append(row[1:])
    return DecodedPng(width, height, b"".join(rows), kinds)
