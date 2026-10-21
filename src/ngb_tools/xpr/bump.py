"""Texturas de relieve con signo, V8U8 y L6V5U5 (normal maps y mapas DuDv), a RGBA8.

En la NV2A no hay formatos "con signo": V8U8 es el color `SZ_G8B8` (0x28) y L6V5U5 es
`SZ_R6G5B5` (0x27). Lo que los hace con signo es el estado de la etapa de textura, que el
XPR0 no guarda; el D3D de Xbox les da el nombre de relieve (`X_D3DFMT_V8U8` = 0x28 con alias
G8B8, `X_D3DFMT_L6V5U5` = 0x27 con alias R6G5B5) y así se exportan. En el juego hay 326 V8U8
(KMD de escenario: agua y superficies con relieve) y 32 L6V5U5 (`kotei.kmd`...), todas
swizzled. Disposición de la palabra little endian de 16 bits:

    V8U8     bits 0-7  U (con signo, complemento a 2)   bits 8-15 V (con signo)
    L6V5U5   bits 0-4  U (con signo)   bits 5-9 V (con signo)   bits 10-15 L (sin signo)

Evidencia: Cxbx-Reloaded (`XbConvert.cpp`) describe L6V5U5 como "byte[0]=vvvUuuuu,
byte[1]=LlllllVv (U and V, but not L, are stored in two's complement)" y lee V8U8 (G8B8)
con B = byte 0 y G = byte 1. xemu (`pgraph/texture.c`, `pgraph_convert_texture_data`)
convierte `SZ_R6G5B5` invirtiendo el bit de signo de los campos de 5 bits
(`rgb655 ^= (1 << 9) | (1 << 4)`) y deja los bits 10-15 sin signo.

**Mapeo a PNG** (con u, v en [-1, 1): u = U/128 en 8 bits y U/16 en 5 bits):

- R = U en binario desplazado: `U + 128` en 8 bits (el byte con el bit 7 invertido) y, en
  5 bits, `U + 16` ampliado a 8 replicando bits (como `__L6V5U5ToARGBRow_C` de Cxbx). Es
  `(u * 0.5 + 0.5) * 255` salvo redondeo: -128 -> 0, 0 -> 128, 127 -> 255; -16 -> 0, 0 -> 132,
  15 -> 255.
- G = lo mismo con V.
- B = z de la normal reconstruida, `z = sqrt(max(0, 1 - u² - v²))`, como `(z + 1) * 127.5`
  redondeado hacia arriba en el .5: plano (u = v = 0) -> 255; u o v en el extremo -> 128.
- A = 255 en V8U8; en L6V5U5, L ampliada de 6 a 8 bits replicando bits (es luminancia, no
  transparencia: el manifiesto lo marca).

El resultado tiene el aspecto de un normal map: azul violeta donde la superficie es plana.
"""

from __future__ import annotations

import math
import struct
from functools import cache
from typing import Final

from ngb_tools.xpr.formats import Codec, TextureFormat
from ngb_tools.xpr.pixels import expand_bits

WORD_BYTES: Final = 2
_WORDS: Final = 1 << 16
_OPAQUE: Final = 255


def signed(value: int, bits: int) -> int:
    """El campo de `bits` bits leído en complemento a 2."""
    return value - (1 << bits) if value >> (bits - 1) else value


def offset_binary(value: int, bits: int) -> int:
    """Un valor con signo de `bits` bits pasado a 0-255: binario desplazado y ampliado."""
    return expand_bits(value + (1 << (bits - 1)), bits)


def normal_z(u: float, v: float) -> int:
    """Componente z de la normal (u, v, z) codificada en 128-255."""
    z = math.sqrt(max(0.0, 1.0 - u * u - v * v))
    return min(255, math.floor((z + 1.0) * 127.5 + 0.5))


def v8u8_texel(word: int) -> bytes:
    u, v = signed(word & 0xFF, 8), signed(word >> 8, 8)
    return bytes((offset_binary(u, 8), offset_binary(v, 8), normal_z(u / 128, v / 128), _OPAQUE))


def l6v5u5_texel(word: int) -> bytes:
    u, v = signed(word & 0x1F, 5), signed((word >> 5) & 0x1F, 5)
    luminance = expand_bits(word >> 10, 6)
    return bytes((offset_binary(u, 5), offset_binary(v, 5), normal_z(u / 16, v / 16), luminance))


@cache
def _table(codec: Codec) -> tuple[bytes, ...]:
    """Texel RGBA de cada una de las 65 536 palabras (se calcula una vez por formato)."""
    texel = v8u8_texel if codec is Codec.V8U8 else l6v5u5_texel
    return tuple(texel(word) for word in range(_WORDS))


def decode_bump(
    data: bytes, width: int, height: int, fmt: TextureFormat, *, pitch: int | None = None
) -> bytes:
    """Imagen V8U8 o L6V5U5 en orden lineal a RGBA8; `pitch` son los bytes por fila."""
    if fmt.codec not in (Codec.V8U8, Codec.L6V5U5):
        raise ValueError(f"{fmt.name} no es un formato de relieve con signo")
    row_bytes = width * WORD_BYTES
    stride = row_bytes if pitch is None else pitch
    needed = stride * (height - 1) + row_bytes if height else 0
    if stride < row_bytes or len(data) < needed:
        raise ValueError(f"{len(data)} bytes; {fmt.name} {width}x{height} necesita {needed}")
    table = _table(fmt.codec)
    row = struct.Struct(f"<{width}H")
    return b"".join(
        table[word] for y in range(height) for word in row.unpack_from(data, y * stride)
    )
