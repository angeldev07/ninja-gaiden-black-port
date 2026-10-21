"""Tabla de formatos de textura de Xbox: código D3DFORMAT -> nombre, bpp, almacenamiento y códec.

En la Xbox, el D3DFORMAT de una textura es directamente el código de color de la NV2A
(`NV097_SET_TEXTURE_FORMAT_COLOR_*`) y viaja en los bits 8-15 del campo `Format`. Por eso
los valores no coinciden con los de D3D8 de PC. Cada formato se guarda de una de tres
maneras:

- **swizzled** (`SZ_*` en la NV2A): dimensiones potencia de dos, píxeles en orden Morton
  (ver `swizzle`). Admiten mipmaps.
- **linear** (`LU_IMAGE_*`, los `LIN_*` de D3D): filas consecutivas con un *pitch* (bytes
  por fila, múltiplo de 64) y dimensiones libres, que vienen en el campo `Size`. Sin mips.
- **compressed** (`L_DXT*`): bloques S3TC de 4x4 píxeles en orden lineal, sin swizzle.
  DXT2 y DXT3 comparten código (0x0E), igual que DXT4 y DXT5 (0x0F): la NV2A no distingue
  el alfa premultiplicado, así que se decodifican como DXT3 y DXT5.

V8U8 y L6V5U5 (y su variante lineal) son mapas de relieve con signo y se exportan con el
mapeo de `bump`. Los demás formatos con signo (V16U16), de profundidad (D16, D24S8...) y
YUV (YUY2, UYVY) se conocen pero no se decodifican: no aparecen en el juego.

Los canales de los formatos empaquetados se describen sobre la palabra little endian del
píxel, con la convención de D3D de nombrar del bit más alto al más bajo: en A1R5G5B5, A es
el bit 15 y B los bits 0-4. Cuando el formato no trae un canal, se usa una constante: A8
deja el color en blanco (como Cxbx) y L8 repite la luminancia en R, G y B.

Fuentes (enum y semántica verificados; sin usar el XDK):
- Cxbx-Reloaded, enum `X_D3DFMT_*` y máscaras de `Format`/`Size`:
  https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/master/src/core/hle/D3D8/XbD3D8Types.h
- Cxbx-Reloaded, tabla `FormatInfos` (bpp, Swzzld/Linear/Cmprsd) y orden de canales:
  https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/master/src/core/hle/D3D8/XbConvert.cpp
- xemu, códigos `NV097_SET_TEXTURE_FORMAT_COLOR_*`:
  https://github.com/xemu-project/xemu/blob/master/hw/xbox/nv2a/nv2a_regs.h
- xemu, bytes por píxel y tamaño de cada nivel (lineal, swizzled y DXT):
  https://github.com/xemu-project/xemu/blob/master/hw/xbox/nv2a/pgraph/texture.c
"""

from __future__ import annotations

from collections.abc import Mapping
from dataclasses import dataclass
from enum import StrEnum
from types import MappingProxyType
from typing import Final


class Storage(StrEnum):
    """Cómo están colocados los datos de la textura en memoria."""

    SWIZZLED = "swizzled"
    LINEAR = "linear"
    COMPRESSED = "compressed"


class Codec(StrEnum):
    """Qué decodificador convierte los datos (ya en orden lineal) a RGBA8."""

    PACKED = "packed"
    PALETTE = "palette"
    BC1 = "bc1"
    BC2 = "bc2"
    BC3 = "bc3"
    V8U8 = "v8u8"  # relieve con signo, ver `bump`
    L6V5U5 = "l6v5u5"
    UNSUPPORTED = "unsupported"


@dataclass(frozen=True, slots=True)
class Field:
    """Un canal dentro de la palabra del píxel: `bits` bits a partir del bit `shift`."""

    shift: int
    bits: int


# Un canal es un campo de la palabra o una constante 0-255 si el formato no lo trae.
type Channel = Field | int


@dataclass(frozen=True, slots=True)
class PackedLayout:
    """Dónde está cada canal RGBA en la palabra little endian de un píxel."""

    red: Channel
    green: Channel
    blue: Channel
    alpha: Channel


@dataclass(frozen=True, slots=True)
class TextureFormat:
    """Un D3DFORMAT de Xbox y lo necesario para decodificarlo."""

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
        """V8U8 o L6V5U5: el PNG es un mapa de relieve, no un color (ver `bump`)."""
        return self.codec in (Codec.V8U8, Codec.L6V5U5)

    @property
    def is_compressed(self) -> bool:
        return self.storage is Storage.COMPRESSED

    @property
    def bytes_per_element(self) -> int:
        """Bytes por píxel (o por índice de paleta) de un formato sin comprimir."""
        return self.bits_per_pixel // 8

    @property
    def block_bytes(self) -> int:
        """Bytes de un bloque de 4x4 píxeles: 8 en DXT1 y 16 en DXT3/DXT5."""
        return 16 * self.bits_per_pixel // 8


# --- Disposiciones de canales -------------------------------------------------------------

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
_AL8: Final = _luminance(Field(0, 8), Field(0, 8))  # alfa y luminancia del mismo byte
_A8L8: Final = _luminance(Field(0, 8), Field(8, 8))
_L16: Final = _luminance(Field(0, 16))
_A8: Final = PackedLayout(red=_WHITE, green=_WHITE, blue=_WHITE, alpha=Field(0, 8))


# --- Tabla --------------------------------------------------------------------------------

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
    """El formato con ese código D3DFORMAT de Xbox, o `None` si no está en la tabla."""
    return FORMATS.get(code)
