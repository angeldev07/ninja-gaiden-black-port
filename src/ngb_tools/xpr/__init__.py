"""Xbox Packed Resource (XPR0): parseo, decodificación de texturas y exportación a PNG.

Un XPR0 es el contenedor de recursos de GPU de la Xbox: texturas, paletas y buffers tal
como los espera la NV2A. En Ninja Gaiden Black aparece suelto (`Media/*.xpr`, `menu.spr`)
o dentro del chunk de un KMD (las texturas de un modelo).

Módulos, de abajo arriba:

- `formats`: tabla de D3DFORMAT de Xbox (nombre, bpp, swizzled/linear/compressed, códec).
- `swizzle`: orden Morton de la NV2A y des-swizzle.
- `dxt`, `pixels`: decodificadores a RGBA8 (DXT1/3/5; ARGB, 565, 1555, 4444, A8, L8, P8...).
- `resources`: cabecera XPR0 y recursos tipados (Texture, Palette, buffers, UserData).
- `texture`: mips, validación y decodificación del nivel 0 de una textura.
- `catalog`: qué texturas hay y cuáles se pueden exportar (lo comparten list y export).
- `png`: escritor PNG con la stdlib.
- `source`: apertura de un XPR0 suelto o del slot XPR0 de un KMD.
- `export`: PNG y manifiesto, siempre fuera de cualquier repo (`ngb_tools._paths`).
- `cli`: `python -I -m ngb_tools.xpr {list,export}`.

Aquí se reexporta la API de lectura y decodificación; el resto se importa de su módulo.
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
