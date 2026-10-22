"""Builder of synthetic KMDs: game data is never used in the tests.

It reproduces the layout observed in `ryu.kmd`:

- a KMD chunk with OBJINFO, XPR0 and HIERARC (the latter optional);
- in the XPR0, the header USERDATA, one descriptor per object, the textures (single-color
  4x4 DXT1) and, per object, its VB followed by its IB (or by its push buffer if the strip
  goes there).
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
FVF_STATIC = 0x112
FVF_SKINNED = 0x116
FVF_COLORED = 0x042

type Vec2 = tuple[float, float]
type Vec3 = tuple[float, float, float]

QUAD_POSITIONS: tuple[Vec3, ...] = ((0, 0, 0), (1, 0, 0), (0, 1, 0), (1, 1, 0))
QUAD_UVS: tuple[Vec2, ...] = ((0, 0), (1, 0), (0, -1), (1, -1))
QUAD_STRIP = (0, 1, 2, 3)


@dataclass
class SynthObject:
    """An object with a single submesh; by default a quad at z=0 with +Z normals."""

    positions: Sequence[Vec3] = QUAD_POSITIONS
    uvs: Sequence[Vec2] = QUAD_UVS
    strip: Sequence[int] = QUAD_STRIP
    translation: Vec3 = (0.0, 0.0, 0.0)
    rotation: Vec3 = (0.0, 0.0, 0.0)
    scale: Vec3 = (1.0, 1.0, 1.0)
    children: Sequence[int] = ()
    group: int = 1
    texture: int = 0
    fvf: int = FVF_STATIC
    weights: Sequence[float] | None = None
    push_buffer: bool = False
    cull: int = 0x900
    blend: bool = False
    stride_override: int | None = None
    extra: dict[str, int] = field(default_factory=dict)

    @property
    def stride(self) -> int:
        if self.stride_override is not None:
            return self.stride_override
        return {FVF_STATIC: 32, FVF_SKINNED: 36, FVF_COLORED: 16}.get(self.fvf, 32)


def vertex_bytes(obj: SynthObject) -> bytes:
    out = bytearray()
    for i, (pos, uv) in enumerate(zip(obj.positions, obj.uvs, strict=True)):
        if obj.fvf == FVF_SKINNED:
            weight = obj.weights[i] if obj.weights is not None else 1.0
            out += struct.pack("<9f", *pos, weight, 0.0, 0.0, 1.0, *uv)
        elif obj.fvf == FVF_COLORED:
            out += struct.pack("<3fI", *pos, 0xFF7F7F7F)
        else:
            out += struct.pack("<8f", *pos, 0.0, 0.0, 1.0, *uv)
            out += bytes(max(0, obj.stride - 32))
    return bytes(out)


def objinfo_entry(obj: SynthObject) -> bytes:
    head = struct.pack("<4sIIIIIfI", b"OI\x01\x01", 0x90, 1, obj.group, 0x201, 0, 1.0, 0)
    vectors = b"".join(
        struct.pack("<3fI", *v, 0)
        for v in (obj.translation, obj.rotation, obj.scale, (0, 0, 0), (1, 1, 0))
    )
    sphere = struct.pack("<4f", 0.5, 0.5, 0.0, 0.8)
    submesh = struct.pack("<I12x4f", 0x201, 0.5, 0.5, 0.0, 0.8)
    return head + bytes(0x10) + vectors + sphere + submesh


def descriptor(obj: SynthObject) -> bytes:
    count = len(obj.strip)
    ib_count = 0 if obj.push_buffer else count
    header = struct.pack(
        "<10I", 1, 1, obj.fvf, 0, 0, len(obj.positions), obj.stride, 1, 0, ib_count
    )
    source = 0 if obj.push_buffer else 2
    fixed = [0, 0, 1, 0, source, 0, 2 * count, 1, 1, 1, int(obj.blend), int(not obj.blend)]
    fixed += [obj.cull, 0, 0x302, 1 if obj.blend else 0x303, 1] + [0] * 7
    material = struct.pack(
        "<17f3I", 0.7, 0.7, 0.7, 1, 0.3, 0.3, 0.35, 1, 0.8, 0.8, 0.85, 1, 0, 0, 0, 1, 35, 1, 1, 2
    )
    stage = struct.pack(
        "<4si14I", b"tx_o", obj.texture, 1, 0, 4, 1, 2, 1, 3, 1, 2, 1, 0x80000000, 0, 0, 0
    )
    return header + struct.pack("<24I", *fixed) + material + stage


def push_buffer_bytes(indices: Sequence[int]) -> bytes:
    """BEGIN(strip), ARRAY_ELEMENT16 in pairs, ARRAY_ELEMENT32 if one is left, END, padding."""
    words = [0x000417FC, 6]
    pairs = [indices[i] | (indices[i + 1] << 16) for i in range(0, len(indices) - 1, 2)]
    if pairs:
        words += [0x40001800 | (len(pairs) << 18), *pairs]
    if len(indices) % 2:
        words += [0x00041808, indices[-1]]
    words += [0x000417FC, 0, 0]
    return struct.pack(f"<{len(words)}I", *words)


def hierarc(objects: Sequence[SynthObject], root: int) -> bytes:
    parent = {c: k for k, o in enumerate(objects) for c in o.children}

    def subtree(k: int) -> int:
        return 1 + sum(subtree(c) for c in objects[k].children if c < len(objects))

    entries = []
    for k, obj in enumerate(objects):
        raw = struct.pack(
            f"<4H{len(obj.children)}H",
            k,
            parent.get(k, 0xFFFF),
            len(obj.children),
            subtree(k),
            *obj.children,
        )
        entries.append(raw + bytes(-len(raw) % 16))
    chunk = bytearray(build_chunk(b"HIERARC", entries, table_pos=0x30))
    struct.pack_into("<4I", chunk, 0x20, 0xFFFFFFFF, 0x00010001, root, 0)
    return bytes(chunk)


def build_xpr0(objects: Sequence[SynthObject], textures: int = 1) -> bytes:
    data = bytearray()
    tex_headers = []
    for _ in range(textures):
        tex_headers.append(texture_header(len(data), format_word(DXT1, 2, 2)))
        data += solid_dxt1_block(0x07E0)
    buffers: list[bytes] = []
    for obj in objects:
        data += bytes(-len(data) % 16)
        buffers.append(buffer_header(VERTEX_COMMON, len(data)))
        data += vertex_bytes(obj)
        data += bytes(-len(data) % 4)
        if obj.push_buffer:
            payload = push_buffer_bytes(obj.strip)
            buffers.append(push_header(len(data), len(payload)))
            data += payload
        else:
            buffers.append(buffer_header(INDEX_COMMON, len(data)))
            data += struct.pack(f"<{len(obj.strip)}H", *obj.strip)
    data += bytes(-len(data) % 16)
    header = userdata(struct.pack("<7I", 0x3F, textures, len(objects), len(objects), 1, 0, 0))
    descriptors = [userdata(descriptor(obj)) for obj in objects]
    return build_xpr([header, *descriptors, *tex_headers, *buffers], bytes(data))


def build_kmd(
    objects: Sequence[SynthObject],
    *,
    root: int = 0,
    textures: int = 1,
    hierarchy: bool = True,
) -> bytes:
    """KMD with one object per element of `objects`; `children` defines HIERARC."""
    objinfo = build_chunk(b"OBJINFO", [objinfo_entry(o) for o in objects])
    slots: list[bytes | None] = [objinfo, build_xpr0(objects, textures)]
    slots.append(hierarc(objects, root) if hierarchy else None)
    return build_chunk(b"KMD", slots, table_pos=0x80)


def parent_and_child() -> list[SynthObject]:
    """The base case: parent at (0, 1, 0) and its child offset by (2, 0, 0) from it."""
    return [
        SynthObject(translation=(0.0, 1.0, 0.0), children=(1,)),
        SynthObject(translation=(2.0, 0.0, 0.0)),
    ]
