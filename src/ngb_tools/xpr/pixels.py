"""Decodificación de formatos sin comprimir a RGBA8: empaquetados (ARGB, 565...) y P8.

Un formato empaquetado guarda cada píxel en una palabra little endian de 8, 16 o 32 bits
con los canales en campos de bits (ver `formats.PackedLayout`). Los campos de menos de 8
bits se amplían replicando sus bits (5 bits `abcde` -> `abcdeabc`), que lleva el mínimo a 0
y el máximo a 255 exactos; los de más de 8 bits (L16) conservan su byte alto.

P8 guarda un índice de 8 bits por píxel en una paleta de hasta 256 D3DCOLOR. Un D3DCOLOR es
un u32 ARGB little endian, así que en memoria cada entrada se lee B, G, R, A.

Estas funciones esperan los datos ya en orden lineal: el des-swizzle va antes (`swizzle`).

**Memoria y velocidad.** Nada guarda un objeto por píxel; el pico es O(n bytes):

- 8 bits (L8, A8, AL8 y P8): cuatro tablas de 256 bytes, una por canal, y
  `bytes.translate` sobre toda la imagen. Cada canal se intercala con `out[k::4] = ...`.
- 16 bits: memoria local de las palabras ya vistas, como mucho 65 536 entradas.
- 32 bits: todos los formatos de la tabla tienen canales de 8 bits alineados a byte, así
  que cada canal es un corte `datos[byte::4]`. Si alguno no lo estuviera, se decodifica
  píxel a píxel sin memorizar (con 32 bits, la memoria podría llegar a 16,7 M entradas).
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
# Por encima de 16 bits por píxel no se memoriza: habría hasta 2**32 palabras distintas.
MEMO_MAX_BITS: Final = 16


def expand_bits(value: int, bits: int) -> int:
    """Lleva un campo de `bits` bits a 8 bits."""
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
    """Las filas sin el relleno del pitch, seguidas."""
    if stride == row_bytes:
        return bytes(data[: row_bytes * height])
    return b"".join(data[y * stride : y * stride + row_bytes] for y in range(height))


def _interleave(planes: Sequence[bytes | int], count: int) -> bytes:
    """RGBA8 a partir de cuatro planos de `count` bytes (o una constante por canal)."""
    out = bytearray(4 * count)
    for k, plane in enumerate(planes):
        out[k::4] = plane if isinstance(plane, bytes) else bytes((plane,)) * count
    return bytes(out)


def _translate_tables(texels: Sequence[bytes]) -> list[bytes]:
    """De 256 texels RGBA a cuatro tablas de `bytes.translate`, una por canal."""
    return [bytes(texel[k] for texel in texels) for k in range(4)]


def _decode_8bit(packed: bytes, layout: PackedLayout) -> bytes:
    tables = _translate_tables([_texel(word, layout) for word in range(256)])
    return _interleave([packed.translate(table) for table in tables], len(packed))


def _decode_memo(
    packed: bytes, width: int, height: int, element: int, layout: PackedLayout
) -> bytes:
    """Palabra a palabra con memoria local (solo con 16 bits: como mucho 65 536 entradas)."""
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
    """Imagen empaquetada en orden lineal a RGBA8; `pitch` son los bytes por fila."""
    if fmt.codec is not Codec.PACKED or fmt.layout is None:
        raise ValueError(f"{fmt.name} no es un formato empaquetado")
    element = fmt.bytes_per_element
    row_bytes = width * element
    stride = row_bytes if pitch is None else pitch
    needed = stride * (height - 1) + row_bytes if height else 0
    if stride < row_bytes or len(data) < needed:
        raise ValueError(f"{len(data)} bytes; {fmt.name} {width}x{height} necesita {needed}")
    packed = _compact(data, row_bytes, height, stride)
    if element == 1:
        return _decode_8bit(packed, fmt.layout)
    if fmt.bits_per_pixel <= MEMO_MAX_BITS:
        return _decode_memo(packed, width, height, element, fmt.layout)
    return _decode_32bit(packed, width, height, fmt.layout)


def palette_rgba(palette: bytes) -> Sequence[bytes]:
    """Las entradas D3DCOLOR de la paleta como RGBA8; hasta 256, rellenando con transparente."""
    entries = min(len(palette) // PALETTE_ENTRY_BYTES, _MAX_PALETTE)
    colors = [
        bytes((palette[i + 2], palette[i + 1], palette[i], palette[i + 3]))
        for i in range(0, entries * PALETTE_ENTRY_BYTES, PALETTE_ENTRY_BYTES)
    ]
    return colors + [_TRANSPARENT] * (_MAX_PALETTE - entries)


def decode_palette(indices: bytes, palette: bytes) -> bytes:
    """Índices P8 (en orden lineal) a RGBA8; un índice sin entrada da negro transparente."""
    tables = _translate_tables(palette_rgba(palette))
    return _interleave([bytes(indices).translate(table) for table in tables], len(indices))
