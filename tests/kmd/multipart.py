"""KMD de personaje sintético con un objeto de dos partes (nunca datos del juego).

Como `doku.kmd` obj1 o `aruma.kmd`: el objeto 0 tiene una parte FVF 0x112 (posición,
normal y UV) que lee del IB y otra FVF 0x142 (posición, color y UV, sin normal) que lee de
un push buffer con basura en el u32 de relleno tras END; sus submallas van en orden
inverso (primero la de la parte 1). El objeto 1 es una pieza de una parte, hija del 0.
"""

from __future__ import annotations

import struct
from collections.abc import Sequence

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

from .synthetic import SynthObject, hierarc, push_buffer_bytes

DXT1 = 0x0C
FVF_NORMAL = 0x112  # stride 32
FVF_LIT = 0x142  # stride 24
COLOR = 0xFF204060
PB_GARBAGE = 0xC2F916E6
STRIP = (0, 1, 2, 3)

type Vec3 = tuple[float, float, float]

# Quads en z = 0: los triángulos de la tira miran a +Z.
QUAD_A: tuple[Vec3, ...] = ((0, 0, 0), (1, 0, 0), (0, 1, 0), (1, 1, 0))
QUAD_B: tuple[Vec3, ...] = ((2, 0, 0), (3, 0, 0), (2, 1, 0), (3, 1, 0))
UVS = ((0.0, 0.0), (1.0, 0.0), (0.0, 1.0), (1.0, 1.0))


def normal_vertices(points: Sequence[Vec3]) -> bytes:
    return b"".join(
        struct.pack("<8f", *p, 0.0, 0.0, 1.0, *uv) for p, uv in zip(points, UVS, strict=True)
    )


def lit_vertices(points: Sequence[Vec3]) -> bytes:
    return b"".join(
        struct.pack("<3fI2f", *p, COLOR, *uv) for p, uv in zip(points, UVS, strict=True)
    )


def part_header(fvf: int, stride: int, ib_count: int) -> bytes:
    return struct.pack("<8I", fvf, 0, 0, 4, stride, 1, 0, ib_count)


def submesh(ordinal: int, part: int, push_buffer: bool) -> bytes:
    source = 0 if push_buffer else 2
    fixed = [ordinal, part, 1, 0, source, 0, 8, 1, 0, 1, 0, 1, 0x900, 0, 0x302, 0x303, 1]
    stage = struct.pack("<4si14I", b"t_PP", 0, 1, 0, 4, 1, 2, 1, 3, 1, 2, 1, 0, 0, 0, 0)
    return struct.pack("<24I", *fixed, *([0] * 7)) + stage


def two_part_descriptor() -> bytes:
    """Dos partes; la submalla 0 es de la parte 1 (push buffer) y la 1 de la parte 0 (IB)."""
    return (
        struct.pack("<2I", 2, 2)
        + part_header(FVF_NORMAL, 32, 4)
        + part_header(FVF_LIT, 24, 0)
        + submesh(0, 1, True)
        + submesh(1, 0, False)
    )


def one_part_descriptor() -> bytes:
    return struct.pack("<2I", 1, 1) + part_header(FVF_NORMAL, 32, 4) + submesh(0, 0, False)


def objinfo_entry(n_sub: int, group: int = 1) -> bytes:
    head = struct.pack("<4sIIIIIfI", b"OI\x01\x01", 0x90, n_sub, group, 0x201, 0, 1.0, 0)
    vectors = b"".join(
        struct.pack("<3fI", *v, 0) for v in ((0, 0, 0), (0, 0, 0), (1, 1, 1), (0, 0, 0), (3, 1, 0))
    )
    sphere = struct.pack("<4f", 1.5, 0.5, 0.0, 1.6)
    return head + bytes(0x10) + vectors + sphere + struct.pack("<I12x4f", 0, 0, 0, 0, 1) * n_sub


def build_multipart_kmd(*, vb_count: int = 3, descriptor: bytes | None = None) -> bytes:
    """KMD con OBJINFO, XPR0 (cabecera, 2 descriptores, 1 textura, 3 VB, 2 IB, 1 PB) y HIERARC."""
    data = bytearray(solid_dxt1_block(0x07E0))
    headers = [texture_header(0, format_word(DXT1, 2, 2))]
    for payload, kind in (
        (normal_vertices(QUAD_A), VERTEX_COMMON),
        (struct.pack("<4H", *STRIP), INDEX_COMMON),
        (lit_vertices(QUAD_B), VERTEX_COMMON),
        (push_buffer_bytes(STRIP)[:-4] + struct.pack("<I", PB_GARBAGE), None),
        (normal_vertices(QUAD_A), VERTEX_COMMON),
        (struct.pack("<4H", *STRIP), INDEX_COMMON),
    ):
        data += bytes(-len(data) % 16)
        if kind is None:
            headers.append(push_header(len(data), len(payload)))
        else:
            headers.append(buffer_header(kind, len(data)))
        data += payload
    data += bytes(-len(data) % 16)
    model_header = userdata(struct.pack("<7I", 0x3F, 1, vb_count, 2, 1, 0, 0))
    descriptors = [
        userdata(two_part_descriptor() if descriptor is None else descriptor),
        userdata(one_part_descriptor()),
    ]
    xpr = build_xpr([model_header, *descriptors, *headers], bytes(data))
    objinfo = build_chunk(b"OBJINFO", [objinfo_entry(2), objinfo_entry(1)])
    tree = hierarc([SynthObject(children=(1,)), SynthObject()], 0)
    return build_chunk(b"KMD", [objinfo, xpr, tree], table_pos=0x80)
