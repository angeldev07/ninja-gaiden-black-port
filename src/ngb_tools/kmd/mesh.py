"""Descriptor de malla: el USERDATA de Team Ninja que acompaña a cada par VB/IB del XPR0.

El XPR0 de un KMD lleva un USERDATA de cabecera (`ModelHeader`) y luego uno por objeto, en
el orden de los objetos de OBJINFO (verificado: 59 en `ryu.kmd`, 63 en `r_ryu.kmd`). Todo
u32 little endian.

Cabecera del descriptor (`8 + 0x20 * n_parts` bytes; 0x28 con una parte):

    0x00  n_parts      partes: pares VB/IB del objeto, uno por FVF (casi siempre 1)
    0x04  n_sub        submallas de todas las partes
    0x08 + 0x20 * p    la parte p, 8 u32:
          +0x00 fvf          D3DFVF del VB (`ngb_tools.kmd.vertices`)
          +0x0C vertex_count
          +0x10 stride       bytes por vértice; tiene que cuadrar con el FVF
          +0x1C index_count  índices u16 del IB de la parte (las submallas que leen del IB)

Cada submalla son 0x60 bytes fijos, luego un material de 0x50 bytes si `+0x20` vale 1 y
luego `+0x08` etapas de textura de 0x40 bytes. Campos fijos conocidos:

    +0x00 índice   +0x04 parte   +0x08 n_tex
    +0x10 origen de los índices (2 = IB, 0 = push buffer)
    +0x14 inicio y +0x18 tamaño del rango de índices, en bytes
    +0x20 lleva material
    +0x28 alpha blend (INFERRED: 1 en las piezas aditivas o transparentes)
    +0x30 cull (INFERRED: 0x900 = D3DCULL_CW de Xbox; 0 = sin cull, doble cara)
    +0x38/+0x3C factores de blend origen/destino (valores NV2A: 0x302 SRC_ALPHA,
                0x303 ONE_MINUS_SRC_ALPHA, 1 ONE)

**Varias partes** (VERIFIED): en 462 objetos de los escenarios y en 15 de 10 personajes
(Doku, Aruma, el tanque, el camión con 4...). Cada parte lleva FVF propio (0x112 con normal
y 0x142 con color, típicamente) y las submallas leen de la parte que dice `+0x04` (en los
personajes de una parte vale 0). `parse_multipart_descriptor` reparte las submallas y
parsea cada parte como un descriptor de una parte con `parse_mesh_descriptor`.
`chr_doku#0` obj1: parte 0 FVF 0x112 y parte 1 FVF 0x012; antes de leerlo así, el
"origen de submalla 24" era la segunda parte de la cabecera leída como submalla.

Material: D3DMATERIAL8 (diffuse, ambient, specular, emissive en RGBA f32 y power) y 3 u32
sin interpretar. Etapa: 4 bytes con la cola del nombre de la textura (`dy_o`, `eye2`), el
índice i32 de la textura en el XPR0 (-1 = la asigna el juego en ejecución) y 14 u32 de
estados (el tercero parece el D3DTOP de Xbox: 4 MODULATE, 14 BLENDTEXTUREALPHA).
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from enum import IntEnum
from typing import Final

from ngb_tools.kmd.errors import KmdFormatError

DESCRIPTOR_HEADER_SIZE: Final = 0x28
SUBMESH_FIXED_SIZE: Final = 0x60
MATERIAL_SIZE: Final = 0x50
STAGE_SIZE: Final = 0x40
STAGE_STATE_WORDS: Final = 14
# Topes de cordura: lo máximo observado es 8 submallas y 3 etapas.
MAX_SUBMESHES: Final = 256
MAX_STAGES: Final = 8
CULL_NONE: Final = 0
MODEL_HEADER_WORDS: Final = 7
DESCRIPTOR_PREFIX_SIZE: Final = 8
PART_HEADER_SIZE: Final = 0x20
# Tope de partes por objeto: el máximo observado es 4 (`truck.kmd`, `zppln_*`).
MAX_PARTS: Final = 16
FIELD_PART: Final = 1
FIELD_STAGES: Final = 2
FIELD_MATERIAL: Final = 8

type Color4 = tuple[float, float, float, float]

_HEADER: Final = struct.Struct("<10I")
_FIXED: Final = struct.Struct("<24I")
_MATERIAL: Final = struct.Struct("<17f3I")
_STAGE: Final = struct.Struct(f"<4si{STAGE_STATE_WORDS}I")


class IndexSource(IntEnum):
    """Campo `+0x10` de la submalla: de dónde salen sus índices."""

    PUSH_BUFFER = 0
    INDEX_BUFFER = 2


@dataclass(frozen=True, slots=True)
class Material:
    """D3DMATERIAL8 de la submalla y los 3 u32 que le siguen."""

    diffuse: Color4
    ambient: Color4
    specular: Color4
    emissive: Color4
    power: float
    tail: tuple[int, int, int]


@dataclass(frozen=True, slots=True)
class TextureStage:
    """Una etapa de textura: cola del nombre, índice en el XPR0 (-1 = en ejecución) y estados."""

    name: str
    texture: int
    states: tuple[int, ...]


@dataclass(frozen=True, slots=True)
class Submesh:
    """Una submalla: rango de índices (en bytes), estado de render y material."""

    index: int
    source: IndexSource
    start: int
    size: int
    fixed: tuple[int, ...]
    material: Material | None
    stages: tuple[TextureStage, ...]

    @property
    def first_index(self) -> int:
        return self.start // 2

    @property
    def index_count(self) -> int:
        return self.size // 2

    @property
    def cull(self) -> int:
        return self.fixed[12]

    @property
    def double_sided(self) -> bool:
        return self.cull == CULL_NONE

    @property
    def alpha_blend(self) -> bool:
        return self.fixed[10] == 1

    @property
    def blend_factors(self) -> tuple[int, int]:
        return (self.fixed[14], self.fixed[15])

    @property
    def base_texture(self) -> int:
        """Textura de la primera etapa, o -1 si no hay etapas o la asigna el juego."""
        return self.stages[0].texture if self.stages else -1


@dataclass(frozen=True, slots=True)
class MeshDescriptor:
    """Cabecera y submallas de un descriptor."""

    fvf: int
    vertex_count: int
    stride: int
    index_count: int
    header: tuple[int, ...]
    submeshes: tuple[Submesh, ...]


@dataclass(frozen=True, slots=True)
class ModelHeader:
    """El primer USERDATA del XPR0 (7 u32): 63, texturas, VB, IB, push buffers y 2 más.

    El tercero cuenta VB, no objetos: con objetos de varias partes hay más VB que objetos
    (`boff_01.kmd`: (63, 98, 43, 42, 22) con 36 objetos; `doku.kmd`: 88 VB, 87 objetos).
    """

    words: tuple[int, ...]

    @property
    def texture_count(self) -> int:
        return self.words[1]

    @property
    def vertex_buffer_count(self) -> int:
        return self.words[2]


def parse_model_header(payload: bytes) -> ModelHeader:
    if len(payload) < 4 * MODEL_HEADER_WORDS:
        raise KmdFormatError(f"USERDATA de cabecera de {len(payload)} bytes, se esperaban 28")
    return ModelHeader(struct.unpack_from(f"<{MODEL_HEADER_WORDS}I", payload))


def _stage_name(raw: bytes) -> str:
    """Los 4 bytes del nombre, sin NUL y con lo no imprimible escapado."""
    text = raw.rstrip(b"\0")
    return "".join(chr(b) if 0x20 <= b < 0x7F else f"\\x{b:02x}" for b in text)


def _parse_material(payload: bytes, offset: int) -> Material:
    *floats, t0, t1, t2 = _MATERIAL.unpack_from(payload, offset)
    f = tuple(float(v) for v in floats)
    return Material(
        diffuse=(f[0], f[1], f[2], f[3]),
        ambient=(f[4], f[5], f[6], f[7]),
        specular=(f[8], f[9], f[10], f[11]),
        emissive=(f[12], f[13], f[14], f[15]),
        power=f[16],
        tail=(t0, t1, t2),
    )


def _parse_submesh(payload: bytes, ordinal: int, pos: int) -> tuple[Submesh, int]:
    if pos + SUBMESH_FIXED_SIZE > len(payload):
        raise KmdFormatError(f"submalla {ordinal}: registro fijo cortado en {pos:#x}")
    fixed = _FIXED.unpack_from(payload, pos)
    n_tex, source, start, size, has_material = fixed[2], fixed[4], fixed[5], fixed[6], fixed[8]
    if source not in (IndexSource.PUSH_BUFFER, IndexSource.INDEX_BUFFER):
        raise KmdFormatError(f"submalla {ordinal}: origen de índices desconocido {source}")
    if start % 2 or size % 2:
        raise KmdFormatError(f"submalla {ordinal}: rango [{start}, +{size}) no alineado a u16")
    if has_material not in (0, 1):
        raise KmdFormatError(f"submalla {ordinal}: campo de material {has_material}")
    if n_tex > MAX_STAGES:
        raise KmdFormatError(f"submalla {ordinal}: {n_tex} etapas supera el tope de {MAX_STAGES}")
    end = pos + SUBMESH_FIXED_SIZE + MATERIAL_SIZE * has_material + STAGE_SIZE * n_tex
    if end > len(payload):
        raise KmdFormatError(f"submalla {ordinal}: material y etapas no caben en el descriptor")
    cursor = pos + SUBMESH_FIXED_SIZE
    material = None
    if has_material:
        material = _parse_material(payload, cursor)
        cursor += MATERIAL_SIZE
    stages: list[TextureStage] = []
    for _ in range(n_tex):
        raw_name, texture, *states = _STAGE.unpack_from(payload, cursor)
        stages.append(TextureStage(_stage_name(raw_name), texture, tuple(states)))
        cursor += STAGE_SIZE
    submesh = Submesh(
        index=fixed[0],
        source=IndexSource(source),
        start=start,
        size=size,
        fixed=fixed,
        material=material,
        stages=tuple(stages),
    )
    return submesh, end


def parse_mesh_descriptor(payload: bytes) -> MeshDescriptor:
    """Parsea un descriptor; tiene que acabar exactamente al final del USERDATA."""
    if len(payload) < DESCRIPTOR_HEADER_SIZE:
        raise KmdFormatError(f"descriptor de {len(payload)} bytes, mínimo {DESCRIPTOR_HEADER_SIZE}")
    header = _HEADER.unpack_from(payload)
    n_sub, fvf, vertex_count, stride, index_count = (
        header[1],
        header[2],
        header[5],
        header[6],
        header[9],
    )
    if n_sub > MAX_SUBMESHES:
        raise KmdFormatError(f"descriptor con {n_sub} submallas supera el tope de {MAX_SUBMESHES}")
    pos = DESCRIPTOR_HEADER_SIZE
    submeshes: list[Submesh] = []
    for ordinal in range(n_sub):
        submesh, pos = _parse_submesh(payload, ordinal, pos)
        submeshes.append(submesh)
    if pos != len(payload):
        raise KmdFormatError(
            f"descriptor: las submallas acaban en {pos:#x} pero el USERDATA mide {len(payload):#x}"
        )
    return MeshDescriptor(
        fvf=fvf,
        vertex_count=vertex_count,
        stride=stride,
        index_count=index_count,
        header=header,
        submeshes=tuple(submeshes),
    )


@dataclass(frozen=True, slots=True)
class MultipartDescriptor:
    """Las partes de un objeto. `order[k]` = (parte, posición en la parte) de la submalla k.

    `header` son los u32 de la cabecera: `n_parts`, `n_sub` y los 8 de cada parte.
    """

    parts: tuple[MeshDescriptor, ...]
    order: tuple[tuple[int, int], ...]
    header: tuple[int, ...]

    @property
    def submesh_count(self) -> int:
        return len(self.order)

    def submesh(self, ordinal: int) -> Submesh:
        part, position = self.order[ordinal]
        return self.parts[part].submeshes[position]


_PREFIX: Final = struct.Struct("<2I")


def _record_extent(payload: bytes, ordinal: int, pos: int) -> tuple[int, int]:
    """Parte y fin del registro de la submalla que empieza en `pos`.

    Solo lee lo imprescindible para trocear (parte, etapas y material); el resto lo valida
    `parse_mesh_descriptor`.
    """
    if pos + SUBMESH_FIXED_SIZE > len(payload):
        raise KmdFormatError(f"submalla {ordinal}: registro fijo cortado en {pos:#x}")
    fixed = _FIXED.unpack_from(payload, pos)
    part, n_tex, has_material = fixed[FIELD_PART], fixed[FIELD_STAGES], fixed[FIELD_MATERIAL]
    if has_material not in (0, 1) or n_tex > MAX_STAGES:
        raise KmdFormatError(
            f"submalla {ordinal}: material {has_material} o {n_tex} etapas inválidos"
        )
    end = pos + SUBMESH_FIXED_SIZE + MATERIAL_SIZE * has_material + STAGE_SIZE * n_tex
    if end > len(payload):
        raise KmdFormatError(f"submalla {ordinal}: material y etapas no caben en el descriptor")
    return part, end


def parse_multipart_descriptor(payload: bytes) -> MultipartDescriptor:
    """Parsea un descriptor de una o varias partes; tiene que ocupar todo el USERDATA.

    Con una parte, `parts[0]` es exactamente lo que da `parse_mesh_descriptor(payload)`.
    Con 0 partes es un objeto vacío, sin VB (`rail_cmn` `ra_door1.kmd` obj1).
    """
    if len(payload) < DESCRIPTOR_PREFIX_SIZE:
        raise KmdFormatError(f"descriptor de {len(payload)} bytes, mínimo {DESCRIPTOR_PREFIX_SIZE}")
    n_parts, n_sub = _PREFIX.unpack_from(payload)
    if n_parts > MAX_PARTS:
        raise KmdFormatError(f"descriptor con {n_parts} partes supera el tope de {MAX_PARTS}")
    if n_sub > MAX_SUBMESHES:
        raise KmdFormatError(f"descriptor con {n_sub} submallas supera el tope de {MAX_SUBMESHES}")
    pos = DESCRIPTOR_PREFIX_SIZE + PART_HEADER_SIZE * n_parts
    if pos > len(payload):
        raise KmdFormatError(f"cabecera de {n_parts} partes cortada ({len(payload):#x} bytes)")
    header = struct.unpack_from(f"<{pos // 4}I", payload)
    records: list[list[bytes]] = [[] for _ in range(n_parts)]
    order: list[tuple[int, int]] = []
    for ordinal in range(n_sub):
        part, end = _record_extent(payload, ordinal, pos)
        if part >= n_parts:
            raise KmdFormatError(f"submalla {ordinal}: parte {part}, hay {n_parts}")
        order.append((part, len(records[part])))
        records[part].append(payload[pos:end])
        pos = end
    if pos != len(payload):
        raise KmdFormatError(
            f"descriptor: las submallas acaban en {pos:#x} pero el USERDATA mide {len(payload):#x}"
        )
    parts = []
    for p, chunks in enumerate(records):
        start = DESCRIPTOR_PREFIX_SIZE + PART_HEADER_SIZE * p
        single = (
            _PREFIX.pack(1, len(chunks))
            + payload[start : start + PART_HEADER_SIZE]
            + b"".join(chunks)
        )
        parts.append(parse_mesh_descriptor(single))
    return MultipartDescriptor(tuple(parts), tuple(order), header)
