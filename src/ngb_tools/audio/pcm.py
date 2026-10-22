"""PCM16 utilities: channel interleaving and little endian bytes."""

from __future__ import annotations

import sys
from array import array
from collections.abc import Sequence


def interleave(channels: Sequence[array[int]], length: int) -> array[int]:
    """Join the channels (trimmed to `length` samples) into a single interleaved stream."""
    count = len(channels)
    if count == 1:
        return channels[0][:length]
    out = array("h", bytes(2 * length * count))
    for index, samples in enumerate(channels):
        out[index::count] = samples[:length]
    return out


def to_le_bytes(samples: array[int]) -> bytes:
    """s16le bytes of `samples` (the WAV order), whatever the machine's order is."""
    if sys.byteorder == "little":
        return samples.tobytes()
    swapped = array("h", samples)
    swapped.byteswap()
    return swapped.tobytes()


def from_le_bytes(data: bytes) -> array[int]:
    """`array('h')` from s16le bytes (a trailing lone byte is ignored)."""
    samples = array("h", data[: len(data) - len(data) % 2])
    if sys.byteorder != "little":
        samples.byteswap()
    return samples


def from_u8(data: bytes) -> array[int]:
    """Unsigned 8-bit PCM (the WAV one) to signed 16-bit."""
    return array("h", ((b - 128) << 8 for b in data))
