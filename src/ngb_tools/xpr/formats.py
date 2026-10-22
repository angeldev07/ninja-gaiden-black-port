"""Xbox texture format table: D3DFORMAT code -> name, bpp, storage and codec.

On the Xbox, a texture's D3DFORMAT is directly the NV2A color code
(`NV097_SET_TEXTURE_FORMAT_COLOR_*`) and travels in bits 8-15 of the `Format` field. That is why
the values do not match those of PC D3D8. Each format is stored in one of three
ways:

- **swizzled** (`SZ_*` on the NV2A): power-of-two dimensions, pixels in Morton order
  (see `swizzle`). They support mipmaps.
- **linear** (`LU_IMAGE_*`, D3D's `LIN_*`): consecutive rows with a *pitch* (bytes
  per row, a multiple of 64) and free dimensions, which come in the `Size` field. No mips.
- **compressed** (`L_DXT*`): 4x4-pixel S3TC blocks in linear order, not swizzled.
  DXT2 and DXT3 share a code (0x0E), as do DXT4 and DXT5 (0x0F): the NV2A does not tell
  premultiplied alpha apart, so they are decoded as DXT3 and DXT5.

V8U8 and L6V5U5 (and their linear variant) are signed bump maps and are exported with the
mapping of `bump`. The other signed formats (V16U16), depth formats (D16, D24S8...) and
YUV (YUY2, UYVY) are known but not decoded: they do not appear in the game.

The channels of packed formats are described over the pixel's little-endian word,
with the D3D convention of naming from the highest bit to the lowest: in A1R5G5B5, A is
bit 15 and B is bits 0-4. When the format has no channel, a constant is used: A8
leaves the color white (like Cxbx) and L8 repeats luminance in R, G and B.

Sources (enum and semantics verified; without using the XDK):
- Cxbx-Reloaded, `X_D3DFMT_*` enum and `Format`/`Size` masks:
  https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/master/src/core/hle/D3D8/XbD3D8Types.h
- Cxbx-Reloaded, `FormatInfos` table (bpp, Swzzld/Linear/Cmprsd) and channel order:
  https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/master/src/core/hle/D3D8/XbConvert.cpp
- xemu, `NV097_SET_TEXTURE_FORMAT_COLOR_*` codes:
  https://github.com/xemu-project/xemu/blob/master/hw/xbox/nv2a/nv2a_regs.h
- xemu, bytes per pixel and size of each level (linear, swizzled and DXT):
  https://github.com/xemu-project/xemu/blob/master/hw/xbox/nv2a/pgraph/texture.c
"""

from __future__ import annotations

from collections.abc import Mapping
from dataclasses import dataclass
from enum import StrEnum
from types import MappingProxyType
from typing import Final


class Storage(StrEnum):
    """How the texture data is laid out in memory."""

    SWIZZLED = "swizzled"
    LINEAR = "linear"
    COMPRESSED = "compressed"


class Codec(StrEnum):
    """Which decoder converts the data (already in linear order) to RGBA8."""

    PACKED = "packed"
    PALETTE = "palette"
    BC1 = "bc1"
    BC2 = "bc2"
    BC3 = "bc3"
    V8U8 = "v8u8"  # signed bump, see `bump`
    L6V5U5 = "l6v5u5"
    UNSUPPORTED = "unsupported"


@dataclass(frozen=True, slots=True)
class Field:
    """A channel within the pixel word: `bits` bits starting at bit `shift`."""

    shift: int
    bits: int


# A channel is a field of the word, or a constant 0-255 if the format has none.
type Channel = Field | int


@dataclass(frozen=True, slots=True)
class PackedLayout:
    """Where each RGBA channel sits in a pixel's little-endian word."""

    red: Channel
    green: Channel
    blue: Channel
    alpha: Channel


@dataclass(frozen=True, slots=True)
class TextureFormat:
    """An Xbox D3DFORMAT and what is needed to decode it."""

    code: int
    name: str
    bits_per_pixel: int
    storage: Storage
    codec: Codec
    layout: PackedLayout | None = None
    aliases: tuple[str, ...] = ()

    @property
    def is_decodable(self) -> bool:
        return self.codec is not Codec.UNSUPPORTED

    @property
    def is_signed_bump(self) -> bool:
        """V8U8 or L6V5U5: the PNG is a bump map, not a color (see `bump`)."""
        return self.codec in (Codec.V8U8, Codec.L6V5U5)

    @property
    def is_compressed(self) -> bool:
        return self.storage is Storage.COMPRESSED

    @property
    def bytes_per_element(self) -> int:
        """Bytes per pixel (or per palette index) of an uncompressed format."""
        return self.bits_per_pixel // 8

    @property
    def block_bytes(self) -> int:
        """Bytes of a 4x4-pixel block: 8 for DXT1 and 16 for DXT3/DXT5."""
        return 16 * self.bits_per_pixel // 8


# --- Channel layouts -------------------------------------------------------------

_OPAQUE: Final = 255
_WHITE: Final = 255


def _argb(alpha: Channel, red: Field, green: Field, blue: Field) -> PackedLayout:
    return PackedLayout(red=red, green=green, blue=blue, alpha=alpha)


_A8R8G8B8: Final = _argb(Field(24, 8), Field(16, 8), Field(8, 8), Field(0, 8))
_X8R8G8B8: Final = _argb(_OPAQUE, Field(16, 8), Field(8, 8), Field(0, 8))
_R5G6B5: Final = _argb(_OPAQUE, Field(11, 5), Field(5, 6), Field(0, 5))
_A1R5G5B5: Final = _argb(Field(15, 1), Field(10, 5), Field(5, 5), Field(0, 5))
_X1R5G5B5: Final = _argb(_OPAQUE, Field(10, 5), Field(5, 5), Field(0, 5))
_A4R4G4B4: Final = _argb(Field(12, 4), Field(8, 4), Field(4, 4), Field(0, 4))
_R5G5B5A1: Final = _argb(Field(0, 1), Field(11, 5), Field(6, 5), Field(1, 5))
_R4G4B4A4: Final = _argb(Field(0, 4), Field(12, 4), Field(8, 4), Field(4, 4))
_A8B8G8R8: Final = _argb(Field(24, 8), Field(0, 8), Field(8, 8), Field(16, 8))
_B8G8R8A8: Final = _argb(Field(0, 8), Field(8, 8), Field(16, 8), Field(24, 8))
_R8G8B8A8: Final = _argb(Field(0, 8), Field(24, 8), Field(16, 8), Field(8, 8))


def _luminance(lum: Field, alpha: Channel = _OPAQUE) -> PackedLayout:
    return PackedLayout(red=lum, green=lum, blue=lum, alpha=alpha)


_L8: Final = _luminance(Field(0, 8))
_AL8: Final = _luminance(Field(0, 8), Field(0, 8))  # alpha and luminance from the same byte
_A8L8: Final = _luminance(Field(0, 8), Field(8, 8))
_L16: Final = _luminance(Field(0, 16))
_A8: Final = PackedLayout(red=_WHITE, green=_WHITE, blue=_WHITE, alpha=Field(0, 8))


# --- Table --------------------------------------------------------------------------------

_SZ: Final = Storage.SWIZZLED
_LIN: Final = Storage.LINEAR
_CMP: Final = Storage.COMPRESSED
_NO: Final = Codec.UNSUPPORTED


def _packed(
    code: int, name: str, bpp: int, storage: Storage, layout: PackedLayout
) -> TextureFormat:
    return TextureFormat(code, name, bpp, storage, Codec.PACKED, layout)


_TABLE: Final = (
    _packed(0x00, "L8", 8, _SZ, _L8),
    _packed(0x01, "AL8", 8, _SZ, _AL8),
    _packed(0x02, "A1R5G5B5", 16, _SZ, _A1R5G5B5),
    _packed(0x03, "X1R5G5B5", 16, _SZ, _X1R5G5B5),
    _packed(0x04, "A4R4G4B4", 16, _SZ, _A4R4G4B4),
    _packed(0x05, "R5G6B5", 16, _SZ, _R5G6B5),
    _packed(0x06, "A8R8G8B8", 32, _SZ, _A8R8G8B8),
    _packed(0x07, "X8R8G8B8", 32, _SZ, _X8R8G8B8),
    TextureFormat(0x0B, "P8", 8, _SZ, Codec.PALETTE),
    TextureFormat(0x0C, "DXT1", 4, _CMP, Codec.BC1),
    TextureFormat(0x0E, "DXT3", 8, _CMP, Codec.BC2, aliases=("DXT2",)),
    TextureFormat(0x0F, "DXT5", 8, _CMP, Codec.BC3, aliases=("DXT4",)),
    _packed(0x10, "LIN_A1R5G5B5", 16, _LIN, _A1R5G5B5),
    _packed(0x11, "LIN_R5G6B5", 16, _LIN, _R5G6B5),
    _packed(0x12, "LIN_A8R8G8B8", 32, _LIN, _A8R8G8B8),
    _packed(0x13, "LIN_L8", 8, _LIN, _L8),
    TextureFormat(0x16, "LIN_R8B8", 16, _LIN, _NO),
    TextureFormat(0x17, "LIN_G8B8", 16, _LIN, _NO, aliases=("LIN_V8U8",)),
    _packed(0x19, "A8", 8, _SZ, _A8),
    _packed(0x1A, "A8L8", 16, _SZ, _A8L8),
    _packed(0x1B, "LIN_AL8", 8, _LIN, _AL8),
    _packed(0x1C, "LIN_X1R5G5B5", 16, _LIN, _X1R5G5B5),
    _packed(0x1D, "LIN_A4R4G4B4", 16, _LIN, _A4R4G4B4),
    _packed(0x1E, "LIN_X8R8G8B8", 32, _LIN, _X8R8G8B8),
    _packed(0x1F, "LIN_A8", 8, _LIN, _A8),
    _packed(0x20, "LIN_A8L8", 16, _LIN, _A8L8),
    TextureFormat(0x24, "YUY2", 16, _LIN, _NO),
    TextureFormat(0x25, "UYVY", 16, _LIN, _NO),
    TextureFormat(0x27, "L6V5U5", 16, _SZ, Codec.L6V5U5, aliases=("R6G5B5",)),
    TextureFormat(0x28, "V8U8", 16, _SZ, Codec.V8U8, aliases=("G8B8",)),
    TextureFormat(0x29, "R8B8", 16, _SZ, _NO),
    TextureFormat(0x2A, "D24S8", 32, _SZ, _NO),
    TextureFormat(0x2B, "F24S8", 32, _SZ, _NO),
    TextureFormat(0x2C, "D16", 16, _SZ, _NO),
    TextureFormat(0x2D, "F16", 16, _SZ, _NO),
    TextureFormat(0x2E, "LIN_D24S8", 32, _LIN, _NO),
    TextureFormat(0x2F, "LIN_F24S8", 32, _LIN, _NO),
    TextureFormat(0x30, "LIN_D16", 16, _LIN, _NO),
    TextureFormat(0x31, "LIN_F16", 16, _LIN, _NO),
    _packed(0x32, "L16", 16, _SZ, _L16),
    TextureFormat(0x33, "V16U16", 32, _SZ, _NO),
    _packed(0x35, "LIN_L16", 16, _LIN, _L16),
    TextureFormat(0x36, "LIN_V16U16", 32, _LIN, _NO),
    TextureFormat(0x37, "LIN_L6V5U5", 16, _LIN, Codec.L6V5U5, aliases=("LIN_R6G5B5",)),
    _packed(0x38, "R5G5B5A1", 16, _SZ, _R5G5B5A1),
    _packed(0x39, "R4G4B4A4", 16, _SZ, _R4G4B4A4),
    _packed(0x3A, "A8B8G8R8", 32, _SZ, _A8B8G8R8),
    _packed(0x3B, "B8G8R8A8", 32, _SZ, _B8G8R8A8),
    _packed(0x3C, "R8G8B8A8", 32, _SZ, _R8G8B8A8),
    _packed(0x3D, "LIN_R5G5B5A1", 16, _LIN, _R5G5B5A1),
    _packed(0x3E, "LIN_R4G4B4A4", 16, _LIN, _R4G4B4A4),
    _packed(0x3F, "LIN_A8B8G8R8", 32, _LIN, _A8B8G8R8),
    _packed(0x40, "LIN_B8G8R8A8", 32, _LIN, _B8G8R8A8),
    _packed(0x41, "LIN_R8G8B8A8", 32, _LIN, _R8G8B8A8),
)

FORMATS: Final[Mapping[int, TextureFormat]] = MappingProxyType({f.code: f for f in _TABLE})


def lookup_format(code: int) -> TextureFormat | None:
    """The format with that Xbox D3DFORMAT code, or `None` if it is not in the table."""
    return FORMATS.get(code)
