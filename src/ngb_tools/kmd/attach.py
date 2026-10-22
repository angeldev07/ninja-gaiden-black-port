"""Weapons and bone-mounted objects: `.itm` anchors and the node that goes into the glTF.

Each of Ryu's weapons lives in `cmn.afs` as a `<weapon>.kmd` + `<weapon>.itm` pair, for
example `katana.kmd` (#70) and `katana.itm` (#192), `nchak` (#76/#193), `flail` (#128/#208),
`rod` (#291/#290), `hammer` (#85/#196), `zanba` (#89/#197) or `yoto` (#109/#249). The `.itm`
is an `itm_dat2` chunk. Its slot 0 holds impact volumes (not interpreted). Its slot 1 is the
**anchor table**:

    +0x00  u32 offset of the records (relative to the slot; 0x40 in all the ones seen)
    +0x08  u32 table size in bytes
    0x20-byte records up to a u32 0xFFFFFF00:
        u32 key      low 16 bits = state and high 16 = variant
        u32 bone     character object index (in Ryu: 11 hand -x, 5 hand +x,
                     0 chest, 2 hip)
        f32 x3       position in the bone's local space (meters)
        f32 x3       rotation in degrees (Euler; order INFERRED like the motions':
                     R = Rz·Ry·Rx)

Evidence: weapons of the same type share a table (katana, yoto, boku and skata; hammer and
zanba; nchak and flail). In all of them, state 1 puts the weapon on the chest (the sword on
the back) or on the hip, and states 2 and 3 put it in the hand of bone 11. For a character
facing +z in a right-handed system, the -x side is the right one, and Ryu wields with his
right hand. On the katana, state 2 places the grip 5.8 cm from the wrist, in the palm, with
the blade (the weapon's +z) coming out of the fist. Meaning of the states: INFERRED (1 =
stowed, 2 = in hand; 5-9 use hand 5).
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
    """The bytes do not form an `.itm` with a readable anchor table."""


@dataclass(frozen=True, slots=True)
class ItemAnchor:
    """One table record: state, variant, bone, position and rotation (degrees)."""

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
    """A model mounted on an object of the main model, with its local transform.

    The exporter hangs it as a child of the `parent` node, so it inherits its animation.
    """

    name: str
    model: KmdModel
    texture_files: Mapping[int, Path]
    parent: int
    translation: Vec3 = (0.0, 0.0, 0.0)
    rotation: Quat = (0.0, 0.0, 0.0, 1.0)
    groups: frozenset[int] | None = field(default=WEAPON_GROUPS)


def parse_item_anchors(data: bytes) -> tuple[ItemAnchor, ...]:
    """The anchor table (slot 1) of an `itm_dat2`."""
    try:
        chunk = parse_chunk(data)
    except TeamNinjaError as error:
        raise ItemFormatError(f"itm: {error}") from error
    if chunk.tag != ITEM_TAG:
        raise ItemFormatError(f"expected a {ITEM_TAG} chunk, found '{chunk.tag}'")
    slot = next((s for s in chunk.slots if s.index == ANCHOR_SLOT), None)
    if slot is None:
        raise ItemFormatError(f"{ITEM_TAG} without slot {ANCHOR_SLOT} (anchor table)")
    body = data[slot.offset : slot.end]
    if len(body) < 4:
        raise ItemFormatError("empty anchor table")
    (pos,) = struct.unpack_from("<I", body, 0)
    anchors: list[ItemAnchor] = []
    while True:
        if pos + 4 > len(body) or len(anchors) > MAX_RECORDS:
            raise ItemFormatError(f"anchor table without terminator {END_KEY:#x}")
        (key,) = struct.unpack_from("<I", body, pos)
        if key == END_KEY:
            return tuple(anchors)
        if pos + RECORD_SIZE > len(body):
            raise ItemFormatError(f"anchor record truncated at {pos:#x}")
        _, bone, px, py, pz, rx, ry, rz = _RECORD.unpack_from(body, pos)
        anchors.append(ItemAnchor(key & 0xFFFF, key >> 16, bone, (px, py, pz), (rx, ry, rz)))
        pos += RECORD_SIZE


def find_anchor(
    anchors: tuple[ItemAnchor, ...], state: int = HELD_STATE, variant: int = 0
) -> ItemAnchor:
    """The first anchor with that state and variant."""
    for anchor in anchors:
        if anchor.state == state and anchor.variant == variant:
            return anchor
    states = sorted({(a.state, a.variant) for a in anchors})
    raise ItemFormatError(f"no anchor for state {state} (available: {states})")
