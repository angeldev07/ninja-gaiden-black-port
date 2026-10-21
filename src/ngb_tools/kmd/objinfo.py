"""OBJINFO: una entrada por objeto del modelo, con su transformación local y sus volúmenes.

El subchunk OBJINFO lleva la cabecera de chunk de Team Ninja (`ngb_tools.teamninja`) y un
slot por objeto. Cada entrada (u32 y f32 little endian, verificado en `ryu.kmd`):

    0x00  "OI\\x01\\x01"   magia
    0x04  header_size    0x90: donde empiezan los registros por submalla
    0x08  n_sub          submallas; igual que en el descriptor de malla del XPR0
    0x0C  group          1 detalle, 2 sombra, 3 efecto, 4 skin, 5 LOD (nombres INFERRED)
    0x10  flags          0x201 / 0xa01 / 0x200 (sin interpretar)
    0x18  f32 1.0
    0x30  T  f32 x3      traslación local respecto al nodo padre de HIERARC
    0x40  R  f32 x3      rotación: 0 en todos los objetos observados, orden desconocido
    0x50  S  f32 x3      escala: 1, o 0 en los objetos de efecto ocultos
    0x60  bbox_min x3    coincide con el mínimo de los vértices
    0x70  bbox_size x3   máximo = min + size
    0x80  esfera x4      centro y radio
    header_size + 0x20 * i: u32 flags, 12 bytes de relleno y la esfera de la submalla i

OBJINFO no apunta a buffers: el objeto k usa el descriptor, el VB y el IB k-ésimos del XPR0.
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
# Tope de submallas por objeto: el máximo observado es 8 (la cabeza de Ryu).
MAX_SUBMESHES: Final = 256

type Vec3 = tuple[float, float, float]
type Sphere = tuple[float, float, float, float]

_HEAD: Final = struct.Struct("<4sIIII")
_VEC3: Final = struct.Struct("<3f")
_SPHERE: Final = struct.Struct("<4f")


class ObjectGroup(IntEnum):
    """Campo 0x0C. Los nombres son INFERRED por lo que se ve al renderizar cada grupo."""

    DETAIL = 1
    SHADOW = 2
    EFFECT = 3
    SKIN = 4
    LOD = 5


def group_name(group: int) -> str:
    try:
        return ObjectGroup(group).name.lower()
    except ValueError:
        return f"grupo{group}"


@dataclass(frozen=True, slots=True)
class ObjectInfo:
    """Una entrada de OBJINFO."""

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
    """Parsea una entrada; `entry` son los bytes de su slot."""
    if len(entry) < MIN_HEADER_SIZE:
        raise KmdFormatError(f"OBJINFO {index}: {len(entry):#x} bytes, mínimo {MIN_HEADER_SIZE:#x}")
    magic, header_size, n_sub, group, flags = _HEAD.unpack_from(entry)
    if magic != ENTRY_MAGIC:
        raise KmdFormatError(f"OBJINFO {index}: magia {magic!r}, se esperaba {ENTRY_MAGIC!r}")
    if n_sub > MAX_SUBMESHES:
        raise KmdFormatError(
            f"OBJINFO {index}: {n_sub} submallas supera el tope de {MAX_SUBMESHES}"
        )
    end = header_size + SUBMESH_RECORD_SIZE * n_sub
    if header_size < MIN_HEADER_SIZE or end > len(entry):
        raise KmdFormatError(
            f"OBJINFO {index}: cabecera {header_size:#x} y {n_sub} submallas no caben en "
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
    """Todas las entradas del subchunk OBJINFO que empieza en `data[0]`.

    Cada slot es un objeto, así que no puede haber slots vacíos: el índice del slot es el
    índice del objeto en el resto del KMD.
    """
    try:
        chunk = parse_chunk(data)
    except TeamNinjaError as error:
        raise KmdFormatError(f"OBJINFO: {error}") from error
    if chunk.tag != OBJINFO_TAG:
        raise KmdFormatError(f"se esperaba el subchunk {OBJINFO_TAG}, hay '{chunk.tag}'")
    if chunk.n_used != chunk.n_slots:
        raise KmdFormatError(
            f"OBJINFO: {chunk.n_slots - chunk.n_used} slots vacíos; cada slot es un objeto"
        )
    return tuple(
        parse_object_entry(slot.index, data[slot.offset : slot.end]) for slot in chunk.slots
    )
