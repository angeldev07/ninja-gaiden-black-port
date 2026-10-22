"""Xbox ADPCM (WAVE_FORMAT 0x0069): the XDK's IMA ADPCM variant, to PCM16.

Layout cross-checked against vgmstream (`coding/ima_decoder.c`, `decode_xbox_ima`):

- Blocks of 36 bytes per channel (36 mono, 72 stereo) with 64 samples per channel.
- At the start of the block, a 4-byte header per channel: s16le initial sample, u8 step
  index (0..88) and u8 reserved.
- After it, the nibbles in groups of 4 bytes (8 samples) per channel, alternating channels
  (c0, c1, c0, c1...), low nibble first. In mono the groups are contiguous.
- The header sample is the first of the block and 63 nibbles are decoded: the last one is
  discarded (unlike MS-IMA, which would give 65 samples).

Each nibble expands like standard IMA: `step>>3` plus `step`, `step>>1` and `step>>2`
depending on bits 2, 1 and 0, negated by bit 3; the result is clamped to 16 bits and the
index is adjusted with the table and limited to 0..88.
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
    """Samples per channel in `size` bytes (only complete blocks count)."""
    return size // block_align(channels) * SAMPLES_PER_BLOCK


def _nibble_offsets(channels: int, channel: int) -> tuple[tuple[int, int], ...]:
    """(byte within the block, shift) of each of the channel's 63 nibbles."""
    base = _HEADER_BYTES * channels + _GROUP_BYTES * channel
    group_stride = _GROUP_BYTES * channels
    return tuple(
        (base + group_stride * (j // 8) + (j % 8) // 2, 4 * (j & 1)) for j in range(_NIBBLES)
    )


def decode_channel(data: bytes, channels: int, channel: int, blocks: int) -> array[int]:
    """The `blocks` x 64 samples of `channel` in a stream of `channels` channels."""
    out = array("h", bytes(2 * SAMPLES_PER_BLOCK * blocks))
    offsets = _nibble_offsets(channels, channel)
    stride = block_align(channels)
    steps, adjust = STEP_TABLE, INDEX_TABLE
    k = 0
    for block in range(blocks):
        pos = block * stride
        sample, index = struct.unpack_from("<hb", data, pos + _HEADER_BYTES * channel)
        index = max(0, min(_MAX_INDEX, index))  # signed and clamped, like vgmstream
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
    """Interleaved PCM16 of the complete blocks of `data` (a trailing remainder is ignored)."""
    if not 1 <= channels <= 8:
        raise AudioFormatError(f"Xbox ADPCM with {channels} channels")
    blocks = len(data) // block_align(channels)
    decoded = [decode_channel(data, channels, ch, blocks) for ch in range(channels)]
    return interleave(decoded, blocks * SAMPLES_PER_BLOCK)
