"""Constructor de `.itm` sintéticos (chunk `itm_dat2`): nunca se usan datos del juego.

Reproduce lo observado en `katana.itm`: el slot 0 son volúmenes (aquí, relleno) y el slot 1
es la tabla de anclajes, con una cabecera cuyo primer u32 es el offset de los registros
(0x40), registros de 0x20 bytes y un terminador 0xFFFFFF00.
"""

from __future__ import annotations

import struct
from collections.abc import Sequence

from teamninja.synthetic import build_chunk

type Record = tuple[int, int, tuple[float, float, float], tuple[float, float, float]]

END_KEY = 0xFFFFFF00


def anchor_table(records: Sequence[Record], *, terminated: bool = True) -> bytes:
    body = bytearray(struct.pack("<4I", 0x40, 0, 0x20 * len(records) + 4, 0))
    body += bytes(0x40 - len(body))
    for key, bone, position, rotation in records:
        body += struct.pack("<II6f", key, bone, *position, *rotation)
    if terminated:
        body += struct.pack("<I", END_KEY)
    return bytes(body)


def build_item(records: Sequence[Record], *, terminated: bool = True) -> bytes:
    return build_chunk(b"itm_dat2", [bytes(0x20), anchor_table(records, terminated=terminated)])


# Como la katana: guardada a la espalda (estado 1), en la mano -x (estados 2 y 3) y una
# variante 0x3b del estado 3.
KATANA_LIKE: tuple[Record, ...] = (
    (0x2, 11, (-0.058, 0.0, -0.025), (0.0, -13.0, 0.0)),
    (0x3, 11, (-0.058, 0.0, -0.025), (0.0, -13.0, 0.0)),
    (0x1, 0, (-0.233, 0.463, -0.113), (84.232, 180.288, 30.955)),
    (0x3B0003, 1, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0)),
)
