"""Mesh descriptor: the Team Ninja USERDATA that accompanies each VB/IB pair of the XPR0.

The XPR0 of a KMD carries a header USERDATA (`ModelHeader`) and then one per object, in
the order of the OBJINFO objects (verified: 59 in `ryu.kmd`, 63 in `r_ryu.kmd`). Everything
is little-endian u32.

Descriptor header (`8 + 0x20 * n_parts` bytes; 0x28 with one part):

    0x00  n_parts      parts: VB/IB pairs of the object, one per FVF (almost always 1)
    0x04  n_sub        submeshes across all the parts
    0x08 + 0x20 * p    part p, 8 u32:
          +0x00 fvf          D3DFVF of the VB (`ngb_tools.kmd.vertices`)
          +0x0C vertex_count
          +0x10 stride       bytes per vertex; must match the FVF
          +0x1C index_count  u16 indices of the part's IB (the submeshes that read from the IB)

Each submesh is a fixed 0x60 bytes, then a 0x50-byte material if `+0x20` is 1, and then
`+0x08` texture stages of 0x40 bytes. Known fixed fields:

    +0x00 index   +0x04 part   +0x08 n_tex
    +0x10 index source (2 = IB, 0 = push buffer)
    +0x14 start and +0x18 size of the index range, in bytes
    +0x20 has material
    +0x28 alpha blend (INFERRED: 1 on additive or transparent pieces)
    +0x30 cull (INFERRED: 0x900 = Xbox D3DCULL_CW; 0 = no cull, double sided)
    +0x38/+0x3C source/destination blend factors (NV2A values: 0x302 SRC_ALPHA,
                0x303 ONE_MINUS_SRC_ALPHA, 1 ONE)

**Several parts** (VERIFIED): in 462 stage objects and in 15 objects of 10 characters
(Doku, Aruma, the tank, the truck with 4...). Each part has its own FVF (typically 0x112
with normal and 0x142 with color) and the submeshes read from the part that `+0x04` says
(in single-part characters it is 0). `parse_multipart_descriptor` distributes the submeshes
and parses each part as a single-part descriptor with `parse_mesh_descriptor`.
`chr_doku#0` obj1: part 0 FVF 0x112 and part 1 FVF 0x012; before reading it this way, the
"submesh 24 source" was the second part of the header read as a submesh.

Material: D3DMATERIAL8 (diffuse, ambient, specular, emissive as RGBA f32 and power) and 3
uninterpreted u32. Stage: 4 bytes with the tail of the texture name (`dy_o`, `eye2`), the
i32 index of the texture in the XPR0 (-1 = assigned by the game at runtime) and 14 u32 of
states (the third looks like the Xbox D3DTOP: 4 MODULATE, 14 BLENDTEXTUREALPHA).
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
# Sanity caps: the maximum observed is 8 submeshes and 3 stages.
MAX_SUBMESHES: Final = 256
MAX_STAGES: Final = 8
CULL_NONE: Final = 0
MODEL_HEADER_WORDS: Final = 7
DESCRIPTOR_PREFIX_SIZE: Final = 8
PART_HEADER_SIZE: Final = 0x20
# Cap on parts per object: the maximum observed is 4 (`truck.kmd`, `zppln_*`).
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
    """Submesh field `+0x10`: where its indices come from."""

    PUSH_BUFFER = 0
    INDEX_BUFFER = 2


@dataclass(frozen=True, slots=True)
class Material:
    """The submesh D3DMATERIAL8 and the 3 u32 that follow it."""

    diffuse: Color4
    ambient: Color4
    specular: Color4
    emissive: Color4
    power: float
    tail: tuple[int, int, int]


@dataclass(frozen=True, slots=True)
class TextureStage:
    """A texture stage: name tail, XPR0 index (-1 = at runtime) and states."""

    name: str
    texture: int
    states: tuple[int, ...]


@dataclass(frozen=True, slots=True)
class Submesh:
    """A submesh: index range (in bytes), render state and material."""

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
        """Texture of the first stage, or -1 if there are no stages or the game assigns it."""
        return self.stages[0].texture if self.stages else -1


@dataclass(frozen=True, slots=True)
class MeshDescriptor:
    """Header and submeshes of a descriptor."""

    fvf: int
    vertex_count: int
    stride: int
    index_count: int
    header: tuple[int, ...]
    submeshes: tuple[Submesh, ...]


@dataclass(frozen=True, slots=True)
class ModelHeader:
    """The first USERDATA of the XPR0 (7 u32): 63, textures, VB, IB, push buffers and 2 more.

    The third counts VBs, not objects: with multi-part objects there are more VBs than
    objects (`boff_01.kmd`: (63, 98, 43, 42, 22) with 36 objects; `doku.kmd`: 88 VBs, 87
    objects).
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
        raise KmdFormatError(f"header USERDATA of {len(payload)} bytes, expected 28")
    return ModelHeader(struct.unpack_from(f"<{MODEL_HEADER_WORDS}I", payload))


def _stage_name(raw: bytes) -> str:
    """The 4 name bytes, without NULs and with non-printable bytes escaped."""
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
        raise KmdFormatError(f"submesh {ordinal}: fixed record truncated at {pos:#x}")
    fixed = _FIXED.unpack_from(payload, pos)
    n_tex, source, start, size, has_material = fixed[2], fixed[4], fixed[5], fixed[6], fixed[8]
    if source not in (IndexSource.PUSH_BUFFER, IndexSource.INDEX_BUFFER):
        raise KmdFormatError(f"submesh {ordinal}: unknown index source {source}")
    if start % 2 or size % 2:
        raise KmdFormatError(f"submesh {ordinal}: range [{start}, +{size}) not u16-aligned")
    if has_material not in (0, 1):
        raise KmdFormatError(f"submesh {ordinal}: material field {has_material}")
    if n_tex > MAX_STAGES:
        raise KmdFormatError(f"submesh {ordinal}: {n_tex} stages exceeds the cap of {MAX_STAGES}")
    end = pos + SUBMESH_FIXED_SIZE + MATERIAL_SIZE * has_material + STAGE_SIZE * n_tex
    if end > len(payload):
        raise KmdFormatError(f"submesh {ordinal}: material and stages do not fit in the descriptor")
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
    """Parse a descriptor; it must end exactly at the end of the USERDATA."""
    if len(payload) < DESCRIPTOR_HEADER_SIZE:
        raise KmdFormatError(
            f"descriptor of {len(payload)} bytes, minimum {DESCRIPTOR_HEADER_SIZE}"
        )
    header = _HEADER.unpack_from(payload)
    n_sub, fvf, vertex_count, stride, index_count = (
        header[1],
        header[2],
        header[5],
        header[6],
        header[9],
    )
    if n_sub > MAX_SUBMESHES:
        raise KmdFormatError(
            f"descriptor with {n_sub} submeshes exceeds the cap of {MAX_SUBMESHES}"
        )
    pos = DESCRIPTOR_HEADER_SIZE
    submeshes: list[Submesh] = []
    for ordinal in range(n_sub):
        submesh, pos = _parse_submesh(payload, ordinal, pos)
        submeshes.append(submesh)
    if pos != len(payload):
        raise KmdFormatError(
            f"descriptor: the submeshes end at {pos:#x} but the USERDATA is {len(payload):#x}"
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
    """The parts of an object. `order[k]` = (part, position within the part) of submesh k.

    `header` is the header u32s: `n_parts`, `n_sub` and the 8 of each part.
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
    """Part and end of the submesh record that starts at `pos`.

    Reads only what is strictly needed to slice (part, stages and material); the rest is
    validated by `parse_mesh_descriptor`.
    """
    if pos + SUBMESH_FIXED_SIZE > len(payload):
        raise KmdFormatError(f"submesh {ordinal}: fixed record truncated at {pos:#x}")
    fixed = _FIXED.unpack_from(payload, pos)
    part, n_tex, has_material = fixed[FIELD_PART], fixed[FIELD_STAGES], fixed[FIELD_MATERIAL]
    if has_material not in (0, 1) or n_tex > MAX_STAGES:
        raise KmdFormatError(
            f"submesh {ordinal}: invalid material {has_material} or {n_tex} stages"
        )
    end = pos + SUBMESH_FIXED_SIZE + MATERIAL_SIZE * has_material + STAGE_SIZE * n_tex
    if end > len(payload):
        raise KmdFormatError(f"submesh {ordinal}: material and stages do not fit in the descriptor")
    return part, end


def parse_multipart_descriptor(payload: bytes) -> MultipartDescriptor:
    """Parse a one-part or multi-part descriptor; it must fill the whole USERDATA.

    With one part, `parts[0]` is exactly what `parse_mesh_descriptor(payload)` returns.
    With 0 parts it is an empty object, without VBs (`rail_cmn` `ra_door1.kmd` obj1).
    """
    if len(payload) < DESCRIPTOR_PREFIX_SIZE:
        raise KmdFormatError(
            f"descriptor of {len(payload)} bytes, minimum {DESCRIPTOR_PREFIX_SIZE}"
        )
    n_parts, n_sub = _PREFIX.unpack_from(payload)
    if n_parts > MAX_PARTS:
        raise KmdFormatError(f"descriptor with {n_parts} parts exceeds the cap of {MAX_PARTS}")
    if n_sub > MAX_SUBMESHES:
        raise KmdFormatError(
            f"descriptor with {n_sub} submeshes exceeds the cap of {MAX_SUBMESHES}"
        )
    pos = DESCRIPTOR_PREFIX_SIZE + PART_HEADER_SIZE * n_parts
    if pos > len(payload):
        raise KmdFormatError(f"header of {n_parts} parts truncated ({len(payload):#x} bytes)")
    header = struct.unpack_from(f"<{pos // 4}I", payload)
    records: list[list[bytes]] = [[] for _ in range(n_parts)]
    order: list[tuple[int, int]] = []
    for ordinal in range(n_sub):
        part, end = _record_extent(payload, ordinal, pos)
        if part >= n_parts:
            raise KmdFormatError(f"submesh {ordinal}: part {part}, but there are {n_parts}")
        order.append((part, len(records[part])))
        records[part].append(payload[pos:end])
        pos = end
    if pos != len(payload):
        raise KmdFormatError(
            f"descriptor: the submeshes end at {pos:#x} but the USERDATA is {len(payload):#x}"
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
