"""Xbox Packed Resource (XPR0): parsing, texture decoding and PNG export.

An XPR0 is the Xbox GPU resource container: textures, palettes and buffers exactly
as the NV2A expects them. In Ninja Gaiden Black it appears standalone (`Media/*.xpr`,
`menu.spr`) or inside a KMD chunk (a model's textures).

Modules, bottom-up:

- `formats`: Xbox D3DFORMAT table (name, bpp, swizzled/linear/compressed, codec).
- `swizzle`: NV2A Morton order and un-swizzling.
- `dxt`, `pixels`: decoders to RGBA8 (DXT1/3/5; ARGB, 565, 1555, 4444, A8, L8, P8...).
- `bump`: signed bump (V8U8, L6V5U5) to RGBA8 with a documented normal-map mapping.
- `resources`: XPR0 header and typed resources (Texture, Palette, buffers, UserData).
- `texture`: mips, validation and level-0 decoding of a texture.
- `catalog`: which textures exist and which can be exported (shared by list and export).
- `png`: PNG writer using only the stdlib.
- `source`: opening a standalone XPR0 or the XPR0 slot of a KMD.
- `export`: PNG and manifest, always outside any repo (`ngb_tools._paths`).
- `cli`: `python -I -m ngb_tools.xpr {list,export}`.

The reading and decoding API is re-exported here; the rest is imported from its module.
"""

from __future__ import annotations

from ngb_tools.xpr.errors import (
    TextureTooLargeError,
    UnsupportedTextureError,
    XprError,
    XprFormatError,
    XprInputError,
    XprTruncatedError,
)
from ngb_tools.xpr.formats import FORMATS, Codec, Storage, TextureFormat, lookup_format
from ngb_tools.xpr.resources import (
    FormatWord,
    IndexBuffer,
    Palette,
    PaletteLink,
    PaletteSource,
    PushBuffer,
    Resource,
    SizeWord,
    Texture,
    UserData,
    VertexBuffer,
    XprFile,
    parse_xpr,
)
from ngb_tools.xpr.texture import (
    AlphaMode,
    RgbaImage,
    TextureCheck,
    alpha_mode,
    check_texture,
    decode_texture,
    texture_layout,
)

__all__ = [
    "FORMATS",
    "AlphaMode",
    "Codec",
    "FormatWord",
    "IndexBuffer",
    "Palette",
    "PaletteLink",
    "PaletteSource",
    "PushBuffer",
    "Resource",
    "RgbaImage",
    "SizeWord",
    "Storage",
    "Texture",
    "TextureCheck",
    "TextureFormat",
    "TextureTooLargeError",
    "UnsupportedTextureError",
    "UserData",
    "VertexBuffer",
    "XprError",
    "XprFile",
    "XprFormatError",
    "XprInputError",
    "XprTruncatedError",
    "alpha_mode",
    "check_texture",
    "decode_texture",
    "lookup_format",
    "parse_xpr",
    "texture_layout",
]
