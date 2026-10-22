"""Builder of synthetic stage KMDs: game data is never used in the tests.

It reproduces what was observed in stages (`ngb_tools.stage.model`): objects with several
parts (one VB/IB pair per part, FVF 0x142 with vertex color), S = (0, 0, 0), group 0, no
HIERARC, push buffers after the buffers of their object and HITMDL with one `VHD` record
per object.
"""

from __future__ import annotations

import struct
from collections.abc import Sequence
from dataclasses import dataclass, field

from teamninja.synthetic import build_chunk
from xpr.synthetic import (
    INDEX_COMMON,
    VERTEX_COMMON,
    buffer_header,
    build_xpr,
    format_word,
    push_header,
    solid_dxt1_block,
    texture_header,
    userdata,
)

DXT1 = 0x0C
FVF_LIT = 0x142  # XYZ | DIFFUSE | TEX1, stride 24
STRIDE_LIT = 24
COLOR = 0xFF406080  # D3DCOLOR ARGB

type Vec3 = tuple[float, float, float]
type Triangle = tuple[int, int, int]

QUAD: tuple[Vec3, ...] = ((0, 0, 0), (1, 0, 0), (0, 0, 1), (1, 0, 1))
QUAD_STRIP = (0, 1, 2, 3)


@dataclass
class SynthPart:
    """A part with one submesh: a quad (shifted by `offset`) and its strip."""

    offset: Vec3 = (0.0, 0.0, 0.0)
    strip: Sequence[int] = QUAD_STRIP
    push_buffer: bool = False
    texture: int = 0
    pb_garbage: int = 0

    def positions(self) -> list[Vec3]:
        ox, oy, oz = self.offset
        return [(x + ox, y + oy, z + oz) for x, y, z in QUAD]


@dataclass
class SynthStageObject:
    parts: Sequence[SynthPart] = field(default_factory=lambda: [SynthPart()])
    order: Sequence[int] | None = None  # submesh order (one per part)
    scale: Vec3 = (0.0, 0.0, 0.0)
    group: int = 0
    collision: Sequence[tuple[Sequence[Vec3], Sequence[Triangle]]] = ()


def vertex_bytes(part: SynthPart) -> bytes:
    out = bytearray()
    for (x, y, z), (u, v) in zip(part.positions(), ((0, 0), (1, 0), (0, 1), (1, 1)), strict=True):
        out += struct.pack("<3fI2f", x, y, z, COLOR, u, v)
    return bytes(out)


def push_buffer_bytes(indices: Sequence[int], garbage: int = 0) -> bytes:
    """BEGIN(strip), ARRAY_ELEMENT16, ARRAY_ELEMENT32 if one is left, END and the padding u32."""
    words = [0x000417FC, 6]
    pairs = [indices[i] | (indices[i + 1] << 16) for i in range(0, len(indices) - 1, 2)]
    if pairs:
        words += [0x40001800 | (len(pairs) << 18), *pairs]
    if len(indices) % 2:
        words += [0x00041808, indices[-1]]
    words += [0x000417FC, 0, garbage]
    return struct.pack(f"<{len(words)}I", *words)


def submesh_record(ordinal: int, part: int, synth: SynthPart) -> bytes:
    size = 2 * len(synth.strip)
    source = 0 if synth.push_buffer else 2
    fixed = [ordinal, part, 1, 0, source, 0, size, 1, 0, 1, 0, 1, 0x900, 0, 0x302, 0x303, 1]
    fixed += [0] * 7
    stage = struct.pack(
        "<4si14I", b"t_PP", synth.texture, 1, 0, 4, 1, 2, 1, 3, 1, 2, 1, 0xBFC00000, 0, 0, 0
    )
    return struct.pack("<24I", *fixed) + stage


def descriptor(obj: SynthStageObject) -> bytes:
    order = list(range(len(obj.parts))) if obj.order is None else list(obj.order)
    out = bytearray(struct.pack("<2I", len(obj.parts), len(order)))
    for part in obj.parts:
        ib = 0 if part.push_buffer else len(part.strip)
        out += struct.pack("<8I", FVF_LIT, 0, 0, len(QUAD), STRIDE_LIT, 1, 0, ib)
    for ordinal, p in enumerate(order):
        out += submesh_record(ordinal, p, obj.parts[p])
    return bytes(out)


def objinfo_entry(obj: SynthStageObject) -> bytes:
    n_sub = len(obj.parts) if obj.order is None else len(obj.order)
    head = struct.pack("<4sIIIIIfI", b"OI\x01\x01", 0x90, n_sub, obj.group, 0x201, 0, 1.0, 0)
    vectors = b"".join(
        struct.pack("<3fI", *v, 0) for v in ((0, 0, 0), (0, 0, 0), obj.scale, (0, 0, 0), (1, 0, 1))
    )
    sphere = struct.pack("<4f", 0.5, 0.0, 0.5, 0.8)
    submeshes = struct.pack("<I12x4f", 0x201, 0.5, 0.0, 0.5, 0.8) * n_sub
    return head + bytes(0x10) + vectors + sphere + submeshes


def vhd_record(blocks: Sequence[tuple[Sequence[Vec3], Sequence[Triangle]]], attr: int = 0) -> bytes:
    """`VHD` record with one block per element; planes (0, 1, 0, -y) and surface = block."""
    head = struct.pack("<4sBBHI", b"VHD\0", 1, 0x0A, len(blocks), attr) + bytes(4)
    head += struct.pack("<8f", 0, 0, 0, 1, 1, 1, 1, 0)
    body = bytearray(head)
    for surface, (vertices, triangles) in enumerate(blocks):
        start = len(body)
        plane_off = start + 0x50 + 8 * len(triangles)
        vert_off = plane_off + 16 * len(triangles)
        end = vert_off + 16 * len(vertices)
        n_tri, n_vert = len(triangles), len(vertices)
        body += struct.pack("<HHIIII", n_tri, n_tri, n_vert, vert_off, plane_off, end) + bytes(12)
        body += struct.pack("<8f", 0, 0, 0, 0, 1, 1, 1, 0)
        body += struct.pack("<I", surface) + bytes(12)
        for i, tri in enumerate(triangles):
            body += struct.pack("<4H", *tri, i)
        for tri in triangles:
            body += struct.pack("<4f", 0.0, 1.0, 0.0, -vertices[tri[0]][1])
        for x, y, z in vertices:
            body += struct.pack("<4f", x, y, z, 1.0)
    return bytes(body)


def hitmdl(objects: Sequence[SynthStageObject]) -> bytes:
    records = [vhd_record(o.collision) if o.collision else None for o in objects]
    return build_chunk(b"HITMDL", records)


def build_xpr0(objects: Sequence[SynthStageObject], textures: int) -> bytes:
    data = bytearray()
    tex_headers = []
    for _ in range(textures):
        tex_headers.append(texture_header(len(data), format_word(DXT1, 2, 2)))
        data += solid_dxt1_block(0x07E0)
    buffers: list[bytes] = []
    counts = [0, 0, 0]
    for obj in objects:
        for part in obj.parts:
            data += bytes(-len(data) % 16)
            buffers.append(buffer_header(VERTEX_COMMON, len(data)))
            data += vertex_bytes(part)
            counts[0] += 1
            if not part.push_buffer:
                buffers.append(buffer_header(INDEX_COMMON, len(data)))
                data += struct.pack(f"<{len(part.strip)}H", *part.strip)
                data += bytes(-len(data) % 4)
                counts[1] += 1
        order = range(len(obj.parts)) if obj.order is None else obj.order
        for p in order:
            part = obj.parts[p]
            if part.push_buffer:
                payload = push_buffer_bytes(part.strip, part.pb_garbage)
                buffers.append(push_header(len(data), len(payload)))
                data += payload
                counts[2] += 1
    data += bytes(-len(data) % 16)
    header = userdata(struct.pack("<7I", 0x3F, textures, *counts, 0, 0))
    descriptors = [userdata(descriptor(obj)) for obj in objects]
    return build_xpr([header, *descriptors, *tex_headers, *buffers], bytes(data))


def build_stage_kmd(
    objects: Sequence[SynthStageObject], *, textures: int = 1, collision: bool = True
) -> bytes:
    """Stage KMD: OBJINFO, XPR0 and (optionally) HITMDL in slot 3, as in the game."""
    objinfo = build_chunk(b"OBJINFO", [objinfo_entry(o) for o in objects])
    slots: list[bytes | None] = [objinfo, build_xpr0(objects, textures), None]
    slots.append(hitmdl(objects) if collision else None)
    return build_chunk(b"KMD", slots, table_pos=0x80)


def two_part_object(offset: Vec3 = (0.0, 0.0, 0.0)) -> SynthStageObject:
    """An object with two parts: 0 via IB and 1 via push buffer, submeshes reversed."""
    ox, oy, oz = offset
    floor = ((ox, oy, oz), (ox + 1, oy, oz), (ox, oy, oz + 1))
    return SynthStageObject(
        parts=[SynthPart(offset=offset), SynthPart(offset=(ox + 2, oy, oz), push_buffer=True)],
        order=[1, 0],
        collision=[(floor, [(0, 2, 1)])],
    )
