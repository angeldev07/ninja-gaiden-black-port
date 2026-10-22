"""OBJINFO: one entry per model object, with its local transform and its volumes.

The OBJINFO subchunk carries the Team Ninja chunk header (`ngb_tools.teamninja`) and one
slot per object. Each entry (little-endian u32 and f32, verified on `ryu.kmd`):

    0x00  "OI\\x01\\x01"   magic
    0x04  header_size    0x90: where the per-submesh records start
    0x08  n_sub          submeshes; same as in the XPR0 mesh descriptor
    0x0C  group          1 detail, 2 shadow, 3 effect, 4 skin, 5 LOD (names INFERRED)
    0x10  flags          0x201 / 0xa01 / 0x200 (not interpreted)
    0x18  f32 1.0
    0x30  T  f32 x3      local translation relative to the HIERARC parent node
    0x40  R  f32 x3      rotation: 0 in every observed object, order unknown
    0x50  S  f32 x3      scale: 1, or 0 in hidden effect objects
    0x60  bbox_min x3    matches the minimum of the vertices
    0x70  bbox_size x3   maximum = min + size
    0x80  sphere x4      center and radius
    header_size + 0x20 * i: u32 flags, 12 bytes of padding and the sphere of submesh i

OBJINFO does not point to buffers: object k uses the k-th descriptor, VB and IB of the XPR0.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from enum import IntEnum
from typing import Final

from ngb_tools.kmd.errors import KmdFormatError
from ngb_tools.teamninja import TeamNinjaError, parse_chunk

OBJINFO_TAG: Final = "OBJINFO"
ENTRY_MAGIC: Final = b"OI\x01\x01"
MIN_HEADER_SIZE: Final = 0x90
SUBMESH_RECORD_SIZE: Final = 0x20
# Cap on submeshes per object: the maximum observed is 8 (Ryu's head).
MAX_SUBMESHES: Final = 256

type Vec3 = tuple[float, float, float]
type Sphere = tuple[float, float, float, float]

_HEAD: Final = struct.Struct("<4sIIII")
_VEC3: Final = struct.Struct("<3f")
_SPHERE: Final = struct.Struct("<4f")


class ObjectGroup(IntEnum):
    """Field 0x0C. The names are INFERRED from what each group looks like when rendered."""

    DETAIL = 1
    SHADOW = 2
    EFFECT = 3
    SKIN = 4
    LOD = 5


def group_name(group: int) -> str:
    try:
        return ObjectGroup(group).name.lower()
    except ValueError:
        return f"group{group}"


@dataclass(frozen=True, slots=True)
class ObjectInfo:
    """One OBJINFO entry."""

    index: int
    submesh_count: int
    group: int
    flags: int
    translation: Vec3
    rotation: Vec3
    scale: Vec3
    bbox_min: Vec3
    bbox_size: Vec3
    sphere: Sphere
    submesh_spheres: tuple[Sphere, ...]


def _vec3(data: bytes, offset: int) -> Vec3:
    x, y, z = _VEC3.unpack_from(data, offset)
    return (x, y, z)


def _sphere(data: bytes, offset: int) -> Sphere:
    x, y, z, r = _SPHERE.unpack_from(data, offset)
    return (x, y, z, r)


def parse_object_entry(index: int, entry: bytes) -> ObjectInfo:
    """Parse one entry; `entry` is the bytes of its slot."""
    if len(entry) < MIN_HEADER_SIZE:
        raise KmdFormatError(
            f"OBJINFO {index}: {len(entry):#x} bytes, minimum {MIN_HEADER_SIZE:#x}"
        )
    magic, header_size, n_sub, group, flags = _HEAD.unpack_from(entry)
    if magic != ENTRY_MAGIC:
        raise KmdFormatError(f"OBJINFO {index}: magic {magic!r}, expected {ENTRY_MAGIC!r}")
    if n_sub > MAX_SUBMESHES:
        raise KmdFormatError(
            f"OBJINFO {index}: {n_sub} submeshes exceeds the cap of {MAX_SUBMESHES}"
        )
    end = header_size + SUBMESH_RECORD_SIZE * n_sub
    if header_size < MIN_HEADER_SIZE or end > len(entry):
        raise KmdFormatError(
            f"OBJINFO {index}: header {header_size:#x} and {n_sub} submeshes do not fit in "
            f"{len(entry):#x} bytes"
        )
    spheres = tuple(
        _sphere(entry, header_size + SUBMESH_RECORD_SIZE * i + 0x10) for i in range(n_sub)
    )
    return ObjectInfo(
        index=index,
        submesh_count=n_sub,
        group=group,
        flags=flags,
        translation=_vec3(entry, 0x30),
        rotation=_vec3(entry, 0x40),
        scale=_vec3(entry, 0x50),
        bbox_min=_vec3(entry, 0x60),
        bbox_size=_vec3(entry, 0x70),
        sphere=_sphere(entry, 0x80),
        submesh_spheres=spheres,
    )


def parse_objinfo(data: bytes) -> tuple[ObjectInfo, ...]:
    """All entries of the OBJINFO subchunk that starts at `data[0]`.

    Each slot is an object, so there can be no empty slots: the slot index is the object
    index in the rest of the KMD.
    """
    try:
        chunk = parse_chunk(data)
    except TeamNinjaError as error:
        raise KmdFormatError(f"OBJINFO: {error}") from error
    if chunk.tag != OBJINFO_TAG:
        raise KmdFormatError(f"expected subchunk {OBJINFO_TAG}, found '{chunk.tag}'")
    if chunk.n_used != chunk.n_slots:
        raise KmdFormatError(
            f"OBJINFO: {chunk.n_slots - chunk.n_used} empty slots; each slot is an object"
        )
    return tuple(
        parse_object_entry(slot.index, data[slot.offset : slot.end]) for slot in chunk.slots
    )
