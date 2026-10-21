"""Xbox ADPCM (WAVE_FORMAT 0x0069): la variante de IMA ADPCM del XDK, a PCM16.

Disposición contrastada con vgmstream (`coding/ima_decoder.c`, `decode_xbox_ima`):

- Bloques de 36 bytes por canal (36 en mono, 72 en estéreo) con 64 muestras por canal.
- Al principio del bloque, una cabecera de 4 bytes por canal: s16le muestra inicial,
  u8 índice de paso (0..88) y u8 reservado.
- Detrás, los nibbles en grupos de 4 bytes (8 muestras) por canal, alternando canales
  (c0, c1, c0, c1...), el nibble bajo primero. En mono los grupos quedan contiguos.
- La muestra de la cabecera es la primera del bloque y se decodifican 63 nibbles: el
  último se descarta (a diferencia de MS-IMA, que daría 65 muestras).

Cada nibble se expande como el IMA estándar: `paso>>3` más `paso`, `paso>>1` y `paso>>2`
según los bits 2, 1 y 0, negado por el bit 3; el resultado se satura a 16 bits y el
índice se ajusta con la tabla y se limita a 0..88.
"""

from __future__ import annotations

import struct
from array import array
from typing import Final

from ngb_tools.audio.errors import AudioFormatError
from ngb_tools.audio.pcm import interleave

FORMAT_TAG: Final = 0x0069
BLOCK_BYTES_PER_CHANNEL: Final = 36
SAMPLES_PER_BLOCK: Final = 64
_HEADER_BYTES: Final = 4
_GROUP_BYTES: Final = 4
_NIBBLES: Final = SAMPLES_PER_BLOCK - 1
_INT16_MAX: Final = 32767
_INT16_MIN: Final = -32768
_MAX_INDEX: Final = 88

STEP_TABLE: Final = (
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60,
    66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371,
    408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707,
    1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484,
    7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385,
    24623, 27086, 29794, 32767,
)  # fmt: skip
INDEX_TABLE: Final = (-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8)


def block_align(channels: int) -> int:
    return BLOCK_BYTES_PER_CHANNEL * channels


def samples_for(size: int, channels: int) -> int:
    """Muestras por canal de `size` bytes (solo cuentan los bloques completos)."""
    return size // block_align(channels) * SAMPLES_PER_BLOCK


def _nibble_offsets(channels: int, channel: int) -> tuple[tuple[int, int], ...]:
    """(byte dentro del bloque, desplazamiento) de cada uno de los 63 nibbles del canal."""
    base = _HEADER_BYTES * channels + _GROUP_BYTES * channel
    group_stride = _GROUP_BYTES * channels
    return tuple(
        (base + group_stride * (j // 8) + (j % 8) // 2, 4 * (j & 1)) for j in range(_NIBBLES)
    )


def decode_channel(data: bytes, channels: int, channel: int, blocks: int) -> array[int]:
    """Las `blocks` × 64 muestras de `channel` en un flujo de `channels` canales."""
    out = array("h", bytes(2 * SAMPLES_PER_BLOCK * blocks))
    offsets = _nibble_offsets(channels, channel)
    stride = block_align(channels)
    steps, adjust = STEP_TABLE, INDEX_TABLE
    k = 0
    for block in range(blocks):
        pos = block * stride
        sample, index = struct.unpack_from("<hb", data, pos + _HEADER_BYTES * channel)
        index = max(0, min(_MAX_INDEX, index))  # con signo y limitado, como vgmstream
        out[k] = sample
        k += 1
        for byte_pos, shift in offsets:
            code = (data[pos + byte_pos] >> shift) & 0x0F
            step = steps[index]
            delta = step >> 3
            if code & 4:
                delta += step
            if code & 2:
                delta += step >> 1
            if code & 1:
                delta += step >> 2
            sample = sample - delta if code & 8 else sample + delta
            sample = max(_INT16_MIN, min(_INT16_MAX, sample))
            out[k] = sample
            k += 1
            index = max(0, min(_MAX_INDEX, index + adjust[code]))
    return out


def decode(data: bytes, channels: int) -> array[int]:
    """PCM16 intercalado de los bloques completos de `data` (un resto final se ignora)."""
    if not 1 <= channels <= 8:
        raise AudioFormatError(f"Xbox ADPCM con {channels} canales")
    blocks = len(data) // block_align(channels)
    decoded = [decode_channel(data, channels, ch, blocks) for ch in range(channels)]
    return interleave(decoded, blocks * SAMPLES_PER_BLOCK)
