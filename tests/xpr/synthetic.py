"""Builders of synthetic XPR0s: game data is never used in the tests.

They reproduce what is observed in Ninja Gaiden Black's XPR0s: resource headers followed
by the 0xFFFFFFFF marker, 0xAD padding up to `header_size` and the GPU data after it, with
the `Data` fields relative to the start of that data.
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
    """`Format` field: dma/border 0-3, cubemap 2, dimensions 4-7, format 8-15, mips 16-19,
    log2 U 20-23, log2 V 24-27 and log2 P 28-31."""
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
    """`Size` field of a linear texture: width-1, height-1 and pitch/64-1."""
    return (width - 1) | ((height - 1) << 12) | ((pitch // 64 - 1) << 24)


def texture_header(data: int, fmt: int, size: int = 0, common: int = TEXTURE_COMMON) -> bytes:
    return struct.pack("<5I", common, data, 0, fmt, size)


def palette_header(data: int, size_code: int = 0) -> bytes:
    return struct.pack("<3I", PALETTE_COMMON | (size_code << 30), data, 0)


def buffer_header(common: int, data: int) -> bytes:
    return struct.pack("<3I", common, data, 0)


def push_header(data: int, size: int) -> bytes:
    """Push buffer as in the game's KMDs: 6 u32s, the last one 0 (Cxbx documents 5)."""
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
    """XPR0 with those resource headers and `data` as the GPU data region."""
    table = b"".join(headers) + (struct.pack("<I", END_MARKER) if end_marker else b"")
    size = align(12 + len(table), 0x800) if header_size is None else header_size
    header_pad = bytes([PAD]) * (size - 12 - len(table))
    total = size + len(data)
    return b"XPR0" + struct.pack("<II", total, size) + table + header_pad + data


def solid_dxt1_block(color565: int) -> bytes:
    """Single-color DXT1 block: c0 = c1 = color and indices 0."""
    return struct.pack("<HHI", color565, color565, 0)


def d3dcolor(red: int, green: int, blue: int, alpha: int = 255) -> bytes:
    """D3DCOLOR palette entry (little-endian ARGB u32: B, G, R, A in memory)."""
    return bytes((blue, green, red, alpha))
