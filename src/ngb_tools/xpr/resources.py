"""XPR0 (Xbox Packed Resource) parsing: header, typed resources and data region.

An XPR0 packs Xbox Direct3D objects ready for the GPU. Everything is little-endian u32:

    0x00  "XPR0"
    0x04  total_size   bytes of the whole XPR0
    0x08  header_size  where the GPU data starts (relative to the start of the XPR0)
    0x0C  resource headers, one after another, up to a u32 0xFFFFFFFF

Each resource header is the D3D structure the game uses as-is in memory. It starts
with `Common` (bits 0-15: reference count; bits 16-18: type) and `Data`, the offset of
the resource bytes **relative to `header_size`** (verified: in `ryu.kmd` the first
index is at `header_size + 0` and the first texture at a `header_size + Data` aligned to
0x1000). The sizes of each type come from the Cxbx-Reloaded structures:

    type  resource       bytes  fields
    0     vertex buffer  12     Common, Data, Lock
    1     index buffer   12     Common, Data, Lock
    2     push buffer    24     Common, Data, Lock, Size, AllocationSize, ?
    3     palette        12     Common, Data, Lock   (bits 30-31: 256/128/64/32 entries)
    4     texture        20     Common, Data, Lock, Format, Size

Cxbx's push buffer is 20 bytes, but in `ryu.kmd` and `r_ryu.kmd` each one takes 24:
a u32 that is always 0 follows (INFERRED: another XDK version or a fixup field).

Team Ninja also interleaves its own blocks `u32 0x80000000, u32 size, payload`
(USERDATA; observed, undocumented), with the model's materials and meshes.

`Format` (texture) packs: bits 0-1 DMA channel, 2 cubemap, 3 border color, 4-7
dimensions (2 or 3), 8-15 D3DFORMAT, 16-19 mip levels, 20-23/24-27/28-31 log2 of
width, height and depth. `Size` is only used in linear (non-power-of-two) textures:
bits 0-11 width-1, 12-23 height-1 and 24-31 pitch/64-1.

Sources:
- xboxdevwiki, XPR format: https://xboxdevwiki.net/XPR
- Cxbx-Reloaded, `X_D3DResource`, `X_D3DPixelContainer`, `X_D3DCOMMON_*` masks,
  `X_D3DFORMAT_*` and `X_D3DSIZE_*`:
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
# Cap on resource headers before the 0xFFFFFFFF. The maximum across the game's 1413 XPR0s
# (standalone and inside KMDs) is 767. 16384 leaves ample margin and bounds memory and
# time against a hostile or unterminated header list.
MAX_RESOURCES: Final = 16384

_U32: Final = struct.Struct("<I")
_BUFFER: Final = struct.Struct("<3I")
_PIXEL_CONTAINER: Final = struct.Struct("<5I")
_PUSH_BUFFER: Final = struct.Struct("<6I")


class ResourceType(IntEnum):
    """Bits 16-18 of `Common` (`X_D3DCOMMON_TYPE_*`)."""

    VERTEX_BUFFER = 0
    INDEX_BUFFER = 1
    PUSH_BUFFER = 2
    PALETTE = 3
    TEXTURE = 4
    SURFACE = 5
    FIXUP = 6


@dataclass(frozen=True, slots=True)
class FormatWord:
    """`Format` field of a texture, decoded by bits."""

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
        """Xbox D3DFORMAT (see `formats`)."""
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
    """`Size` field: dimensions and pitch of a linear texture; 0 in the others."""

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
    """`D3DTexture`. `index` is the position in the header list and `offset` that of the
    header inside the XPR0; `data` is relative to `header_size`."""

    index: int
    offset: int
    common: int
    data: int
    lock: int
    format: FormatWord
    size: SizeWord


@dataclass(frozen=True, slots=True)
class Palette:
    """`D3DPalette`: D3DCOLOR entries (ARGB u32) starting at `data`."""

    index: int
    offset: int
    common: int
    data: int
    lock: int

    @property
    def entries(self) -> int:
        """`X_D3DPALETTE_COMMON_PALETTESIZE`: 0 = 256, 1 = 128, 2 = 64, 3 = 32 entries."""
        return 256 >> ((self.common >> 30) & 0x3)

    @property
    def byte_size(self) -> int:
        return 4 * self.entries


@dataclass(frozen=True, slots=True)
class VertexBuffer:
    """`D3DVertexBuffer`: vertices at `data`; their format is described by the KMD USERDATA."""

    index: int
    offset: int
    common: int
    data: int
    lock: int


@dataclass(frozen=True, slots=True)
class IndexBuffer:
    """`D3DIndexBuffer`: u16 indices at `data` (in ryu.kmd, at the start of the data)."""

    index: int
    offset: int
    common: int
    data: int
    lock: int


@dataclass(frozen=True, slots=True)
class PushBuffer:
    """`D3DPushBuffer`: prerecorded GPU commands (`size` bytes at `data`).

    `trailing` is the sixth u32 observed in the game (always 0); Cxbx does not document it.
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
    """Team Ninja's own block inside the header list."""

    index: int
    offset: int
    payload: bytes


type Resource = Texture | Palette | VertexBuffer | IndexBuffer | PushBuffer | UserData


class PaletteSource(StrEnum):
    """How the palette was associated with a P8 texture (see `XprFile.palette_link`)."""

    NEXT = "next"  # the header following the texture (what the whole game does)
    ONLY = "only"  # fallback: the only palette in the file


@dataclass(frozen=True, slots=True)
class PaletteLink:
    palette: Palette
    source: PaletteSource


@dataclass(frozen=True, slots=True)
class XprFile:
    """A parsed XPR0.

    `blob` is a view (no copy) of its `total_size` bytes over whatever was passed to
    `parse_xpr`. `textures` and `palettes` are indexed once, at construction.
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
        object.__setattr__(self, "textures", textures)  # frozen dataclass
        object.__setattr__(self, "palettes", palettes)

    @property
    def data_size(self) -> int:
        return self.total_size - self.header_size

    def require_data(self, offset: int, length: int) -> None:
        """Check, without copying anything, that `[offset, offset + length)` fits in the data."""
        if offset < 0 or length < 0 or offset + length > self.data_size:
            raise XprTruncatedError(
                f"data [{offset:#x}, {offset + length:#x}) outside the {self.data_size:#x} "
                "bytes of the data region"
            )

    def data_bytes(self, offset: int, length: int) -> bytes:
        """`length` bytes of the data region starting at `offset` (a `Data` field)."""
        self.require_data(offset, length)
        start = self.header_size + offset
        return bytes(self.blob[start : start + length])

    def palette_link(self, texture: Texture) -> PaletteLink | None:
        """Palette of a P8 texture and how it was deduced.

        An XPR0 does not link texture and palette: the game calls SetPalette at run time. In the
        game's 1413 XPR0s, the 2959 P8 textures are followed by their palette (in the
        headers and in the data), so the next header is used if it is a palette
        (`NEXT`). As a fallback, the only palette in the file (`ONLY`, never seen in the
        game); otherwise it is unknown (`None`). O(1) cost: the palettes are already indexed.
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
    """The resource header at `pos` and the position of the next one."""
    (common,) = _U32.unpack_from(blob, pos)
    if common == USERDATA_MARKER:
        if pos + 8 > limit:
            raise XprFormatError(f"userdata at {pos:#x} has no size before header_size")
        (size,) = _U32.unpack_from(blob, pos + 4)
        end = pos + 8 + size
        if end > limit:
            raise XprFormatError(f"userdata at {pos:#x} of {size:#x} bytes runs past header_size")
        return UserData(index, pos, bytes(blob[pos + 8 : end])), end
    kind = (common >> 16) & 0x7
    layout = _LAYOUTS.get(kind, _BUFFER)
    if pos + layout.size > limit:
        raise XprFormatError(f"resource header at {pos:#x} cut off by header_size")
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
                f"unsupported resource type {kind} at {pos:#x} (Common = {common:#010x})"
            )


def _parse_resources(blob: memoryview, header_size: int) -> tuple[Resource, ...]:
    resources: list[Resource] = []
    pos = XPR_HEADER_SIZE
    while pos + 4 <= header_size:
        if _U32.unpack_from(blob, pos)[0] == END_MARKER:
            return tuple(resources)
        if len(resources) == MAX_RESOURCES:
            raise XprFormatError(
                f"more than {MAX_RESOURCES} resource headers without the end marker"
            )
        resource, pos = _parse_resource(blob, len(resources), pos, header_size)
        resources.append(resource)
    raise XprFormatError(f"no 0xFFFFFFFF end marker before header_size = {header_size:#x}")


def parse_xpr(data: Buffer) -> XprFile:
    """Parse the XPR0 that starts at `data[0]`; anything left after `total_size` is ignored.

    No copy: the `XprFile` keeps a view of `data` (bytes, or a view of a KMD).
    """
    view = memoryview(data).cast("B")
    magic = bytes(view[: len(XPR_MAGIC)])  # only the 4 magic bytes are copied
    if len(view) < XPR_HEADER_SIZE or magic != XPR_MAGIC:
        raise XprFormatError(f"invalid magic: {magic!r} (expected {XPR_MAGIC!r})")
    total_size, header_size = struct.unpack_from("<II", view, 4)
    if total_size < XPR_HEADER_SIZE + 4:
        raise XprFormatError(
            f"total_size = {total_size:#x}: the header and the end marker do not fit"
        )
    if total_size > len(view):
        raise XprTruncatedError(f"total_size = {total_size:#x} but there are {len(view):#x} bytes")
    if not XPR_HEADER_SIZE + 4 <= header_size <= total_size:
        raise XprFormatError(
            f"header_size = {header_size:#x} outside [0x10, total_size = {total_size:#x}]"
        )
    blob = view[:total_size]
    return XprFile(total_size, header_size, _parse_resources(blob, header_size), blob)
