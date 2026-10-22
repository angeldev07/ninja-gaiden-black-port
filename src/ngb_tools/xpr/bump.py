"""Signed bump textures, V8U8 and L6V5U5 (normal maps and DuDv maps), to RGBA8.

The NV2A has no "signed" formats: V8U8 is the color `SZ_G8B8` (0x28) and L6V5U5 is
`SZ_R6G5B5` (0x27). What makes them signed is the texture-stage state, which the
XPR0 does not store; Xbox D3D gives them the bump name (`X_D3DFMT_V8U8` = 0x28 aliased to
G8B8, `X_D3DFMT_L6V5U5` = 0x27 aliased to R6G5B5) and they are exported that way. The game
has 326 V8U8 (scenery KMDs: water and bumpy surfaces) and 32 L6V5U5 (`kotei.kmd`...), all
swizzled. Layout of the 16-bit little-endian word:

    V8U8     bits 0-7  U (signed, two's complement)   bits 8-15 V (signed)
    L6V5U5   bits 0-4  U (signed)   bits 5-9 V (signed)   bits 10-15 L (unsigned)

Evidence: Cxbx-Reloaded (`XbConvert.cpp`) describes L6V5U5 as "byte[0]=vvvUuuuu,
byte[1]=LlllllVv (U and V, but not L, are stored in two's complement)" and reads V8U8 (G8B8)
with B = byte 0 and G = byte 1. xemu (`pgraph/texture.c`, `pgraph_convert_texture_data`)
converts `SZ_R6G5B5` by flipping the sign bit of the 5-bit fields
(`rgb655 ^= (1 << 9) | (1 << 4)`) and leaves bits 10-15 unsigned.

**Mapping to PNG** (with u, v in [-1, 1): u = U/128 for 8 bits and U/16 for 5 bits):

- R = U in offset binary: `U + 128` for 8 bits (the byte with bit 7 flipped) and, for
  5 bits, `U + 16` widened to 8 by replicating bits (like Cxbx's `__L6V5U5ToARGBRow_C`). It is
  `(u * 0.5 + 0.5) * 255` up to rounding: -128 -> 0, 0 -> 128, 127 -> 255; -16 -> 0, 0 -> 132,
  15 -> 255.
- G = the same with V.
- B = z of the reconstructed normal, `z = sqrt(max(0, 1 - u² - v²))`, as `(z + 1) * 127.5`
  rounded up at .5: flat (u = v = 0) -> 255; u or v at the extreme -> 128.
- A = 255 in V8U8; in L6V5U5, L widened from 6 to 8 bits by replicating bits (it is luminance,
  not transparency: the manifest flags it).

The result looks like a normal map: violet-blue where the surface is flat.
"""

from __future__ import annotations

import math
import struct
from functools import cache
from typing import Final

from ngb_tools.xpr.formats import Codec, TextureFormat
from ngb_tools.xpr.pixels import expand_bits

WORD_BYTES: Final = 2
_WORDS: Final = 1 << 16
_OPAQUE: Final = 255


def signed(value: int, bits: int) -> int:
    """The `bits`-bit field read as two's complement."""
    return value - (1 << bits) if value >> (bits - 1) else value


def offset_binary(value: int, bits: int) -> int:
    """A signed `bits`-bit value mapped to 0-255: offset binary, widened."""
    return expand_bits(value + (1 << (bits - 1)), bits)


def normal_z(u: float, v: float) -> int:
    """z component of the normal (u, v, z), encoded in 128-255."""
    z = math.sqrt(max(0.0, 1.0 - u * u - v * v))
    return min(255, math.floor((z + 1.0) * 127.5 + 0.5))


def v8u8_texel(word: int) -> bytes:
    u, v = signed(word & 0xFF, 8), signed(word >> 8, 8)
    return bytes((offset_binary(u, 8), offset_binary(v, 8), normal_z(u / 128, v / 128), _OPAQUE))


def l6v5u5_texel(word: int) -> bytes:
    u, v = signed(word & 0x1F, 5), signed((word >> 5) & 0x1F, 5)
    luminance = expand_bits(word >> 10, 6)
    return bytes((offset_binary(u, 5), offset_binary(v, 5), normal_z(u / 16, v / 16), luminance))


@cache
def _table(codec: Codec) -> tuple[bytes, ...]:
    """RGBA texel for each of the 65,536 words (computed once per format)."""
    texel = v8u8_texel if codec is Codec.V8U8 else l6v5u5_texel
    return tuple(texel(word) for word in range(_WORDS))


def decode_bump(
    data: bytes, width: int, height: int, fmt: TextureFormat, *, pitch: int | None = None
) -> bytes:
    """V8U8 or L6V5U5 image in linear order to RGBA8; `pitch` is the bytes per row."""
    if fmt.codec not in (Codec.V8U8, Codec.L6V5U5):
        raise ValueError(f"{fmt.name} is not a signed bump format")
    row_bytes = width * WORD_BYTES
    stride = row_bytes if pitch is None else pitch
    needed = stride * (height - 1) + row_bytes if height else 0
    if stride < row_bytes or len(data) < needed:
        raise ValueError(f"{len(data)} bytes; {fmt.name} {width}x{height} needs {needed}")
    table = _table(fmt.codec)
    row = struct.Struct(f"<{width}H")
    return b"".join(
        table[word] for y in range(height) for word in row.unpack_from(data, y * stride)
    )
