"""Constructores sintéticos de bancos de motions y de un KMD humanoide: nunca datos del juego.

Reproducen lo observado en `ryu.mtn`: tabla de ids terminada en 0, datos de cada pista justo
antes de la cabecera de su motion (offsets terminados en 0) y claves con un u16 de control
(tipo en los bits 0-1, frames en los bits 2-11 y bits altos de un valor de 20 bits en 12-15).
"""

from __future__ import annotations

import struct
from collections.abc import Sequence

from teamninja.synthetic import build_chunk

from .synthetic import SynthObject

TRACKS = 44


def key(kind: int, frames: int, value: int, *slopes: int) -> bytes:
    raw = value & 0xFFFFF
    ctrl = kind | (frames << 2) | ((raw >> 16) << 12)
    return struct.pack("<HH", ctrl, raw & 0xFFFF) + struct.pack(f"<{len(slopes)}h", *slopes)


def constant(value: int, frames: int) -> bytes:
    """Pista constante: una clave de valor y la de fin."""
    return key(1, frames, value) + key(0, 0, value)


def build_bank(motions: Sequence[Sequence[bytes]], table: Sequence[int]) -> bytes:
    """Cuerpo de banco (como un `.mot`): `table[id]` = índice de la motion de ese id."""
    body = bytearray(4 * (len(table) + 1))
    body += bytes(-len(body) % 16)
    headers: list[int] = []
    for tracks in motions:
        offsets = []
        for track in tracks:
            offsets.append(len(body))
            body += track
        body += bytes(-len(body) % 4)
        headers.append(len(body))
        body += struct.pack(f"<{len(offsets)}I", *offsets) + bytes(4)
    for motion_id, motion in enumerate(table):
        struct.pack_into("<I", body, 4 * motion_id, headers[motion])
    return bytes(body)


def chr_dat(body: bytes) -> bytes:
    """El mismo banco envuelto en un `chr_dat` de 3 slots, como un `.mtn`."""
    return build_chunk(b"chr_dat", [body, bytes(16), struct.pack("<4I", 8, 0, 0x100, 0)])


def humanoid() -> list[SynthObject]:
    """15 huesos como Ryu: 0 cadera, 1 pecho, 2 cabeza, brazos 3-5 (-x) y 6-8 (+x), piernas
    9-11 (-x) y 12-14 (+x). Brazos de 0,25 + 0,25 m en pose T y piernas de 0,4 + 0,4 m."""
    t = [
        (0.0, 0.0, 0.0), (0.0, 0.2, 0.0), (0.0, 0.3, 0.0),
        (-0.2, 0.25, 0.0), (-0.25, 0.0, 0.0), (-0.25, 0.0, 0.0),
        (0.2, 0.25, 0.0), (0.25, 0.0, 0.0), (0.25, 0.0, 0.0),
        (-0.1, -0.05, 0.0), (0.0, -0.4, 0.0), (0.0, -0.4, 0.0),
        (0.1, -0.05, 0.0), (0.0, -0.4, 0.0), (0.0, -0.4, 0.0),
    ]  # fmt: skip
    children = {0: (1, 9, 12), 1: (2, 3, 6), 3: (4,), 4: (5,), 6: (7,), 7: (8,)}
    children |= {9: (10,), 10: (11,), 12: (13,), 13: (14,)}
    return [SynthObject(translation=t[k], children=children.get(k, ())) for k in range(15)]


def standing_values() -> list[int]:
    """Pose con valores calculados a mano (unidades de 1e-4).

    Cadera en (0, 0,9, 0) sin rotaciones. Brazos estirados en pose T: el objetivo está a
    0,5 m del hombro, justo el alcance. Piernas con el tobillo 0,48 m por debajo de la
    cadera y H = ±90° (rodilla hacia +z). Por la ley de los cosenos: cos a = 0,6 y
    sin a = 0,8, así que la rodilla queda en cadera + (0, -0,24, 0,32).
    """
    v = [0] * TRACKS
    v[1] = 9000
    v[12:15] = [-7000, 13500, 0]  # brazo -x: hombro (-0,2, 1,35, 0) + 0,5 en -x
    v[20:23] = [7000, 13500, 0]
    v[28:31] = [-1000, 3700, 0]  # pierna -x: cadera (-0,1, 0,85, 0) - 0,48 en y
    v[35] = 15708  # H = +90°: el lado -x rota (-1, 0, 0) hasta +z
    v[36:39] = [1000, 3700, 0]
    v[43] = -15708
    return v


def standing_motion(frames: int = 2) -> list[bytes]:
    return [constant(value, frames) for value in standing_values()]
