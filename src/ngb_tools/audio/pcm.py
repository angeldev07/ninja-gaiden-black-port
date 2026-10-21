"""Utilidades de PCM16: intercalado de canales y bytes little endian."""

from __future__ import annotations

import sys
from array import array
from collections.abc import Sequence


def interleave(channels: Sequence[array[int]], length: int) -> array[int]:
    """Une los canales (recortados a `length` muestras) en un único flujo intercalado."""
    count = len(channels)
    if count == 1:
        return channels[0][:length]
    out = array("h", bytes(2 * length * count))
    for index, samples in enumerate(channels):
        out[index::count] = samples[:length]
    return out


def to_le_bytes(samples: array[int]) -> bytes:
    """Bytes s16le de `samples` (el orden del WAV), sea cual sea el de la máquina."""
    if sys.byteorder == "little":
        return samples.tobytes()
    swapped = array("h", samples)
    swapped.byteswap()
    return swapped.tobytes()


def from_le_bytes(data: bytes) -> array[int]:
    """`array('h')` desde bytes s16le (un byte suelto al final se ignora)."""
    samples = array("h", data[: len(data) - len(data) % 2])
    if sys.byteorder != "little":
        samples.byteswap()
    return samples


def from_u8(data: bytes) -> array[int]:
    """PCM de 8 bits sin signo (el del WAV) a 16 bits con signo."""
    return array("h", ((b - 128) << 8 for b in data))
