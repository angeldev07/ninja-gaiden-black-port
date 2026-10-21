"""Vectores de prueba de port/src/gl/texconv.c generados con ngb_tools.xpr (la referencia).

Cada caso es una textura sintética (bytes pseudoaleatorios con semilla fija, nunca datos del
juego) y lo que tiene que dar texconv_convert, calculado con las funciones de Python:
des-swizzle (swizzle.py), canales (pixels.py), paleta (pixels.py), S3TC (dxt.py) y campos con
signo de V8U8 y L6V5U5 (bump.py). Lo escribe en port/tests/texconv_vectors.h:

    PYTHONPATH=src python -m port.texconv_vectors          (desde tests/)
    python tests/port/texconv_vectors.py --write           (desde la raíz, con PYTHONPATH=src)

`test_texconv_vectors.py` comprueba que el .h está al día.
"""

from __future__ import annotations

import random
import sys
from dataclasses import dataclass
from pathlib import Path

from ngb_tools.xpr.bump import signed
from ngb_tools.xpr.dxt import decode_bc1, decode_bc2, decode_bc3
from ngb_tools.xpr.formats import FORMATS
from ngb_tools.xpr.pixels import decode_packed, decode_palette
from ngb_tools.xpr.swizzle import unswizzle

HEADER = Path(__file__).resolve().parents[2] / "port" / "tests" / "texconv_vectors.h"
DIMENSIONS_2D = 2
CUBE_FACES = 6
CUBE_ALIGN = 128
DXT = {0x0C: (8, decode_bc1), 0x0E: (16, decode_bc2), 0x0F: (16, decode_bc3)}
P8, V8U8, L6V5U5 = 0x0B, 0x28, 0x27


@dataclass(frozen=True)
class Case:
    name: str
    code: int
    width: int
    height: int
    levels: int = 1
    pitch: int = 0
    cube: bool = False
    s3tc: bool = False
    palette_entries: int = 0


CASES = (
    Case("a8r8g8b8_8x4", 0x06, 8, 4),
    Case("x8r8g8b8_4x4", 0x07, 4, 4),
    Case("r5g6b5_8x8_mips", 0x05, 8, 8, levels=4),
    Case("a1r5g5b5_4x8", 0x02, 4, 8),
    Case("a4r4g4b4_4x4", 0x04, 4, 4),
    Case("a8_8x8", 0x19, 8, 8),
    Case("l8_4x4", 0x00, 4, 4),
    Case("a8l8_4x4", 0x1A, 4, 4),
    Case("al8_4x2", 0x01, 4, 2),
    Case("r8g8b8a8_2x2", 0x3C, 2, 2),
    Case("a8b8g8r8_4x4", 0x3A, 4, 4),
    Case("r4g4b4a4_2x4", 0x39, 2, 4),
    Case("l16_4x4", 0x32, 4, 4),
    Case("p8_8x8", P8, 8, 8, palette_entries=32),
    Case("dxt1_8x8_cpu", 0x0C, 8, 8),
    Case("dxt1_8x8_s3tc", 0x0C, 8, 8, s3tc=True),
    Case("dxt1_4x4_mips_cpu", 0x0C, 4, 4, levels=3),
    Case("dxt3_8x4_cpu", 0x0E, 8, 4),
    Case("dxt5_8x8_mips_cpu", 0x0F, 8, 8, levels=2),
    Case("dxt5_8x8_mips_s3tc", 0x0F, 8, 8, levels=2, s3tc=True),
    Case("lin_a8r8g8b8_5x3", 0x12, 5, 3, pitch=64),
    Case("lin_r5g6b5_6x2", 0x11, 6, 2, pitch=64),
    Case("lin_l8_7x3", 0x13, 7, 3, pitch=64),
    Case("lin_a8l8_3x3", 0x20, 3, 3, pitch=64),
    Case("cube_a8r8g8b8_4x4", 0x06, 4, 4, cube=True),
    Case("cube_dxt1_8x8_mips_cpu", 0x0C, 8, 8, levels=2, cube=True),
    Case("v8u8_4x4", V8U8, 4, 4),
    Case("l6v5u5_4x4", L6V5U5, 4, 4),
)


def format_word(case: Case) -> int:
    log2u = case.width.bit_length() - 1
    log2v = case.height.bit_length() - 1
    if case.pitch:
        log2u = log2v = 0
    word = 0x9 | (DIMENSIONS_2D << 4) | (case.code << 8) | (case.levels << 16)
    word |= (log2u << 20) | (log2v << 24)
    return word | (4 if case.cube else 0)


def level_sizes(case: Case) -> list[tuple[int, int, int]]:
    """(ancho, alto, bytes) de cada nivel en el origen."""
    out = []
    for level in range(case.levels):
        w, h = max(1, case.width >> level), max(1, case.height >> level)
        if case.code in DXT:
            size = -(-w // 4) * -(-h // 4) * DXT[case.code][0]
        elif case.pitch:
            size = case.pitch * h
        else:
            size = w * h * FORMATS[case.code].bytes_per_element
        out.append((w, h, size))
    return out


def scale(value: int, num: int, den: int) -> int:
    magnitude = (2 * num * abs(value) + den) // (2 * den)
    return -magnitude if value < 0 else magnitude


def l6v5u5(linear: bytes) -> bytes:
    out = bytearray()
    for i in range(0, len(linear), 2):
        word = linear[i] | linear[i + 1] << 8
        u, v = signed(word & 0x1F, 5), signed((word >> 5) & 0x1F, 5)
        texel = (scale(u, 127, 16), scale(v, 127, 16), scale(word >> 10, 127, 63), 127)
        out += bytes(c & 0xFF for c in texel)
    return bytes(out)


def convert_level(case: Case, chunk: bytes, w: int, h: int, palette: bytes) -> bytes:
    if case.code in DXT:
        return chunk if case.s3tc else DXT[case.code][1](chunk, w, h)
    fmt = FORMATS[case.code]
    if case.pitch:
        return decode_packed(chunk, w, h, fmt, pitch=case.pitch)
    linear = unswizzle(chunk, w, h, fmt.bytes_per_element)
    if case.code == P8:
        return decode_palette(linear, palette)
    if case.code == V8U8:
        return linear
    if case.code == L6V5U5:
        return l6v5u5(linear)
    return decode_packed(linear, w, h, fmt)


def build(case: Case, rng: random.Random) -> tuple[bytes, bytes, bytes]:
    """Origen, paleta y lo esperado."""
    sizes = level_sizes(case)
    face_size = sum(size for _, _, size in sizes)
    faces = CUBE_FACES if case.cube else 1
    stride = -(-face_size // CUBE_ALIGN) * CUBE_ALIGN if case.cube else face_size
    src = bytearray()
    for _ in range(faces):
        face = rng.randbytes(face_size)
        src += face + bytes(stride - face_size)
    palette = rng.randbytes(4 * case.palette_entries) if case.palette_entries else b""
    expected = bytearray()
    for f in range(faces):
        offset = f * stride
        for w, h, size in sizes:
            expected += convert_level(case, bytes(src[offset : offset + size]), w, h, palette)
            offset += size
    return bytes(src), palette, bytes(expected)


def c_bytes(name: str, data: bytes) -> str:
    if not data:
        return f"static const uint8_t {name}[1] = {{0}};\n"
    rows = [
        "    " + ", ".join(f"0x{b:02X}" for b in data[i : i + 16]) + ","
        for i in range(0, len(data), 16)
    ]
    return f"static const uint8_t {name}[{len(data)}] = {{\n" + "\n".join(rows) + "\n};\n"


def render() -> str:
    rng = random.Random(0x7E1C0)
    parts = [
        "/* Generado por tests/port/texconv_vectors.py con ngb_tools.xpr: no editar a mano. */\n",
        "#pragma once\n\n#include <stddef.h>\n#include <stdint.h>\n\n",
        "typedef struct texconv_vector {\n    const char *name;\n    uint32_t format, control1, "
        "image_rect;\n    int s3tc;\n    const uint8_t *src;\n    size_t src_len;\n"
        "    const uint8_t *palette;\n    size_t palette_len;\n    const uint8_t *expected;\n"
        "    size_t expected_len;\n} texconv_vector;\n\n",
    ]
    rows = []
    for k, case in enumerate(CASES):
        src, palette, expected = build(case, rng)
        parts += [c_bytes(f"k_src{k}", src), c_bytes(f"k_pal{k}", palette)]
        parts.append(c_bytes(f"k_exp{k}", expected))
        control1 = case.pitch << 16
        rect = (case.width << 16) | case.height if case.pitch else 0
        rows.append(
            f'    {{"{case.name}", 0x{format_word(case):08X}u, 0x{control1:08X}u, 0x{rect:08X}u, '
            f"{int(case.s3tc)}, k_src{k}, {len(src)}, k_pal{k}, {len(palette)}, k_exp{k}, "
            f"{len(expected)}}},"
        )
    parts.append("\nstatic const texconv_vector k_vectors[] = {\n" + "\n".join(rows) + "\n};\n")
    return "".join(parts)


def main(argv: list[str]) -> int:
    text = render()
    if "--write" in argv:
        HEADER.write_text(text, encoding="ascii", newline="\n")
        print(f"{len(CASES)} casos -> {HEADER}")
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
