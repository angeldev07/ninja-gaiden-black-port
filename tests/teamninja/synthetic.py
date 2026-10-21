"""Constructor de chunks de Team Ninja sintéticos: nunca se usan datos del juego en los tests.

Reproduce la disposición observada en `ryu.kmd`: cabecera de 0x20 bytes, tabla de offsets
en `table_pos` y cada carga útil alineada a 0x10 detrás de la tabla.
"""

from __future__ import annotations

import struct
from collections.abc import Sequence

HEADER_SIZE = 0x20


def align(n: int, alignment: int = 0x10) -> int:
    """Redondea `n` hacia arriba al múltiplo de `alignment`."""
    return (n + alignment - 1) // alignment * alignment


def build_chunk(
    tag: bytes,
    payloads: Sequence[bytes | None],
    *,
    table_pos: int = HEADER_SIZE,
    trailing: int = 0,
) -> bytes:
    """Chunk con un slot por elemento de `payloads` (`None` = slot vacío, offset 0).

    `trailing` añade bytes de relleno tras el chunk, como el padding de sector de un AFS.
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
