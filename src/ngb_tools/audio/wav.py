"""Escritor de WAV PCM16 (RIFF/WAVE con `fmt ` y `data`), solo con la stdlib.

Se generan los bytes a mano en vez de usar `wave` para escribirlos con
`ngb_tools._paths.atomic_write_chunks` (`.part` y renombrado); `wave` los relee igual.
"""

from __future__ import annotations

import struct
from array import array
from collections.abc import Iterator
from typing import Final

from ngb_tools.audio.pcm import to_le_bytes

PCM_TAG: Final = 1
BITS: Final = 16
_FMT: Final = struct.Struct("<4sI4s4sIHHIIHH4sI")
_MAX_RIFF: Final = 0xFFFFFFFF


def wav_header(channels: int, sample_rate: int, frames: int) -> bytes:
    """Cabecera de 44 bytes para `frames` muestras por canal de PCM16."""
    block = channels * BITS // 8
    data_size = frames * block
    if data_size + _FMT.size - 8 > _MAX_RIFF:
        raise ValueError(f"{data_size} bytes de audio no caben en un RIFF")
    return _FMT.pack(
        b"RIFF",
        data_size + _FMT.size - 8,
        b"WAVE",
        b"fmt ",
        16,
        PCM_TAG,
        channels,
        sample_rate,
        sample_rate * block,
        block,
        BITS,
        b"data",
        data_size,
    )


def iter_wav_chunks(samples: array[int], channels: int, sample_rate: int) -> Iterator[bytes]:
    """Cabecera y datos de un WAV con `samples` (PCM16 intercalado)."""
    if channels <= 0 or len(samples) % channels:
        raise ValueError(f"{len(samples)} muestras no se reparten en {channels} canales")
    yield wav_header(channels, sample_rate, len(samples) // channels)
    yield to_le_bytes(samples)


def wav_bytes(samples: array[int], channels: int, sample_rate: int) -> bytes:
    return b"".join(iter_wav_chunks(samples, channels, sample_rate))
