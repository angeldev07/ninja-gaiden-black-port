"""Builder of synthetic Team Ninja chunks: game data is never used in the tests.

Reproduces the layout observed in `ryu.kmd`: 0x20-byte header, offset table
at `table_pos` and each payload aligned to 0x10 after the table.
"""

from __future__ import annotations

import struct
from collections.abc import Sequence

HEADER_SIZE = 0x20


def align(n: int, alignment: int = 0x10) -> int:
    """Round `n` up to the multiple of `alignment`."""
    return (n + alignment - 1) // alignment * alignment


def build_chunk(
    tag: bytes,
    payloads: Sequence[bytes | None],
    *,
    table_pos: int = HEADER_SIZE,
    trailing: int = 0,
) -> bytes:
    """Chunk with one slot per element of `payloads` (`None` = empty slot, offset 0).

    `trailing` appends padding bytes after the chunk, like the sector padding of an AFS.
    """
    pos = align(table_pos + 4 * len(payloads))
    offsets: list[int] = []
    for payload in payloads:
        if payload is None:
            offsets.append(0)
            continue
        offsets.append(pos)
        pos = align(pos + len(payload))
    buf = bytearray(pos)
    used = sum(1 for payload in payloads if payload is not None)
    struct.pack_into("<12s5I", buf, 0, tag, pos, len(payloads), used, table_pos, HEADER_SIZE)
    for i, (offset, payload) in enumerate(zip(offsets, payloads, strict=True)):
        struct.pack_into("<I", buf, table_pos + 4 * i, offset)
        if payload is not None:
            buf[offset : offset + len(payload)] = payload
    return bytes(buf) + bytes(trailing)
