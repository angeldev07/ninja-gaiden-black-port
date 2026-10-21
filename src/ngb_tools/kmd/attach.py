"""Armas y objetos montados en un hueso: anclajes de los `.itm` y el nodo que va al glTF.

Cada arma de Ryu está en `cmn.afs` como un par `<arma>.kmd` + `<arma>.itm`, por ejemplo
`katana.kmd` (#70) y `katana.itm` (#192), `nchak` (#76/#193), `flail` (#128/#208), `rod`
(#291/#290), `hammer` (#85/#196), `zanba` (#89/#197) o `yoto` (#109/#249). El `.itm` es un
chunk `itm_dat2`. Su slot 0 son volúmenes de impacto (sin interpretar). Su slot 1 es la
**tabla de anclajes**:

    +0x00  u32 offset de los registros (relativo al slot; 0x40 en todos los vistos)
    +0x08  u32 bytes de la tabla
    registros de 0x20 bytes hasta un u32 0xFFFFFF00:
        u32 clave    16 bits bajos = estado y 16 altos = variante
        u32 hueso    índice de objeto del personaje (en Ryu: 11 mano -x, 5 mano +x,
                     0 pecho, 2 cadera)
        f32 x3       posición en el espacio local del hueso (metros)
        f32 x3       rotación en grados (Euler; orden INFERRED como el de las motions:
                     R = Rz·Ry·Rx)

Evidencia: las armas del mismo tipo comparten tabla (katana, yoto, boku y skata; hammer y
zanba; nchak y flail). En todas, el estado 1 lleva el arma al pecho (la espada a la
espalda) o a la cadera, y los estados 2 y 3, a la mano del hueso 11. Para un personaje que
mira a +z en un sistema de mano derecha, el lado -x es el derecho, y Ryu empuña con la
derecha. En la katana, el estado 2 pone la empuñadura a 5,8 cm de la muñeca, en la palma,
con la hoja (+z del arma) saliendo del puño. Significado de los estados: INFERRED (1 =
guardada, 2 = en la mano; 5-9 usan la mano 5).
"""

from __future__ import annotations

import math
import struct
from collections.abc import Mapping
from dataclasses import dataclass, field
from pathlib import Path
from typing import Final

from ngb_tools.kmd.errors import KmdError
from ngb_tools.kmd.model import KmdModel
from ngb_tools.kmd.objinfo import ObjectGroup, Vec3
from ngb_tools.kmd.rig import Quat, euler_xyz, quaternion
from ngb_tools.teamninja import TeamNinjaError, parse_chunk

ITEM_TAG: Final = "itm_dat2"
ANCHOR_SLOT: Final = 1
RECORD_SIZE: Final = 0x20
END_KEY: Final = 0xFFFFFF00
STOWED_STATE: Final = 1  # INFERRED
HELD_STATE: Final = 2  # INFERRED
MAX_RECORDS: Final = 4096
WEAPON_GROUPS: Final = frozenset({ObjectGroup.DETAIL.value})

_RECORD: Final = struct.Struct("<II6f")


class ItemFormatError(KmdError):
    """Los bytes no forman un `.itm` con tabla de anclajes legible."""


@dataclass(frozen=True, slots=True)
class ItemAnchor:
    """Un registro de la tabla: estado, variante, hueso, posición y rotación (grados)."""

    state: int
    variant: int
    bone: int
    position: Vec3
    rotation_deg: Vec3

    @property
    def quaternion(self) -> Quat:
        x, y, z = (math.radians(a) for a in self.rotation_deg)
        return quaternion(euler_xyz(x, y, z))


@dataclass(frozen=True, slots=True)
class Attachment:
    """Un modelo montado en un objeto del modelo principal, con su transformación local.

    El exportador lo cuelga como hijo del nodo de `parent`, así que hereda su animación.
    """

    name: str
    model: KmdModel
    texture_files: Mapping[int, Path]
    parent: int
    translation: Vec3 = (0.0, 0.0, 0.0)
    rotation: Quat = (0.0, 0.0, 0.0, 1.0)
    groups: frozenset[int] | None = field(default=WEAPON_GROUPS)


def parse_item_anchors(data: bytes) -> tuple[ItemAnchor, ...]:
    """La tabla de anclajes (slot 1) de un `itm_dat2`."""
    try:
        chunk = parse_chunk(data)
    except TeamNinjaError as error:
        raise ItemFormatError(f"itm: {error}") from error
    if chunk.tag != ITEM_TAG:
        raise ItemFormatError(f"se esperaba un chunk {ITEM_TAG}, hay '{chunk.tag}'")
    slot = next((s for s in chunk.slots if s.index == ANCHOR_SLOT), None)
    if slot is None:
        raise ItemFormatError(f"{ITEM_TAG} sin slot {ANCHOR_SLOT} (tabla de anclajes)")
    body = data[slot.offset : slot.end]
    if len(body) < 4:
        raise ItemFormatError("tabla de anclajes vacía")
    (pos,) = struct.unpack_from("<I", body, 0)
    anchors: list[ItemAnchor] = []
    while True:
        if pos + 4 > len(body) or len(anchors) > MAX_RECORDS:
            raise ItemFormatError(f"tabla de anclajes sin terminador {END_KEY:#x}")
        (key,) = struct.unpack_from("<I", body, pos)
        if key == END_KEY:
            return tuple(anchors)
        if pos + RECORD_SIZE > len(body):
            raise ItemFormatError(f"registro de anclaje cortado en {pos:#x}")
        _, bone, px, py, pz, rx, ry, rz = _RECORD.unpack_from(body, pos)
        anchors.append(ItemAnchor(key & 0xFFFF, key >> 16, bone, (px, py, pz), (rx, ry, rz)))
        pos += RECORD_SIZE


def find_anchor(
    anchors: tuple[ItemAnchor, ...], state: int = HELD_STATE, variant: int = 0
) -> ItemAnchor:
    """El primer anclaje de ese estado y variante."""
    for anchor in anchors:
        if anchor.state == state and anchor.variant == variant:
            return anchor
    states = sorted({(a.state, a.variant) for a in anchors})
    raise ItemFormatError(f"no hay anclaje para el estado {state} (hay {states})")
