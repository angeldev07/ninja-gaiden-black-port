"""Parseo de XPR0 (Xbox Packed Resource): cabecera, recursos tipados y región de datos.

Un XPR0 empaqueta objetos de Direct3D de Xbox listos para la GPU. Todo en u32 little endian:

    0x00  "XPR0"
    0x04  total_size   bytes del XPR0 entero
    0x08  header_size  donde empiezan los datos de GPU (relativo al inicio del XPR0)
    0x0C  cabeceras de recurso, una tras otra, hasta un u32 0xFFFFFFFF

Cada cabecera de recurso es la estructura D3D que el juego usa tal cual en memoria. Empieza
por `Common` (bits 0-15: contador de referencias; bits 16-18: tipo) y `Data`, el offset de
los bytes del recurso **relativo a `header_size`** (verificado: en `ryu.kmd` el primer
índice está en `header_size + 0` y la primera textura en un `header_size + Data` alineado a
0x1000). Los tamaños de cada tipo salen de las estructuras de Cxbx-Reloaded:

    tipo  recurso        bytes  campos
    0     vertex buffer  12     Common, Data, Lock
    1     index buffer   12     Common, Data, Lock
    2     push buffer    24     Common, Data, Lock, Size, AllocationSize, ?
    3     paleta         12     Common, Data, Lock   (bits 30-31: 256/128/64/32 entradas)
    4     textura        20     Common, Data, Lock, Format, Size

El push buffer de Cxbx mide 20 bytes, pero en `ryu.kmd` y `r_ryu.kmd` cada uno ocupa 24:
le sigue un u32 que siempre vale 0 (INFERRED: otra versión del XDK o un campo de fixup).

Team Ninja intercala además bloques propios `u32 0x80000000, u32 tamaño, carga útil`
(USERDATA; observado, no documentado), con los materiales y las mallas del modelo.

`Format` (textura) empaqueta: bits 0-1 canal DMA, 2 cubemap, 3 color de borde, 4-7
dimensiones (2 o 3), 8-15 D3DFORMAT, 16-19 niveles de mip, 20-23/24-27/28-31 log2 del
ancho, alto y profundidad. `Size` solo se usa en texturas lineales (no potencia de dos):
bits 0-11 ancho-1, 12-23 alto-1 y 24-31 pitch/64-1.

Fuentes:
- xboxdevwiki, formato XPR: https://xboxdevwiki.net/XPR
- Cxbx-Reloaded, `X_D3DResource`, `X_D3DPixelContainer`, máscaras `X_D3DCOMMON_*`,
  `X_D3DFORMAT_*` y `X_D3DSIZE_*`:
  https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/master/src/core/hle/D3D8/XbD3D8Types.h
"""

from __future__ import annotations

import struct
from collections.abc import Buffer
from dataclasses import dataclass, field
from enum import IntEnum, StrEnum
from types import MappingProxyType
from typing import Final

from ngb_tools.xpr.errors import XprFormatError, XprTruncatedError

XPR_MAGIC: Final = b"XPR0"
XPR_HEADER_SIZE: Final = 12
END_MARKER: Final = 0xFFFFFFFF
USERDATA_MARKER: Final = 0x80000000
# Tope de cabeceras de recurso antes del 0xFFFFFFFF. El máximo en los 1413 XPR0 del juego
# (sueltos y dentro de KMD) es 767. 16384 deja margen de sobra y acota la memoria y el
# tiempo ante una lista de cabeceras hostil o sin terminador.
MAX_RESOURCES: Final = 16384

_U32: Final = struct.Struct("<I")
_BUFFER: Final = struct.Struct("<3I")
_PIXEL_CONTAINER: Final = struct.Struct("<5I")
_PUSH_BUFFER: Final = struct.Struct("<6I")


class ResourceType(IntEnum):
    """Bits 16-18 de `Common` (`X_D3DCOMMON_TYPE_*`)."""

    VERTEX_BUFFER = 0
    INDEX_BUFFER = 1
    PUSH_BUFFER = 2
    PALETTE = 3
    TEXTURE = 4
    SURFACE = 5
    FIXUP = 6


@dataclass(frozen=True, slots=True)
class FormatWord:
    """Campo `Format` de una textura, decodificado por bits."""

    raw: int

    @property
    def dma_channel(self) -> int:
        return self.raw & 0x3

    @property
    def cubemap(self) -> bool:
        return bool(self.raw & 0x4)

    @property
    def border_color(self) -> bool:
        return bool(self.raw & 0x8)

    @property
    def dimensions(self) -> int:
        return (self.raw >> 4) & 0xF

    @property
    def code(self) -> int:
        """D3DFORMAT de Xbox (ver `formats`)."""
        return (self.raw >> 8) & 0xFF

    @property
    def levels(self) -> int:
        return (self.raw >> 16) & 0xF

    @property
    def log2_width(self) -> int:
        return (self.raw >> 20) & 0xF

    @property
    def log2_height(self) -> int:
        return (self.raw >> 24) & 0xF

    @property
    def log2_depth(self) -> int:
        return (self.raw >> 28) & 0xF

    @property
    def width(self) -> int:
        return 1 << self.log2_width

    @property
    def height(self) -> int:
        return 1 << self.log2_height

    @property
    def depth(self) -> int:
        return 1 << self.log2_depth


@dataclass(frozen=True, slots=True)
class SizeWord:
    """Campo `Size`: dimensiones y pitch de una textura lineal; 0 en las demás."""

    raw: int

    @property
    def is_set(self) -> bool:
        return self.raw != 0

    @property
    def width(self) -> int:
        return (self.raw & 0xFFF) + 1

    @property
    def height(self) -> int:
        return ((self.raw >> 12) & 0xFFF) + 1

    @property
    def pitch(self) -> int:
        return ((self.raw >> 24) + 1) * 64


@dataclass(frozen=True, slots=True)
class Texture:
    """`D3DTexture`. `index` es la posición en la lista de cabeceras y `offset`, la de la
    cabecera dentro del XPR0; `data` es relativo a `header_size`."""

    index: int
    offset: int
    common: int
    data: int
    lock: int
    format: FormatWord
    size: SizeWord


@dataclass(frozen=True, slots=True)
class Palette:
    """`D3DPalette`: entradas D3DCOLOR (u32 ARGB) a partir de `data`."""

    index: int
    offset: int
    common: int
    data: int
    lock: int

    @property
    def entries(self) -> int:
        """`X_D3DPALETTE_COMMON_PALETTESIZE`: 0 = 256, 1 = 128, 2 = 64, 3 = 32 entradas."""
        return 256 >> ((self.common >> 30) & 0x3)

    @property
    def byte_size(self) -> int:
        return 4 * self.entries


@dataclass(frozen=True, slots=True)
class VertexBuffer:
    """`D3DVertexBuffer`: vértices en `data`; su formato lo describe el USERDATA del KMD."""

    index: int
    offset: int
    common: int
    data: int
    lock: int


@dataclass(frozen=True, slots=True)
class IndexBuffer:
    """`D3DIndexBuffer`: índices u16 en `data` (en ryu.kmd, al principio de los datos)."""

    index: int
    offset: int
    common: int
    data: int
    lock: int


@dataclass(frozen=True, slots=True)
class PushBuffer:
    """`D3DPushBuffer`: comandos de GPU pregrabados (`size` bytes en `data`).

    `trailing` es el sexto u32 observado en el juego (siempre 0); Cxbx no lo documenta.
    """

    index: int
    offset: int
    common: int
    data: int
    lock: int
    size: int
    allocation_size: int
    trailing: int


@dataclass(frozen=True, slots=True)
class UserData:
    """Bloque propio de Team Ninja dentro de la lista de cabeceras."""

    index: int
    offset: int
    payload: bytes


type Resource = Texture | Palette | VertexBuffer | IndexBuffer | PushBuffer | UserData


class PaletteSource(StrEnum):
    """Cómo se asoció la paleta a una textura P8 (ver `XprFile.palette_link`)."""

    NEXT = "next"  # la cabecera siguiente a la textura (lo que hace todo el juego)
    ONLY = "only"  # reserva: la única paleta del archivo


@dataclass(frozen=True, slots=True)
class PaletteLink:
    palette: Palette
    source: PaletteSource


@dataclass(frozen=True, slots=True)
class XprFile:
    """Un XPR0 parseado.

    `blob` es una vista (sin copia) de sus `total_size` bytes sobre lo que se pasó a
    `parse_xpr`. `textures` y `palettes` se indexan una sola vez, al construirlo.
    """

    total_size: int
    header_size: int
    resources: tuple[Resource, ...]
    blob: memoryview
    textures: tuple[Texture, ...] = field(init=False)
    palettes: tuple[Palette, ...] = field(init=False)

    def __post_init__(self) -> None:
        textures = tuple(r for r in self.resources if isinstance(r, Texture))
        palettes = tuple(r for r in self.resources if isinstance(r, Palette))
        object.__setattr__(self, "textures", textures)  # dataclass congelada
        object.__setattr__(self, "palettes", palettes)

    @property
    def data_size(self) -> int:
        return self.total_size - self.header_size

    def require_data(self, offset: int, length: int) -> None:
        """Comprueba, sin copiar nada, que `[offset, offset + length)` cabe en los datos."""
        if offset < 0 or length < 0 or offset + length > self.data_size:
            raise XprTruncatedError(
                f"datos [{offset:#x}, {offset + length:#x}) fuera de los {self.data_size:#x} "
                "bytes de la región de datos"
            )

    def data_bytes(self, offset: int, length: int) -> bytes:
        """`length` bytes de la región de datos a partir de `offset` (un campo `Data`)."""
        self.require_data(offset, length)
        start = self.header_size + offset
        return bytes(self.blob[start : start + length])

    def palette_link(self, texture: Texture) -> PaletteLink | None:
        """Paleta de una textura P8 y cómo se dedujo.

        Un XPR0 no enlaza textura y paleta: el juego llama a SetPalette en ejecución. En los
        1413 XPR0 del juego, las 2959 texturas P8 van seguidas de su paleta (en las
        cabeceras y en los datos), así que se usa la cabecera siguiente si es una paleta
        (`NEXT`). Como reserva, la única paleta del archivo (`ONLY`, nunca visto en el
        juego); si no, se desconoce (`None`). Coste O(1): las paletas ya están indexadas.
        """
        following = texture.index + 1
        if following < len(self.resources):
            candidate = self.resources[following]
            if isinstance(candidate, Palette):
                return PaletteLink(candidate, PaletteSource.NEXT)
        if len(self.palettes) == 1:
            return PaletteLink(self.palettes[0], PaletteSource.ONLY)
        return None


_LAYOUTS: Final = MappingProxyType(
    {ResourceType.TEXTURE: _PIXEL_CONTAINER, ResourceType.PUSH_BUFFER: _PUSH_BUFFER}
)


def _parse_resource(blob: memoryview, index: int, pos: int, limit: int) -> tuple[Resource, int]:
    """La cabecera de recurso en `pos` y la posición de la siguiente."""
    (common,) = _U32.unpack_from(blob, pos)
    if common == USERDATA_MARKER:
        if pos + 8 > limit:
            raise XprFormatError(f"userdata en {pos:#x} sin tamaño antes de header_size")
        (size,) = _U32.unpack_from(blob, pos + 4)
        end = pos + 8 + size
        if end > limit:
            raise XprFormatError(f"userdata en {pos:#x} de {size:#x} bytes rebasa header_size")
        return UserData(index, pos, bytes(blob[pos + 8 : end])), end
    kind = (common >> 16) & 0x7
    layout = _LAYOUTS.get(kind, _BUFFER)
    if pos + layout.size > limit:
        raise XprFormatError(f"cabecera de recurso en {pos:#x} cortada por header_size")
    fields = layout.unpack_from(blob, pos)
    end = pos + layout.size
    match kind:
        case ResourceType.TEXTURE:
            _, data, lock, fmt, size = fields
            return Texture(index, pos, common, data, lock, FormatWord(fmt), SizeWord(size)), end
        case ResourceType.PALETTE:
            return Palette(index, pos, *fields), end
        case ResourceType.VERTEX_BUFFER:
            return VertexBuffer(index, pos, *fields), end
        case ResourceType.INDEX_BUFFER:
            return IndexBuffer(index, pos, *fields), end
        case ResourceType.PUSH_BUFFER:
            return PushBuffer(index, pos, *fields), end
        case _:
            raise XprFormatError(
                f"tipo de recurso {kind} no soportado en {pos:#x} (Common = {common:#010x})"
            )


def _parse_resources(blob: memoryview, header_size: int) -> tuple[Resource, ...]:
    resources: list[Resource] = []
    pos = XPR_HEADER_SIZE
    while pos + 4 <= header_size:
        if _U32.unpack_from(blob, pos)[0] == END_MARKER:
            return tuple(resources)
        if len(resources) == MAX_RESOURCES:
            raise XprFormatError(f"más de {MAX_RESOURCES} cabeceras de recurso sin la marca de fin")
        resource, pos = _parse_resource(blob, len(resources), pos, header_size)
        resources.append(resource)
    raise XprFormatError(f"sin marca de fin 0xFFFFFFFF antes de header_size = {header_size:#x}")


def parse_xpr(data: Buffer) -> XprFile:
    """Parsea el XPR0 que empieza en `data[0]`; lo que sobre tras `total_size` se ignora.

    No copia: el `XprFile` guarda una vista de `data` (bytes, o una vista de un KMD).
    """
    view = memoryview(data).cast("B")
    magic = bytes(view[: len(XPR_MAGIC)])  # solo se copian los 4 bytes de la magia
    if len(view) < XPR_HEADER_SIZE or magic != XPR_MAGIC:
        raise XprFormatError(f"magia inválida: {magic!r} (se esperaba {XPR_MAGIC!r})")
    total_size, header_size = struct.unpack_from("<II", view, 4)
    if total_size < XPR_HEADER_SIZE + 4:
        raise XprFormatError(
            f"total_size = {total_size:#x}: no caben la cabecera y la marca de fin"
        )
    if total_size > len(view):
        raise XprTruncatedError(f"total_size = {total_size:#x} pero hay {len(view):#x} bytes")
    if not XPR_HEADER_SIZE + 4 <= header_size <= total_size:
        raise XprFormatError(
            f"header_size = {header_size:#x} fuera de [0x10, total_size = {total_size:#x}]"
        )
    blob = view[:total_size]
    return XprFile(total_size, header_size, _parse_resources(blob, header_size), blob)
