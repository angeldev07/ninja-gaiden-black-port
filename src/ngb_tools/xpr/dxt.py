"""Decodificación S3TC: DXT1 (BC1), DXT2/3 (BC2) y DXT4/5 (BC3) a RGBA8.

Los DXT dividen la imagen en bloques de 4x4 píxeles y comprimen cada uno por separado, a
tamaño fijo. En la NV2A los bloques van en orden lineal (fila de bloques tras fila de
bloques) y sin swizzle.

**Mitad de color (8 bytes, común a los tres):** dos colores RGB565 `c0` y `c1` (u16 LE) y
32 bits con un índice de 2 bits por píxel (el píxel 0 en los bits bajos, fila a fila). Los
índices 2 y 3 son colores intermedios:

- `c0 > c1`: c2 = (2*c0 + c1) / 3, c3 = (c0 + 2*c1) / 3 (cuatro colores);
- `c0 <= c1` y **solo en DXT1**: c2 = (c0 + c1) / 2 y c3 = negro transparente. Es el
  "alfa de 1 bit" de DXT1 (`L_DXT1_A1R5G5B5` en la NV2A). DXT3/5 usan siempre 4 colores.

Los canales de 5 y 6 bits se amplían a 8 replicando sus bits altos en los bajos
(`r8 = r5 << 3 | r5 >> 2`), así 0 -> 0 y 31 -> 255. Las divisiones truncan, como en xemu.

**DXT3 (16 bytes):** 64 bits de alfa explícito (4 bits por píxel, `a8 = a4 * 17`) y
después la mitad de color.

**DXT5 (16 bytes):** dos alfas `a0`, `a1` de 8 bits, 48 bits con un índice de 3 bits por
píxel y la mitad de color. Si `a0 > a1` hay 6 alfas interpolados; si no, 4 interpolados
más 0 y 255.

DXT2 y DXT4 son DXT3 y DXT5 con el color premultiplicado por el alfa. En la NV2A
comparten código (`L_DXT23_A8R8G8B8` = 0x0E y `L_DXT45_A8R8G8B8` = 0x0F en xemu), así que
la textura no dice cuál es: se decodifican como DXT3 y DXT5 (alfa recto), como xemu y Cxbx.
El manifiesto lo deja escrito (`alpha`, ver `texture.alpha_mode`): un píxel con un canal
mayor que su alfa descarta el premultiplicado. En el juego, de los 5510 DXT5 (0x0F),
5134 lo descartan, 29 son opacos y 347 quedan sin decidir. El único 0x0E (dlcustom.xpr)
queda sin decidir: es opaco al 99,9 % y sus píxeles translúcidos son oscuros.

Fuentes:
- xemu, decodificador de referencia del emulador:
  https://github.com/xemu-project/xemu/blob/master/hw/xbox/nv2a/pgraph/s3tc.c
- Microsoft, formatos BC1-BC3:
  https://learn.microsoft.com/en-us/windows/win32/direct3d10/d3d10-graphics-programming-guide-resources-block-compression
"""

from __future__ import annotations

import struct
from collections.abc import Callable, Sequence
from typing import Final

BLOCK: Final = 4
_COLOR_HALF: Final = struct.Struct("<HHI")

type Rgb = tuple[int, int, int]
type BlockDecoder = Callable[[bytes], Sequence[bytes]]


def _expand565(color: int) -> Rgb:
    r5, g6, b5 = color >> 11, (color >> 5) & 0x3F, color & 0x1F
    return (r5 << 3 | r5 >> 2, g6 << 2 | g6 >> 4, b5 << 3 | b5 >> 2)


def _color_palette(c0: int, c1: int, *, four_colors: bool) -> tuple[Rgb, Rgb, Rgb, Rgb]:
    """Los cuatro colores del bloque; en modo de tres, el cuarto es negro."""
    rgb0, rgb1 = _expand565(c0), _expand565(c1)
    if four_colors:
        rgb2 = (
            (2 * rgb0[0] + rgb1[0]) // 3,
            (2 * rgb0[1] + rgb1[1]) // 3,
            (2 * rgb0[2] + rgb1[2]) // 3,
        )
        rgb3 = (
            (rgb0[0] + 2 * rgb1[0]) // 3,
            (rgb0[1] + 2 * rgb1[1]) // 3,
            (rgb0[2] + 2 * rgb1[2]) // 3,
        )
    else:
        rgb2 = ((rgb0[0] + rgb1[0]) // 2, (rgb0[1] + rgb1[1]) // 2, (rgb0[2] + rgb1[2]) // 2)
        rgb3 = (0, 0, 0)
    return rgb0, rgb1, rgb2, rgb3


def _color_indices(indices: int) -> list[int]:
    return [(indices >> (2 * i)) & 0b11 for i in range(16)]


def _bc1_block(block: bytes) -> Sequence[bytes]:
    c0, c1, indices = _COLOR_HALF.unpack_from(block)
    four_colors = c0 > c1
    palette = _color_palette(c0, c1, four_colors=four_colors)
    texels = [bytes((*rgb, 255)) for rgb in palette]
    if not four_colors:
        texels[3] = bytes(4)  # negro transparente
    return [texels[i] for i in _color_indices(indices)]


def _with_alpha(colors: bytes, alpha: Sequence[int]) -> Sequence[bytes]:
    """Combina la mitad de color (siempre de 4 colores) con un alfa por píxel."""
    c0, c1, indices = _COLOR_HALF.unpack_from(colors)
    palette = _color_palette(c0, c1, four_colors=True)
    return [
        bytes((*palette[index], a)) for index, a in zip(_color_indices(indices), alpha, strict=True)
    ]


def _bc2_block(block: bytes) -> Sequence[bytes]:
    bits = int.from_bytes(block[0:8], "little")
    alpha = [((bits >> (4 * i)) & 0xF) * 17 for i in range(16)]
    return _with_alpha(block[8:16], alpha)


def _bc3_alpha_palette(a0: int, a1: int) -> tuple[int, ...]:
    if a0 > a1:
        return (a0, a1, *(((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)))
    return (a0, a1, *(((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)), 0, 255)


def _bc3_block(block: bytes) -> Sequence[bytes]:
    palette = _bc3_alpha_palette(block[0], block[1])
    bits = int.from_bytes(block[2:8], "little")
    alpha = [palette[(bits >> (3 * i)) & 0b111] for i in range(16)]
    return _with_alpha(block[8:16], alpha)


def compressed_size(width: int, height: int, block_bytes: int) -> int:
    """Bytes de una imagen DXT: los bloques cubren la imagen aunque no sea múltiplo de 4."""
    return -(-width // BLOCK) * -(-height // BLOCK) * block_bytes


def _decode_blocks(
    data: bytes, width: int, height: int, block_bytes: int, decode_block: BlockDecoder
) -> bytes:
    blocks_x, blocks_y = -(-width // BLOCK), -(-height // BLOCK)
    needed = blocks_x * blocks_y * block_bytes
    if len(data) < needed:
        raise ValueError(f"{len(data)} bytes; un DXT de {width}x{height} necesita {needed}")
    out = bytearray(width * height * 4)
    for by in range(blocks_y):
        for bx in range(blocks_x):
            start = (by * blocks_x + bx) * block_bytes
            texels = decode_block(data[start : start + block_bytes])
            x0 = bx * BLOCK
            span = min(BLOCK, width - x0) * 4  # bytes visibles de cada fila del bloque
            for row in range(min(BLOCK, height - by * BLOCK)):
                pos = ((by * BLOCK + row) * width + x0) * 4
                out[pos : pos + span] = b"".join(texels[row * BLOCK : row * BLOCK + BLOCK])[:span]
    return bytes(out)


def decode_bc1(data: bytes, width: int, height: int) -> bytes:
    """DXT1 a RGBA8 (8 bytes por bloque, alfa de 1 bit)."""
    return _decode_blocks(data, width, height, 8, _bc1_block)


def decode_bc2(data: bytes, width: int, height: int) -> bytes:
    """DXT2/DXT3 a RGBA8 (16 bytes por bloque, alfa explícito de 4 bits)."""
    return _decode_blocks(data, width, height, 16, _bc2_block)


def decode_bc3(data: bytes, width: int, height: int) -> bytes:
    """DXT4/DXT5 a RGBA8 (16 bytes por bloque, alfa interpolado)."""
    return _decode_blocks(data, width, height, 16, _bc3_block)
