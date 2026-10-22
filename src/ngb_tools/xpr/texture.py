"""From texture header to image: mip layout and level-0 decoding.

**Mips.** A texture with N levels stores the base image and its half-size reductions one
after another, with no padding: level k measures `max(1, width >> k) x max(1, height >> k)`
and starts where k-1 ends. Each level occupies:

- swizzled: `width * height * bytes_per_pixel` (and each level is swizzled separately);
- DXT: one 4x4 block per chunk, even if the level is smaller (2x2 and 1x1 spend a whole
  block): `ceil(width/4) * ceil(height/4) * bytes_per_block`.

Like xemu, levels that would go below 1x1 are discarded: at most `max(log2 width,
log2 height) + 1`. Linear textures have no mips and measure `pitch * height`.

If the small mips run past the XPR0 but level 0 is complete, level 0 is exported
and the manifest warns. In the game this never happens: 4280 textures with mips (up to 11 levels)
in 1413 XPR0s, all complete.

**Decoding.** Level-0 data is un-swizzled if needed (the indices in
P8, the pixels in the rest) and then passed to the format's decoder. Cubemaps (6 faces
aligned to 128 bytes) and volume textures are detected and rejected: unsupported.

**Hostile data.** Everything is validated before reading or allocating: dimensions (at most
`MAX_TEXTURE_PIXELS`), texture and palette ranges inside the XPR0, and pitch.

Source of the sizes: xemu's `pgraph_get_texture_length` and level upload,
https://github.com/xemu-project/xemu/blob/master/hw/xbox/nv2a/pgraph/texture.c
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import StrEnum
from typing import Final

from ngb_tools.xpr.bump import decode_bump
from ngb_tools.xpr.dxt import compressed_size, decode_bc1, decode_bc2, decode_bc3
from ngb_tools.xpr.errors import (
    TextureTooLargeError,
    UnsupportedTextureError,
    XprFormatError,
)
from ngb_tools.xpr.formats import Codec, Storage, TextureFormat, lookup_format
from ngb_tools.xpr.pixels import decode_packed, decode_palette
from ngb_tools.xpr.resources import PaletteLink, Texture, XprFile
from ngb_tools.xpr.swizzle import unswizzle

# Cap on level-0 pixels. The log2 fields of `Format` reach 15 (32768x32768:
# 4 GiB in RGBA); the largest real texture is 1024x512 (dlcustom.xpr). It is checked
# before reading or allocating anything.
MAX_TEXTURE_PIXELS: Final = 4096 * 4096


@dataclass(frozen=True, slots=True)
class MipLevel:
    """A mip level: dimensions and bytes `[offset, offset + size)` from `Data`."""

    level: int
    width: int
    height: int
    offset: int
    size: int


@dataclass(frozen=True, slots=True)
class TextureLayout:
    """Format, dimensions and levels of a 2D texture."""

    format: TextureFormat
    width: int
    height: int
    pitch: int | None
    levels: tuple[MipLevel, ...]

    @property
    def byte_size(self) -> int:
        """Bytes of all the levels together (what is hashed in the manifest)."""
        last = self.levels[-1]
        return last.offset + last.size


@dataclass(frozen=True, slots=True)
class RgbaImage:
    """RGBA8 pixels row by row, top to bottom."""

    width: int
    height: int
    pixels: bytes

    def __post_init__(self) -> None:
        if len(self.pixels) != 4 * self.width * self.height:
            raise ValueError(f"{len(self.pixels)} bytes for {self.width}x{self.height} RGBA")


def _level_size(fmt: TextureFormat, width: int, height: int) -> int:
    if fmt.is_compressed:
        return compressed_size(width, height, fmt.block_bytes)
    return width * height * fmt.bytes_per_element


def mip_chain(fmt: TextureFormat, width: int, height: int, levels: int) -> tuple[MipLevel, ...]:
    """The `levels` levels of a swizzled or DXT texture, already clipped to 1x1."""
    fitting = max(width.bit_length(), height.bit_length())  # = max(log2) + 1
    chain: list[MipLevel] = []
    offset = 0
    for level in range(max(1, min(levels, fitting))):
        w, h = max(1, width >> level), max(1, height >> level)
        size = _level_size(fmt, w, h)
        chain.append(MipLevel(level, w, h, offset, size))
        offset += size
    return tuple(chain)


def _check_pixels(fmt: TextureFormat, width: int, height: int) -> None:
    if width * height > MAX_TEXTURE_PIXELS:
        raise TextureTooLargeError(
            f"{fmt.name} {width}x{height}: more than {MAX_TEXTURE_PIXELS} pixels (defensive cap)"
        )


def texture_layout(texture: Texture) -> TextureLayout:
    """Layout of a 2D texture; cubemaps, volumes and unknown formats fail."""
    word = texture.format
    fmt = lookup_format(word.code)
    if fmt is None:
        raise UnsupportedTextureError(f"unknown format {word.code:#04x}")
    if word.cubemap:
        raise UnsupportedTextureError(f"cubemap {fmt.name} {word.width}x{word.height}: unsupported")
    if word.dimensions == 3:
        raise UnsupportedTextureError(
            f"volume {fmt.name} {word.width}x{word.height}x{word.depth}: unsupported"
        )
    if word.dimensions != 2:
        raise UnsupportedTextureError(f"{word.dimensions} dimensions: expected 2 or 3")
    if fmt.storage is Storage.LINEAR:
        size = texture.size
        if not size.is_set:
            raise UnsupportedTextureError(f"linear {fmt.name} without a Size field")
        _check_pixels(fmt, size.width, size.height)
        if size.pitch < size.width * fmt.bytes_per_element:
            raise UnsupportedTextureError(
                f"{fmt.name} {size.width}x{size.height}: pitch {size.pitch} smaller than a row"
            )
        level = MipLevel(0, size.width, size.height, 0, size.pitch * size.height)
        return TextureLayout(fmt, size.width, size.height, size.pitch, (level,))
    _check_pixels(fmt, word.width, word.height)
    chain = mip_chain(fmt, word.width, word.height, word.levels)
    return TextureLayout(fmt, word.width, word.height, None, chain)


@dataclass(frozen=True, slots=True)
class TextureCheck:
    """Result of `check_texture`: layout, complete levels and palette (if P8)."""

    layout: TextureLayout
    present_levels: int
    palette: PaletteLink | None

    @property
    def missing_levels(self) -> int:
        return len(self.layout.levels) - self.present_levels

    @property
    def present_bytes(self) -> int:
        """Bytes of the present levels (what is hashed in the manifest)."""
        last = self.layout.levels[self.present_levels - 1]
        return last.offset + last.size


def _palette_link(xpr: XprFile, texture: Texture) -> PaletteLink:
    link = xpr.palette_link(texture)
    if link is None:
        raise UnsupportedTextureError(
            f"P8 without an associated palette (header {texture.index}, "
            f"{len(xpr.palettes)} palettes)"
        )
    return link


def _present_levels(xpr: XprFile, texture: Texture, layout: TextureLayout) -> int:
    """Complete levels inside the data region, counting from 0."""
    present = 0
    for level in layout.levels:
        if texture.data + level.offset + level.size > xpr.data_size:
            break
        present += 1
    return present


def check_texture(xpr: XprFile, texture: Texture) -> TextureCheck:
    """Check, without decoding, that level 0 of the texture can be exported.

    Fails with `UnsupportedTextureError` (cubemap, volume, size, format or palette) or with
    `XprTruncatedError` if level 0 or the palette run past the XPR0. If smaller mips are
    missing, it does not fail: `present_levels` says how many there are (in the game they are
    never missing).
    """
    layout = texture_layout(texture)
    if not layout.format.is_decodable:
        raise UnsupportedTextureError(f"format {layout.format.name} has no decoder")
    present = _present_levels(xpr, texture, layout)
    if present == 0:
        xpr.require_data(texture.data, layout.levels[0].size)  # raises XprTruncatedError
    link = None
    if layout.format.codec is Codec.PALETTE:
        link = _palette_link(xpr, texture)
        xpr.require_data(link.palette.data, link.palette.byte_size)
    return TextureCheck(layout, present, link)


def _decode(
    fmt: TextureFormat, raw: bytes, width: int, height: int, *, pitch: int | None, palette: bytes
) -> bytes:
    if fmt.storage is Storage.SWIZZLED:
        raw = unswizzle(raw, width, height, fmt.bytes_per_element)
    match fmt.codec:
        case Codec.BC1:
            return decode_bc1(raw, width, height)
        case Codec.BC2:
            return decode_bc2(raw, width, height)
        case Codec.BC3:
            return decode_bc3(raw, width, height)
        case Codec.PALETTE:
            return decode_palette(raw, palette)
        case Codec.PACKED:
            return decode_packed(raw, width, height, fmt, pitch=pitch)
        case Codec.V8U8 | Codec.L6V5U5:
            return decode_bump(raw, width, height, fmt, pitch=pitch)
        case Codec.UNSUPPORTED:
            raise UnsupportedTextureError(f"format {fmt.name} has no decoder")


def decode_texture(xpr: XprFile, texture: Texture) -> RgbaImage:
    """Level 0 of the texture as RGBA8.

    This is the public API: it only raises typed errors. A `ValueError` from the decoders
    (swizzle, DXT, pixels) is wrapped in `XprFormatError`.
    """
    check = check_texture(xpr, texture)
    layout = check.layout
    fmt = layout.format
    base = layout.levels[0]
    palette = b""
    if check.palette is not None:
        entry = check.palette.palette
        palette = xpr.data_bytes(entry.data, entry.byte_size)
    raw = xpr.data_bytes(texture.data + base.offset, base.size)
    try:
        pixels = _decode(fmt, raw, base.width, base.height, pitch=layout.pitch, palette=palette)
        return RgbaImage(base.width, base.height, pixels)
    except ValueError as error:
        raise XprFormatError(f"texture {texture.index} ({fmt.name}): {error}") from error


class AlphaMode(StrEnum):
    """What is known about the alpha of an exported texture (the PNG is always straight alpha)."""

    STRAIGHT = "straight"  # the format has no premultiplied twin
    STRAIGHT_PROVEN = "straight-proven"  # DXT2-5: the pixels rule out premultiplied
    STRAIGHT_ASSUMED = "straight-assumed"  # DXT2-5: cannot be told apart; straight is assumed
    LUMINANCE = "luminance"  # L6V5U5: A is the luminance L, not transparency (see `bump`)


def alpha_mode(fmt: TextureFormat, rgba: bytes) -> AlphaMode:
    """For DXT2/3 and DXT4/5 (same code on Xbox), whether the pixels prove straight alpha.

    A premultiplied color satisfies R, G, B <= A at every pixel. If some channel exceeds
    its alpha, premultiplied is ruled out; if everything is opaque, the two coincide. Otherwise,
    it cannot be known and straight is assumed (DXT3/DXT5), which is what xemu and Cxbx do.
    In L6V5U5 the A channel carries luminance (`LUMINANCE`).
    """
    if fmt.codec is Codec.L6V5U5:
        return AlphaMode.LUMINANCE
    if fmt.codec not in (Codec.BC2, Codec.BC3):
        return AlphaMode.STRAIGHT
    alpha = rgba[3::4]
    if alpha.count(255) == len(alpha):
        return AlphaMode.STRAIGHT_PROVEN
    for k in range(3):
        if any(c > a for c, a in zip(rgba[k::4], alpha, strict=True)):
            return AlphaMode.STRAIGHT_PROVEN
    return AlphaMode.STRAIGHT_ASSUMED
