"""Constructores de XPR0 sintéticos: nunca se usan datos del juego en los tests.

Reproducen lo observado en los XPR0 de Ninja Gaiden Black: cabeceras de recurso seguidas
de la marca 0xFFFFFFFF, relleno 0xAD hasta `header_size` y los datos de GPU detrás, con
los campos `Data` relativos al inicio de esos datos.
"""

from __future__ import annotations

import struct
from collections.abc import Sequence

TEXTURE_COMMON = 0x00040001
PALETTE_COMMON = 0x00830001
VERTEX_COMMON = 0x00800001
INDEX_COMMON = 0x00010001
PUSH_COMMON = 0x00020001
USERDATA_MARKER = 0x80000000
END_MARKER = 0xFFFFFFFF
PAD = 0xAD


def align(n: int, alignment: int) -> int:
    return (n + alignment - 1) // alignment * alignment


def format_word(
    code: int,
    log2_u: int,
    log2_v: int,
    *,
    levels: int = 1,
    dimensions: int = 2,
    cubemap: bool = False,
    log2_p: int = 0,
    dma: int = 0x9,
) -> int:
    """Campo `Format`: dma/borde 0-3, cubemap 2, dimensiones 4-7, formato 8-15, mips 16-19,
    log2 U 20-23, log2 V 24-27 y log2 P 28-31."""
    return (
        dma
        | (int(cubemap) << 2)
        | (dimensions << 4)
        | (code << 8)
        | (levels << 16)
        | (log2_u << 20)
        | (log2_v << 24)
        | (log2_p << 28)
    )


def size_word(width: int, height: int, pitch: int) -> int:
    """Campo `Size` de una textura lineal: ancho-1, alto-1 y pitch/64-1."""
    return (width - 1) | ((height - 1) << 12) | ((pitch // 64 - 1) << 24)


def texture_header(data: int, fmt: int, size: int = 0, common: int = TEXTURE_COMMON) -> bytes:
    return struct.pack("<5I", common, data, 0, fmt, size)


def palette_header(data: int, size_code: int = 0) -> bytes:
    return struct.pack("<3I", PALETTE_COMMON | (size_code << 30), data, 0)


def buffer_header(common: int, data: int) -> bytes:
    return struct.pack("<3I", common, data, 0)


def push_header(data: int, size: int) -> bytes:
    """Push buffer como en los KMD del juego: 6 u32, el último a 0 (Cxbx documenta 5)."""
    return struct.pack("<6I", PUSH_COMMON, data, 0, size, size, 0)


def userdata(payload: bytes) -> bytes:
    return struct.pack("<II", USERDATA_MARKER, len(payload)) + payload


def build_xpr(
    headers: Sequence[bytes],
    data: bytes,
    *,
    header_size: int | None = None,
    end_marker: bool = True,
) -> bytes:
    """XPR0 con esas cabeceras de recurso y `data` como región de datos de GPU."""
    table = b"".join(headers) + (struct.pack("<I", END_MARKER) if end_marker else b"")
    size = align(12 + len(table), 0x800) if header_size is None else header_size
    header_pad = bytes([PAD]) * (size - 12 - len(table))
    total = size + len(data)
    return b"XPR0" + struct.pack("<II", total, size) + table + header_pad + data


def solid_dxt1_block(color565: int) -> bytes:
    """Bloque DXT1 de un solo color: c0 = c1 = color e índices 0."""
    return struct.pack("<HHI", color565, color565, 0)


def d3dcolor(red: int, green: int, blue: int, alpha: int = 255) -> bytes:
    """Entrada de paleta D3DCOLOR (u32 ARGB little endian: B, G, R, A en memoria)."""
    return bytes((blue, green, red, alpha))
