"""Synthetic AFS builders: game data is never used in the tests.

They reproduce the layout of the Ninja Gaiden Black AFS files: data aligned to 0x800,
attribute table at the end and the `custom` field copied from the header.
"""

from __future__ import annotations

import struct
import zlib
from collections.abc import Sequence
from typing import Literal

AttrMode = Literal["after_toc", "before_data", "none"]
Date = tuple[int, int, int, int, int, int]

SECTOR = 0x800
DEFAULT_DATE: Date = (2004, 2, 10, 23, 56, 2)


def align(n: int, alignment: int = SECTOR) -> int:
    """Round `n` up to a multiple of `alignment`."""
    return (n + alignment - 1) // alignment * alignment


def build_afs(
    files: Sequence[tuple[str, bytes]],
    attr_mode: AttrMode = "after_toc",
    date: Date = DEFAULT_DATE,
) -> bytes:
    """Synthetic AFS with `files` = [(name, data)] and the attribute pointer at `attr_mode`."""
    count = len(files)
    toc_end = 8 + 8 * count
    pos = align(toc_end + 8)
    toc: list[tuple[int, int]] = []
    for _, data in files:
        toc.append((pos, len(data)))
        pos = align(pos + len(data))
    attr_offset, attr_size = pos, count * 0x30
    buf = bytearray(align(attr_offset + attr_size) if attr_mode != "none" else pos)
    struct.pack_into("<4sI", buf, 0, b"AFS\x00", count)
    for i, ((offset, size), (_, data)) in enumerate(zip(toc, files, strict=True)):
        struct.pack_into("<II", buf, 8 + 8 * i, offset, size)
        buf[offset : offset + size] = data
    if attr_mode != "none":
        ptr_pos = toc_end if attr_mode == "after_toc" else toc[0][0] - 8
        struct.pack_into("<II", buf, ptr_pos, attr_offset, attr_size)
        for i, (name, _) in enumerate(files):
            # Same as in the game: custom = u32 at offset 4 + 4*i of the file.
            custom = int.from_bytes(buf[4 + 4 * i : 8 + 4 * i], "little")
            struct.pack_into("<32s6HI", buf, attr_offset + 0x30 * i, name.encode(), *date, custom)
    return bytes(buf)


def make_adx(length: int = 64) -> bytes:
    """Minimal ADX header: `80 00`, copyright offset and `(c)CRI` right before the audio."""
    adx = bytearray(length)
    adx[0:4] = b"\x80\x00\x00\x20"
    adx[0x1E:0x24] = b"(c)CRI"
    return bytes(adx)


FILES: tuple[tuple[str, bytes], ...] = (
    ("menu.xpr", b"XPR0" + bytes(60)),
    ("song", zlib.compress(bytes(range(256)) * 64)),
    ("table.bin", b"\x01\x00\x00\x00" * 1000),
)
