"""HITMDL: la malla de colisión de un KMD, un registro `VHD` por objeto.

El subchunk HITMDL lleva la cabecera de chunk de Team Ninja con un slot por objeto (el slot
k es el objeto k; vacío = sin colisión). Cada registro, en u32/f32 little endian (VERIFIED
con un script del analista en los 323 KMD con HITMDL del juego: 1929 registros y 25876
bloques, todos coherentes):

    0x00  "VHD\\0"
    0x04  u8 1, u8 0x0a, u16 n_blocks
    0x08  u32 atributo del objeto (0, 1, 3, 8, 9, 0x108...; sin interpretar)
    0x10  bbox_min x4 (w = 1) y 0x20 bbox_size x4 (w = 0)
    0x30  n_blocks bloques seguidos

Cada bloque (los offsets son relativos al inicio del registro `VHD`):

    +0x00  u16 n_tri, u16 n_plane, u32 n_vert
    +0x08  u32 vert_off, u32 plane_off, u32 end_off (donde empieza el bloque siguiente)
    +0x20  bbox_min x4 y +0x30 bbox_size x4
    +0x40  u32 superficie (0, 3, 7...; INFERRED: tipo de suelo), 12 bytes a 0
    +0x50  n_tri triángulos de 4 u16: a, b, c y el plano del triángulo
    plane_off  n_plane planos (nx, ny, nz, d) con n·p + d = 0
    vert_off   n_vert vértices (x, y, z, 1), ya en el espacio del escenario

Orientación: la normal de (b - a) × (c - a) apunta como la del plano en 164587 de los
164608 triángulos (el resto, degenerados o casi), así que (a, b, c) ya es CCW visto desde
el lado de la normal, lo que pide glTF.
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
    """La colisión de un objeto: vértices y triángulos de todos sus bloques juntos.

    `surfaces[i]` es el campo de superficie del bloque del triángulo i.
    """

    obj: int
    attribute: int
    vertices: tuple[Vec3, ...]
    triangles: tuple[Triangle, ...]
    surfaces: tuple[int, ...]


def _block(
    record: bytes, obj: int, block: int, pos: int, base: int
) -> tuple[list[Vec3], list[Triangle], int, int]:
    """Vértices, triángulos (con `base` sumado), superficie y fin de un bloque."""
    where = f"HITMDL obj{obj} bloque {block}"
    if pos + BLOCK_HEADER_SIZE > len(record):
        raise StageFormatError(f"{where}: cabecera cortada en {pos:#x}")
    n_tri, n_plane, n_vert, vert_off, plane_off, end_off = _BLOCK.unpack_from(record, pos)
    (surface,) = _SURFACE.unpack_from(record, pos + 0x40)
    tri_end = pos + BLOCK_HEADER_SIZE + TRIANGLE_SIZE * n_tri
    if not (
        tri_end <= plane_off
        and plane_off + PLANE_SIZE * n_plane <= vert_off
        and vert_off + VERTEX_SIZE * n_vert <= end_off <= len(record)
    ):
        raise StageFormatError(
            f"{where}: triángulos, planos ({plane_off:#x}), vértices ({vert_off:#x}) y fin "
            f"({end_off:#x}) no encajan en {len(record):#x} bytes"
        )
    vertices: list[Vec3] = []
    for i in range(n_vert):
        x, y, z = _VEC3.unpack_from(record, vert_off + VERTEX_SIZE * i)
        vertices.append((x, y, z))
    triangles: list[Triangle] = []
    for i in range(n_tri):
        a, b, c, plane = _TRIANGLE.unpack_from(record, pos + BLOCK_HEADER_SIZE + TRIANGLE_SIZE * i)
        if max(a, b, c) >= n_vert or plane >= n_plane:
            raise StageFormatError(f"{where}: triángulo {i} fuera de rango")
        triangles.append((a + base, b + base, c + base))
    return vertices, triangles, surface, end_off


def parse_collision_record(obj: int, record: bytes) -> CollisionMesh:
    """Parsea el registro `VHD` del objeto `obj` (los bytes de su slot)."""
    if len(record) < RECORD_HEADER_SIZE:
        raise StageFormatError(f"HITMDL obj{obj}: {len(record):#x} bytes, sin cabecera")
    magic, _, _, n_blocks, attribute = _RECORD.unpack_from(record)
    if magic != VHD_MAGIC:
        raise StageFormatError(f"HITMDL obj{obj}: magia {magic!r}, se esperaba {VHD_MAGIC!r}")
    vertices: list[Vec3] = []
    triangles: list[Triangle] = []
    surfaces: list[int] = []
    pos = RECORD_HEADER_SIZE
    for block in range(n_blocks):
        verts, tris, surface, end = _block(record, obj, block, pos, len(vertices))
        if end <= pos:
            raise StageFormatError(f"HITMDL obj{obj} bloque {block}: no avanza ({end:#x})")
        vertices += verts
        triangles += tris
        surfaces += [surface] * len(tris)
        pos = end
    return CollisionMesh(obj, attribute, tuple(vertices), tuple(triangles), tuple(surfaces))


def parse_hitmdl(data: bytes, object_count: int) -> dict[int, CollisionMesh]:
    """Colisión por objeto del subchunk HITMDL que empieza en `data[0]`."""
    try:
        chunk = parse_chunk(data)
    except TeamNinjaError as error:
        raise StageFormatError(f"HITMDL: {error}") from error
    if chunk.tag != HITMDL_TAG:
        raise StageFormatError(f"se esperaba el subchunk {HITMDL_TAG}, hay '{chunk.tag}'")
    if chunk.n_slots != object_count:
        raise StageFormatError(f"HITMDL tiene {chunk.n_slots} slots y hay {object_count} objetos")
    return {
        slot.index: parse_collision_record(slot.index, data[slot.offset : slot.end])
        for slot in chunk.slots
    }
