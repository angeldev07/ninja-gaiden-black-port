"""Decoding of uncompressed formats to RGBA8: packed (ARGB, 565...) and P8.

A packed format stores each pixel in an 8-, 16- or 32-bit little-endian word
with the channels in bit fields (see `formats.PackedLayout`). Fields narrower than 8
bits are widened by replicating their bits (5 bits `abcde` -> `abcdeabc`), which maps the
minimum to 0 and the maximum to exactly 255; those wider than 8 bits (L16) keep their high byte.

P8 stores an 8-bit index per pixel into a palette of up to 256 D3DCOLORs. A D3DCOLOR is
a little-endian ARGB u32, so in memory each entry reads B, G, R, A.

These functions expect the data already in linear order: un-swizzling comes first (`swizzle`).

**Memory and speed.** Nothing stores one object per pixel; the peak is O(n bytes):

- 8 bits (L8, A8, AL8 and P8): four 256-byte tables, one per channel, and
  `bytes.translate` over the whole image. Each channel is interleaved with `out[k::4] = ...`.
- 16 bits: local memo of the words already seen, at most 65,536 entries.
- 32 bits: all formats in the table have byte-aligned 8-bit channels, so
  each channel is a slice `data[byte::4]`. If one were not, it is decoded
  pixel by pixel without memoizing (at 32 bits, the memo could reach 16.7 M entries).
"""

from __future__ import annotations

import struct
from collections.abc import Sequence
from types import MappingProxyType
from typing import Final

from ngb_tools.xpr.formats import Channel, Codec, Field, PackedLayout, TextureFormat

PALETTE_ENTRY_BYTES: Final = 4
_MAX_PALETTE: Final = 256
_WORD_CODES: Final = MappingProxyType({1: "B", 2: "H", 4: "I"})
_TRANSPARENT: Final = bytes(4)
# Above 16 bits per pixel nothing is memoized: there could be up to 2**32 distinct words.
MEMO_MAX_BITS: Final = 16


def expand_bits(value: int, bits: int) -> int:
    """Widen a `bits`-bit field to 8 bits."""
    if bits >= 8:
        return value >> (bits - 8)
    widened, filled = 0, 0
    while filled < 8:
        widened = (widened << bits) | value
        filled += bits
    return widened >> (filled - 8)


def _channel(word: int, channel: Channel) -> int:
    if isinstance(channel, Field):
        return expand_bits((word >> channel.shift) & ((1 << channel.bits) - 1), channel.bits)
    return channel


def _channels(layout: PackedLayout) -> tuple[Channel, Channel, Channel, Channel]:
    return layout.red, layout.green, layout.blue, layout.alpha


def _texel(word: int, layout: PackedLayout) -> bytes:
    return bytes(_channel(word, channel) for channel in _channels(layout))


def _compact(data: bytes, row_bytes: int, height: int, stride: int) -> bytes:
    """The rows without the pitch padding, back to back."""
    if stride == row_bytes:
        return bytes(data[: row_bytes * height])
    return b"".join(data[y * stride : y * stride + row_bytes] for y in range(height))


def _interleave(planes: Sequence[bytes | int], count: int) -> bytes:
    """RGBA8 from four planes of `count` bytes (or one constant per channel)."""
    out = bytearray(4 * count)
    for k, plane in enumerate(planes):
        out[k::4] = plane if isinstance(plane, bytes) else bytes((plane,)) * count
    return bytes(out)


def _translate_tables(texels: Sequence[bytes]) -> list[bytes]:
    """From 256 RGBA texels to four `bytes.translate` tables, one per channel."""
    return [bytes(texel[k] for texel in texels) for k in range(4)]


def _decode_8bit(packed: bytes, layout: PackedLayout) -> bytes:
    tables = _translate_tables([_texel(word, layout) for word in range(256)])
    return _interleave([packed.translate(table) for table in tables], len(packed))


def _decode_memo(
    packed: bytes, width: int, height: int, element: int, layout: PackedLayout
) -> bytes:
    """Word by word with a local memo (16 bits only: at most 65,536 entries)."""
    row_format = struct.Struct(f"<{width}{_WORD_CODES[element]}")
    memo: dict[int, bytes] = {}
    rows: list[bytes] = []
    for y in range(height):
        texels = []
        for word in row_format.unpack_from(packed, y * row_format.size):
            texel = memo.get(word)
            if texel is None:
                texel = memo[word] = _texel(word, layout)
            texels.append(texel)
        rows.append(b"".join(texels))
    return b"".join(rows)


def _byte_aligned(layout: PackedLayout) -> bool:
    return all(
        not isinstance(c, Field) or (c.bits == 8 and c.shift % 8 == 0) for c in _channels(layout)
    )


def _decode_32bit(packed: bytes, width: int, height: int, layout: PackedLayout) -> bytes:
    count = width * height
    if _byte_aligned(layout):
        planes: list[bytes | int] = [
            packed[c.shift // 8 :: 4] if isinstance(c, Field) else c for c in _channels(layout)
        ]
        return _interleave(planes, count)
    words = struct.unpack(f"<{count}I", packed)
    return b"".join(_texel(word, layout) for word in words)


def decode_packed(
    data: bytes, width: int, height: int, fmt: TextureFormat, *, pitch: int | None = None
) -> bytes:
    """Packed image in linear order to RGBA8; `pitch` is the bytes per row."""
    if fmt.codec is not Codec.PACKED or fmt.layout is None:
        raise ValueError(f"{fmt.name} is not a packed format")
    element = fmt.bytes_per_element
    row_bytes = width * element
    stride = row_bytes if pitch is None else pitch
    needed = stride * (height - 1) + row_bytes if height else 0
    if stride < row_bytes or len(data) < needed:
        raise ValueError(f"{len(data)} bytes; {fmt.name} {width}x{height} needs {needed}")
    packed = _compact(data, row_bytes, height, stride)
    if element == 1:
        return _decode_8bit(packed, fmt.layout)
    if fmt.bits_per_pixel <= MEMO_MAX_BITS:
        return _decode_memo(packed, width, height, element, fmt.layout)
    return _decode_32bit(packed, width, height, fmt.layout)


def palette_rgba(palette: bytes) -> Sequence[bytes]:
    """The palette's D3DCOLOR entries as RGBA8; up to 256, padding with transparent."""
    entries = min(len(palette) // PALETTE_ENTRY_BYTES, _MAX_PALETTE)
    colors = [
        bytes((palette[i + 2], palette[i + 1], palette[i], palette[i + 3]))
        for i in range(0, entries * PALETTE_ENTRY_BYTES, PALETTE_ENTRY_BYTES)
    ]
    return colors + [_TRANSPARENT] * (_MAX_PALETTE - entries)


def decode_palette(indices: bytes, palette: bytes) -> bytes:
    """P8 indices (in linear order) to RGBA8; an index with no entry gives transparent black."""
    tables = _translate_tables(palette_rgba(palette))
    return _interleave([bytes(indices).translate(table) for table in tables], len(indices))
