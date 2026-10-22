"""S3TC decoding: DXT1 (BC1), DXT2/3 (BC2) and DXT4/5 (BC3) to RGBA8.

DXT splits the image into 4x4-pixel blocks and compresses each one separately, at a
fixed size. On the NV2A the blocks are in linear order (block row after block
row) and not swizzled.

**Color half (8 bytes, common to all three):** two RGB565 colors `c0` and `c1` (u16 LE) and
32 bits holding a 2-bit index per pixel (pixel 0 in the low bits, row by row). Indices
2 and 3 are intermediate colors:

- `c0 > c1`: c2 = (2*c0 + c1) / 3, c3 = (c0 + 2*c1) / 3 (four colors);
- `c0 <= c1` and **DXT1 only**: c2 = (c0 + c1) / 2 and c3 = transparent black. This is
  DXT1's "1-bit alpha" (`L_DXT1_A1R5G5B5` on the NV2A). DXT3/5 always use 4 colors.

The 5- and 6-bit channels are widened to 8 by replicating their high bits into the low ones
(`r8 = r5 << 3 | r5 >> 2`), so 0 -> 0 and 31 -> 255. Divisions truncate, as in xemu.

**DXT3 (16 bytes):** 64 bits of explicit alpha (4 bits per pixel, `a8 = a4 * 17`) and
then the color half.

**DXT5 (16 bytes):** two 8-bit alphas `a0`, `a1`, 48 bits with a 3-bit index per
pixel, and the color half. If `a0 > a1` there are 6 interpolated alphas; otherwise, 4
interpolated plus 0 and 255.

DXT2 and DXT4 are DXT3 and DXT5 with the color premultiplied by alpha. On the NV2A they
share code (`L_DXT23_A8R8G8B8` = 0x0E and `L_DXT45_A8R8G8B8` = 0x0F in xemu), so the
texture does not say which it is: they are decoded as DXT3 and DXT5 (straight alpha), like
xemu and Cxbx. The manifest records this (`alpha`, see `texture.alpha_mode`): a pixel with a
channel greater than its alpha rules out premultiplication. In the game, of the 5510 DXT5
(0x0F), 5134 rule it out, 29 are opaque and 347 remain undecided. The only 0x0E
(dlcustom.xpr) remains undecided: it is 99.9 % opaque and its translucent pixels are dark.

Sources:
- xemu, the emulator's reference decoder:
  https://github.com/xemu-project/xemu/blob/master/hw/xbox/nv2a/pgraph/s3tc.c
- Microsoft, BC1-BC3 formats:
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
    """The four colors of the block; in three-color mode, the fourth is black."""
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
        texels[3] = bytes(4)  # transparent black
    return [texels[i] for i in _color_indices(indices)]


def _with_alpha(colors: bytes, alpha: Sequence[int]) -> Sequence[bytes]:
    """Combine the color half (always 4 colors) with a per-pixel alpha."""
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
    """Bytes of a DXT image: the blocks cover the image even if it is not a multiple of 4."""
    return -(-width // BLOCK) * -(-height // BLOCK) * block_bytes


def _decode_blocks(
    data: bytes, width: int, height: int, block_bytes: int, decode_block: BlockDecoder
) -> bytes:
    blocks_x, blocks_y = -(-width // BLOCK), -(-height // BLOCK)
    needed = blocks_x * blocks_y * block_bytes
    if len(data) < needed:
        raise ValueError(f"{len(data)} bytes; a {width}x{height} DXT needs {needed}")
    out = bytearray(width * height * 4)
    for by in range(blocks_y):
        for bx in range(blocks_x):
            start = (by * blocks_x + bx) * block_bytes
            texels = decode_block(data[start : start + block_bytes])
            x0 = bx * BLOCK
            span = min(BLOCK, width - x0) * 4  # visible bytes of each block row
            for row in range(min(BLOCK, height - by * BLOCK)):
                pos = ((by * BLOCK + row) * width + x0) * 4
                out[pos : pos + span] = b"".join(texels[row * BLOCK : row * BLOCK + BLOCK])[:span]
    return bytes(out)


def decode_bc1(data: bytes, width: int, height: int) -> bytes:
    """DXT1 to RGBA8 (8 bytes per block, 1-bit alpha)."""
    return _decode_blocks(data, width, height, 8, _bc1_block)


def decode_bc2(data: bytes, width: int, height: int) -> bytes:
    """DXT2/DXT3 to RGBA8 (16 bytes per block, explicit 4-bit alpha)."""
    return _decode_blocks(data, width, height, 16, _bc2_block)


def decode_bc3(data: bytes, width: int, height: int) -> bytes:
    """DXT4/DXT5 to RGBA8 (16 bytes per block, interpolated alpha)."""
    return _decode_blocks(data, width, height, 16, _bc3_block)
