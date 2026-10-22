"""HITMDL: the collision mesh of a KMD, one `VHD` record per object.

The HITMDL subchunk carries the Team Ninja chunk header with one slot per object (slot k is
object k; empty = no collision). Each record, in little-endian u32/f32 (VERIFIED with an
analysis script on the 323 KMDs with HITMDL in the game: 1929 records and 25876 blocks, all
consistent):

    0x00  "VHD\\0"
    0x04  u8 1, u8 0x0a, u16 n_blocks
    0x08  u32 object attribute (0, 1, 3, 8, 9, 0x108...; not interpreted)
    0x10  bbox_min x4 (w = 1) and 0x20 bbox_size x4 (w = 0)
    0x30  n_blocks consecutive blocks

Each block (offsets are relative to the start of the `VHD` record):

    +0x00  u16 n_tri, u16 n_plane, u32 n_vert
    +0x08  u32 vert_off, u32 plane_off, u32 end_off (where the next block starts)
    +0x20  bbox_min x4 and +0x30 bbox_size x4
    +0x40  u32 surface (0, 3, 7...; INFERRED: ground type), 12 bytes of 0
    +0x50  n_tri triangles of 4 u16: a, b, c and the triangle's plane
    plane_off  n_plane planes (nx, ny, nz, d) with n·p + d = 0
    vert_off   n_vert vertices (x, y, z, 1), already in stage space

Orientation: the normal of (b - a) × (c - a) points like the plane's in 164587 of the
164608 triangles (the rest are degenerate or nearly so), so (a, b, c) is already CCW seen
from the normal side, which is what glTF requires.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from typing import Final

from ngb_tools.stage.errors import StageFormatError
from ngb_tools.teamninja import TeamNinjaError, parse_chunk

HITMDL_TAG: Final = "HITMDL"
VHD_MAGIC: Final = b"VHD\0"
RECORD_HEADER_SIZE: Final = 0x30
BLOCK_HEADER_SIZE: Final = 0x50
TRIANGLE_SIZE: Final = 8
PLANE_SIZE: Final = 16
VERTEX_SIZE: Final = 16
_RECORD: Final = struct.Struct("<4sBBHI")
_BLOCK: Final = struct.Struct("<HHIIII")
_SURFACE: Final = struct.Struct("<I")
_TRIANGLE: Final = struct.Struct("<4H")
_VEC3: Final = struct.Struct("<3f")

type Vec3 = tuple[float, float, float]
type Triangle = tuple[int, int, int]


@dataclass(frozen=True, slots=True)
class CollisionMesh:
    """The collision of an object: vertices and triangles of all its blocks together.

    `surfaces[i]` is the surface field of the block of triangle i.
    """

    obj: int
    attribute: int
    vertices: tuple[Vec3, ...]
    triangles: tuple[Triangle, ...]
    surfaces: tuple[int, ...]


def _block(
    record: bytes, obj: int, block: int, pos: int, base: int
) -> tuple[list[Vec3], list[Triangle], int, int]:
    """Vertices, triangles (with `base` added), surface and end of a block."""
    where = f"HITMDL obj{obj} block {block}"
    if pos + BLOCK_HEADER_SIZE > len(record):
        raise StageFormatError(f"{where}: header truncated at {pos:#x}")
    n_tri, n_plane, n_vert, vert_off, plane_off, end_off = _BLOCK.unpack_from(record, pos)
    (surface,) = _SURFACE.unpack_from(record, pos + 0x40)
    tri_end = pos + BLOCK_HEADER_SIZE + TRIANGLE_SIZE * n_tri
    if not (
        tri_end <= plane_off
        and plane_off + PLANE_SIZE * n_plane <= vert_off
        and vert_off + VERTEX_SIZE * n_vert <= end_off <= len(record)
    ):
        raise StageFormatError(
            f"{where}: triangles, planes ({plane_off:#x}), vertices ({vert_off:#x}) and end "
            f"({end_off:#x}) do not fit in {len(record):#x} bytes"
        )
    vertices: list[Vec3] = []
    for i in range(n_vert):
        x, y, z = _VEC3.unpack_from(record, vert_off + VERTEX_SIZE * i)
        vertices.append((x, y, z))
    triangles: list[Triangle] = []
    for i in range(n_tri):
        a, b, c, plane = _TRIANGLE.unpack_from(record, pos + BLOCK_HEADER_SIZE + TRIANGLE_SIZE * i)
        if max(a, b, c) >= n_vert or plane >= n_plane:
            raise StageFormatError(f"{where}: triangle {i} out of range")
        triangles.append((a + base, b + base, c + base))
    return vertices, triangles, surface, end_off


def parse_collision_record(obj: int, record: bytes) -> CollisionMesh:
    """Parse the `VHD` record of object `obj` (the bytes of its slot)."""
    if len(record) < RECORD_HEADER_SIZE:
        raise StageFormatError(f"HITMDL obj{obj}: {len(record):#x} bytes, no header")
    magic, _, _, n_blocks, attribute = _RECORD.unpack_from(record)
    if magic != VHD_MAGIC:
        raise StageFormatError(f"HITMDL obj{obj}: magic {magic!r}, expected {VHD_MAGIC!r}")
    vertices: list[Vec3] = []
    triangles: list[Triangle] = []
    surfaces: list[int] = []
    pos = RECORD_HEADER_SIZE
    for block in range(n_blocks):
        verts, tris, surface, end = _block(record, obj, block, pos, len(vertices))
        if end <= pos:
            raise StageFormatError(f"HITMDL obj{obj} block {block}: does not advance ({end:#x})")
        vertices += verts
        triangles += tris
        surfaces += [surface] * len(tris)
        pos = end
    return CollisionMesh(obj, attribute, tuple(vertices), tuple(triangles), tuple(surfaces))


def parse_hitmdl(data: bytes, object_count: int) -> dict[int, CollisionMesh]:
    """Collision per object from the HITMDL subchunk that starts at `data[0]`."""
    try:
        chunk = parse_chunk(data)
    except TeamNinjaError as error:
        raise StageFormatError(f"HITMDL: {error}") from error
    if chunk.tag != HITMDL_TAG:
        raise StageFormatError(f"expected subchunk {HITMDL_TAG}, found '{chunk.tag}'")
    if chunk.n_slots != object_count:
        raise StageFormatError(
            f"HITMDL has {chunk.n_slots} slots but there are {object_count} objects"
        )
    return {
        slot.index: parse_collision_record(slot.index, data[slot.offset : slot.end])
        for slot in chunk.slots
    }
