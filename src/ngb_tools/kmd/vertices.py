"""Vértices según el FVF (Flexible Vertex Format) de Direct3D 8 de Xbox.

Los VB de un KMD no traen declaración de vértice: el formato es el FVF del descriptor de
malla. Los bits son los de D3D8 (Cxbx-Reloaded, `XbD3D8Types.h`: `X_D3DFVF_*`) y los datos
van intercalados en este orden:

    posición    XYZ 0x002 (3 f32) o XYZB1-B3 0x006/0x008/0x00A (3 f32 + 1-3 pesos f32)
    0x010 NORMAL    3 f32 (en los KMD vistos, siempre unitarias: nada de NORMPACKED3)
    0x020 PSIZE     1 f32
    0x040 DIFFUSE   D3DCOLOR (u32 ARGB)
    0x080 SPECULAR  D3DCOLOR
    bits 8-11       número de juegos de coordenadas de textura; bits 16+2i, tamaño del juego i
                    (0 = 2 f32, 1 = 3, 2 = 4, 3 = 1)

Observado en `ryu.kmd`: 0x112 (stride 32), 0x116 (36, un peso de mezcla) y 0x042 (16).
XYZRHW (ya transformado) y cualquier bit desconocido se rechazan.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from typing import Final

from ngb_tools.kmd.errors import KmdFormatError, UnsupportedVertexFormatError

FVF_POSITION_MASK: Final = 0x00E
FVF_XYZ: Final = 0x002
FVF_XYZRHW: Final = 0x004
FVF_NORMAL: Final = 0x010
FVF_PSIZE: Final = 0x020
FVF_DIFFUSE: Final = 0x040
FVF_SPECULAR: Final = 0x080
FVF_TEXCOUNT_MASK: Final = 0xF00
FVF_TEXCOUNT_SHIFT: Final = 8
_BLEND_WEIGHTS: Final = {FVF_XYZ: 0, 0x006: 1, 0x008: 2, 0x00A: 3}
_TEXCOORD_FLOATS: Final = (2, 3, 4, 1)

type Vec2 = tuple[float, float]
type Vec3 = tuple[float, float, float]


@dataclass(frozen=True, slots=True)
class VertexLayout:
    """Disposición de un vértice: offsets en bytes de cada atributo (None = ausente)."""

    fvf: int
    stride: int
    weight_count: int
    normal: int | None
    diffuse: int | None
    texcoord: int | None
    texcoord_floats: int


def vertex_layout(fvf: int) -> VertexLayout:
    """Disposición del FVF, o `UnsupportedVertexFormatError` si no se sabe leer."""
    position = fvf & FVF_POSITION_MASK
    if position == FVF_XYZRHW:
        raise UnsupportedVertexFormatError(f"FVF {fvf:#x}: posición ya transformada (XYZRHW)")
    if position not in _BLEND_WEIGHTS:
        raise UnsupportedVertexFormatError(f"FVF {fvf:#x}: posición {position:#x} no admitida")
    tex_count = (fvf & FVF_TEXCOUNT_MASK) >> FVF_TEXCOUNT_SHIFT
    known = FVF_POSITION_MASK | FVF_NORMAL | FVF_PSIZE | FVF_DIFFUSE | FVF_SPECULAR
    known |= FVF_TEXCOUNT_MASK
    for i in range(tex_count):
        known |= 0x3 << (16 + 2 * i)
    if fvf & ~known:
        raise UnsupportedVertexFormatError(f"FVF {fvf:#x}: bits desconocidos {fvf & ~known:#x}")
    weight_count = _BLEND_WEIGHTS[position]
    offset = 12 + 4 * weight_count
    normal = diffuse = texcoord = None
    if fvf & FVF_NORMAL:
        normal, offset = offset, offset + 12
    if fvf & FVF_PSIZE:
        offset += 4
    if fvf & FVF_DIFFUSE:
        diffuse, offset = offset, offset + 4
    if fvf & FVF_SPECULAR:
        offset += 4
    texcoord_floats = 0
    for i in range(tex_count):
        floats = _TEXCOORD_FLOATS[(fvf >> (16 + 2 * i)) & 0x3]
        if i == 0:
            texcoord, texcoord_floats = offset, floats
        offset += 4 * floats
    return VertexLayout(fvf, offset, weight_count, normal, diffuse, texcoord, texcoord_floats)


@dataclass(frozen=True, slots=True)
class VertexData:
    """Atributos decodificados; los ausentes en el FVF son None.

    `weights` es el primer peso de mezcla (XYZB1); `colors`, D3DCOLOR en u32 ARGB; `uvs`, el
    primer juego de coordenadas (sus dos primeras componentes).
    """

    positions: tuple[Vec3, ...]
    normals: tuple[Vec3, ...] | None
    uvs: tuple[Vec2, ...] | None
    weights: tuple[float, ...] | None
    colors: tuple[int, ...] | None

    def __len__(self) -> int:
        return len(self.positions)


def _column3(data: bytes, offset: int, stride: int, count: int) -> tuple[Vec3, ...]:
    unpack = struct.Struct("<3f").unpack_from
    out: list[Vec3] = []
    for i in range(count):
        x, y, z = unpack(data, i * stride + offset)
        out.append((x, y, z))
    return tuple(out)


def decode_vertices(data: bytes, layout: VertexLayout, count: int) -> VertexData:
    """Decodifica `count` vértices de `data` (al menos `count * stride` bytes)."""
    stride = layout.stride
    if len(data) < count * stride:
        raise KmdFormatError(f"VB de {len(data)} bytes para {count} vértices de {stride}")
    positions = _column3(data, 0, stride, count)
    normals = None if layout.normal is None else _column3(data, layout.normal, stride, count)
    uvs: tuple[Vec2, ...] | None = None
    if layout.texcoord is not None:
        unpack2 = struct.Struct("<2f" if layout.texcoord_floats >= 2 else "<f").unpack_from
        uv_list: list[Vec2] = []
        for i in range(count):
            values = unpack2(data, i * stride + layout.texcoord)
            uv_list.append((values[0], values[1] if len(values) > 1 else 0.0))
        uvs = tuple(uv_list)
    weights = None
    if layout.weight_count:
        unpack_f = struct.Struct("<f").unpack_from
        weights = tuple(unpack_f(data, i * stride + 12)[0] for i in range(count))
    colors = None
    if layout.diffuse is not None:
        unpack_u = struct.Struct("<I").unpack_from
        colors = tuple(unpack_u(data, i * stride + layout.diffuse)[0] for i in range(count))
    return VertexData(positions, normals, uvs, weights, colors)
