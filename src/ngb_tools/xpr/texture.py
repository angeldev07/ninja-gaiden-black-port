"""De cabecera de textura a imagen: disposición de los mips y decodificación del nivel 0.

**Mips.** Una textura con N niveles guarda la imagen base y sus reducciones a la mitad una
tras otra, sin relleno: el nivel k mide `max(1, ancho >> k) x max(1, alto >> k)` y empieza
donde acaba el k-1. Cada nivel ocupa:

- swizzled: `ancho * alto * bytes_por_píxel` (y cada nivel se swizzlea por separado);
- DXT: un bloque de 4x4 por cada trozo, aunque el nivel sea menor (2x2 y 1x1 gastan un
  bloque entero): `ceil(ancho/4) * ceil(alto/4) * bytes_por_bloque`.

Como xemu, se descartan los niveles que bajarían de 1x1: como mucho `max(log2 ancho,
log2 alto) + 1`. Las texturas lineales no tienen mips y miden `pitch * alto`.

Si los mips pequeños se salen del XPR0 pero el nivel 0 está entero, se exporta el nivel 0
y el manifiesto avisa. En el juego nunca pasa: 4280 texturas con mips (hasta 11 niveles)
en 1413 XPR0, todas completas.

**Decodificación.** Los datos del nivel 0 se des-swizzlean si hace falta (los índices en
P8, los píxeles en el resto) y después pasan al decodificador del formato. Cubemaps (6 caras
alineadas a 128 bytes) y texturas de volumen se detectan y se rechazan: no hay soporte.

**Datos hostiles.** Todo se valida antes de leer o reservar: dimensiones (como mucho
`MAX_TEXTURE_PIXELS`), rangos de la textura y de su paleta dentro del XPR0 y pitch.

Fuente de los tamaños: `pgraph_get_texture_length` y la subida de niveles de xemu,
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

# Tope de píxeles del nivel 0. Los campos log2 de `Format` llegan a 15 (32768x32768:
# 4 GiB en RGBA); la mayor textura real es de 1024x512 (dlcustom.xpr). Se comprueba
# antes de leer o reservar nada.
MAX_TEXTURE_PIXELS: Final = 4096 * 4096


@dataclass(frozen=True, slots=True)
class MipLevel:
    """Un nivel de mip: dimensiones y bytes `[offset, offset + size)` desde `Data`."""

    level: int
    width: int
    height: int
    offset: int
    size: int


@dataclass(frozen=True, slots=True)
class TextureLayout:
    """Formato, dimensiones y niveles de una textura 2D."""

    format: TextureFormat
    width: int
    height: int
    pitch: int | None
    levels: tuple[MipLevel, ...]

    @property
    def byte_size(self) -> int:
        """Bytes de todos los niveles juntos (lo que se hashea en el manifiesto)."""
        last = self.levels[-1]
        return last.offset + last.size


@dataclass(frozen=True, slots=True)
class RgbaImage:
    """Píxeles RGBA8 fila a fila, de arriba abajo."""

    width: int
    height: int
    pixels: bytes

    def __post_init__(self) -> None:
        if len(self.pixels) != 4 * self.width * self.height:
            raise ValueError(f"{len(self.pixels)} bytes para {self.width}x{self.height} RGBA")


def _level_size(fmt: TextureFormat, width: int, height: int) -> int:
    if fmt.is_compressed:
        return compressed_size(width, height, fmt.block_bytes)
    return width * height * fmt.bytes_per_element


def mip_chain(fmt: TextureFormat, width: int, height: int, levels: int) -> tuple[MipLevel, ...]:
    """Los `levels` niveles de una textura swizzled o DXT, ya recortados a 1x1."""
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
            f"{fmt.name} {width}x{height}: más de {MAX_TEXTURE_PIXELS} píxeles (tope defensivo)"
        )


def texture_layout(texture: Texture) -> TextureLayout:
    """Disposición de una textura 2D; cubemaps, volúmenes y formatos desconocidos fallan."""
    word = texture.format
    fmt = lookup_format(word.code)
    if fmt is None:
        raise UnsupportedTextureError(f"formato desconocido {word.code:#04x}")
    if word.cubemap:
        raise UnsupportedTextureError(f"cubemap {fmt.name} {word.width}x{word.height}: sin soporte")
    if word.dimensions == 3:
        raise UnsupportedTextureError(
            f"volumen {fmt.name} {word.width}x{word.height}x{word.depth}: sin soporte"
        )
    if word.dimensions != 2:
        raise UnsupportedTextureError(f"{word.dimensions} dimensiones: se esperaban 2 o 3")
    if fmt.storage is Storage.LINEAR:
        size = texture.size
        if not size.is_set:
            raise UnsupportedTextureError(f"{fmt.name} lineal sin campo Size")
        _check_pixels(fmt, size.width, size.height)
        if size.pitch < size.width * fmt.bytes_per_element:
            raise UnsupportedTextureError(
                f"{fmt.name} {size.width}x{size.height}: pitch {size.pitch} menor que una fila"
            )
        level = MipLevel(0, size.width, size.height, 0, size.pitch * size.height)
        return TextureLayout(fmt, size.width, size.height, size.pitch, (level,))
    _check_pixels(fmt, word.width, word.height)
    chain = mip_chain(fmt, word.width, word.height, word.levels)
    return TextureLayout(fmt, word.width, word.height, None, chain)


@dataclass(frozen=True, slots=True)
class TextureCheck:
    """Resultado de `check_texture`: disposición, niveles completos y paleta (si es P8)."""

    layout: TextureLayout
    present_levels: int
    palette: PaletteLink | None

    @property
    def missing_levels(self) -> int:
        return len(self.layout.levels) - self.present_levels

    @property
    def present_bytes(self) -> int:
        """Bytes de los niveles presentes (lo que se hashea en el manifiesto)."""
        last = self.layout.levels[self.present_levels - 1]
        return last.offset + last.size


def _palette_link(xpr: XprFile, texture: Texture) -> PaletteLink:
    link = xpr.palette_link(texture)
    if link is None:
        raise UnsupportedTextureError(
            f"P8 sin paleta asociada (cabecera {texture.index}, {len(xpr.palettes)} paletas)"
        )
    return link


def _present_levels(xpr: XprFile, texture: Texture, layout: TextureLayout) -> int:
    """Niveles completos dentro de la región de datos, contando desde el 0."""
    present = 0
    for level in layout.levels:
        if texture.data + level.offset + level.size > xpr.data_size:
            break
        present += 1
    return present


def check_texture(xpr: XprFile, texture: Texture) -> TextureCheck:
    """Comprueba, sin decodificar, que el nivel 0 de la textura se puede exportar.

    Falla con `UnsupportedTextureError` (cubemap, volumen, tamaño, formato o paleta) o con
    `XprTruncatedError` si el nivel 0 o la paleta se salen del XPR0. Si faltan mips más
    pequeños, no falla: `present_levels` dice cuántos hay (en el juego nunca faltan).
    """
    layout = texture_layout(texture)
    if not layout.format.is_decodable:
        raise UnsupportedTextureError(f"formato {layout.format.name} sin decodificador")
    present = _present_levels(xpr, texture, layout)
    if present == 0:
        xpr.require_data(texture.data, layout.levels[0].size)  # lanza XprTruncatedError
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
            raise UnsupportedTextureError(f"formato {fmt.name} sin decodificador")


def decode_texture(xpr: XprFile, texture: Texture) -> RgbaImage:
    """El nivel 0 de la textura como RGBA8.

    Es la API pública: solo lanza errores tipados. Un `ValueError` de los decodificadores
    (swizzle, DXT, píxeles) se envuelve en `XprFormatError`.
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
        raise XprFormatError(f"textura {texture.index} ({fmt.name}): {error}") from error


class AlphaMode(StrEnum):
    """Qué se sabe del alfa de una textura exportada (el PNG siempre es alfa recto)."""

    STRAIGHT = "straight"  # el formato no tiene gemelo premultiplicado
    STRAIGHT_PROVEN = "straight-proven"  # DXT2-5: los píxeles descartan el premultiplicado
    STRAIGHT_ASSUMED = "straight-assumed"  # DXT2-5: no se puede distinguir; se supone recto
    LUMINANCE = "luminance"  # L6V5U5: A es la luminancia L, no transparencia (ver `bump`)


def alpha_mode(fmt: TextureFormat, rgba: bytes) -> AlphaMode:
    """Para DXT2/3 y DXT4/5 (mismo código en Xbox), si los píxeles prueban el alfa recto.

    Un color premultiplicado cumple R, G, B <= A en todos los píxeles. Si algún canal supera
    a su alfa, el premultiplicado queda descartado; si todo es opaco, los dos coinciden. Si
    no, no se puede saber y se supone recto (DXT3/DXT5), que es lo que hacen xemu y Cxbx.
    En L6V5U5 el canal A lleva la luminancia (`LUMINANCE`).
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
