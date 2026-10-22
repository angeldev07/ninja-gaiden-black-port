"""CRI Middleware ADX: header, predictor coefficients and decoding to PCM16.

Layout (everything big endian), cross-checked against vgmstream (`meta/adx.c`,
`coding/adx_decoder.c`) and FFmpeg (`libavcodec/adx.c`):

- 0x00 u16 `0x8000`; 0x02 u16 copyright offset: the audio starts at offset + 4 and the 6
  bytes before it are `(c)CRI`.
- 0x04 u8 encoding (3 = standard ADPCM with coefficients from the cutoff), 0x05 u8 block
  size (18), 0x06 u8 bits per sample (4), 0x07 u8 channels.
- 0x08 u32 sample rate, 0x0C u32 samples per channel, 0x10 u16 highpass cutoff (Hz).
- 0x12 u8 version (3, 4 or 5) and 0x13 u8 flags: 0x08 and 0x09 are the two CRI encryptions.
- Loops (metadata only): in version 3, at 0x14; in 4, after the initial history
  (0x18 + 4*channels, minimum 8) and, if present, an `AINF` block. Each loop block is
  `u16 padding, u16 1, u32 flag, u32 start sample, u32 start offset, u32 end sample,
  u32 end offset`.

Each 18-byte block belongs to one channel (the channels are interleaved block by block):
u16 scale and 32 signed nibbles, high first. Sample: `n*(scale+1) + prediction`, with the
prediction `coef1*h1 + coef2*h2` in x4096 fixed point. The version 3 library shifts each
product separately; version 4's shifts the sum (vgmstream tells them apart this way; the
difference is +-1 LSB). Everything is clamped to 16 bits.
"""

from __future__ import annotations

import math
import struct
from array import array
from dataclasses import dataclass
from typing import Final

from ngb_tools.audio.errors import (
    AudioFormatError,
    AudioTruncatedError,
    EncryptedAdxError,
    UnsupportedCodecError,
)
from ngb_tools.audio.pcm import interleave

MAGIC: Final = 0x8000
COPYRIGHT: Final = b"(c)CRI"
FRAME_SIZE: Final = 18
SAMPLES_PER_FRAME: Final = 32
ENCODING_STANDARD: Final = 3
ENCRYPTION_FLAGS: Final = frozenset({0x08, 0x09})
VERSIONS: Final = frozenset({3, 4, 5})
FIXED_POINT_BITS: Final = 12
MIN_HEADER: Final = 0x14
MAX_CHANNELS: Final = 8
_BASE: Final = struct.Struct(">HHBBBBIIHBB")
_LOOP: Final = struct.Struct(">HHIIIII")
_INT16_MAX: Final = 32767
_INT16_MIN: Final = -32768


def _signed_nibble(value: int) -> int:
    return value - 16 if value >= 8 else value


# High and low nibble of each byte, already signed: avoids shifts in the hot loop.
_NIBBLE_HI: Final = tuple(_signed_nibble(b >> 4) for b in range(256))
_NIBBLE_LO: Final = tuple(_signed_nibble(b & 0x0F) for b in range(256))


@dataclass(frozen=True, slots=True)
class AdxLoop:
    """Loop declared in the header (samples per channel and absolute offsets in the ADX)."""

    start_sample: int
    start_offset: int
    end_sample: int
    end_offset: int


@dataclass(frozen=True, slots=True)
class AdxHeader:
    data_offset: int
    encoding: int
    frame_size: int
    bits: int
    channels: int
    sample_rate: int
    total_samples: int
    cutoff: int
    version: int
    flags: int
    loop: AdxLoop | None

    @property
    def encrypted(self) -> bool:
        return self.flags in ENCRYPTION_FLAGS

    @property
    def frames(self) -> int:
        """Blocks per channel needed for `total_samples`."""
        return -(-self.total_samples // SAMPLES_PER_FRAME)

    @property
    def data_size(self) -> int:
        """Audio bytes covering `total_samples` (without the end-of-stream block)."""
        return self.frames * self.channels * self.frame_size


def header_size(prefix: bytes) -> int:
    """Header bytes (up to the first audio block) according to the first 4 bytes."""
    if len(prefix) < 4:
        raise AudioTruncatedError("truncated ADX header")
    magic, offset = struct.unpack_from(">HH", prefix)
    if magic != MAGIC:
        raise AudioFormatError(f"invalid ADX magic {magic:#06x}")
    return int(offset) + 4


def _loop_at(raw: bytes, pos: int) -> AdxLoop | None:
    if pos + _LOOP.size > len(raw):
        return None
    _pad, _one, flag, start, start_off, end, end_off = _LOOP.unpack_from(raw, pos)
    return AdxLoop(start, start_off, end, end_off) if flag else None


def parse_loop(raw: bytes, version: int, channels: int, data_offset: int) -> AdxLoop | None:
    """Loop for versions 3 and 4 if the header has room for it (like vgmstream)."""
    room = data_offset - len(COPYRIGHT)
    if version == 3:
        return _loop_at(raw, MIN_HEADER) if room >= MIN_HEADER + _LOOP.size else None
    if version == 4:
        hist_end = 0x18 + (4 * channels if channels > 1 else 8)
        ainf = hist_end + 4
        ainf_size = 0
        if raw[ainf : ainf + 4] == b"AINF":
            ainf_size = int.from_bytes(raw[ainf + 4 : ainf + 8], "big")
        if room - ainf_size >= hist_end + _LOOP.size:
            return _loop_at(raw, hist_end)
    return None


def parse_header(raw: bytes) -> AdxHeader:
    """Full header (`raw` starts at the ADX and reaches at least the first block).

    It only validates the structure; whether the stream can be decoded is up to `check_supported`.
    """
    data_offset = header_size(raw)
    if data_offset < MIN_HEADER + len(COPYRIGHT) or len(raw) < data_offset:
        raise AudioTruncatedError(f"ADX header of {data_offset} bytes truncated or impossible")
    fields: tuple[int, ...] = _BASE.unpack_from(raw)
    _, _, encoding, frame_size, bits, channels, rate, total, cutoff, version, flags = fields
    if raw[data_offset - len(COPYRIGHT) : data_offset] != COPYRIGHT:
        raise AudioFormatError("missing (c)CRI signature before the audio")
    if not 1 <= channels <= MAX_CHANNELS or rate == 0:
        raise AudioFormatError(f"channels {channels} or sample rate {rate} impossible")
    return AdxHeader(
        data_offset=data_offset,
        encoding=encoding,
        frame_size=frame_size,
        bits=bits,
        channels=channels,
        sample_rate=rate,
        total_samples=total,
        cutoff=cutoff,
        version=version,
        flags=flags,
        loop=parse_loop(raw, version, channels, data_offset),
    )


def check_supported(header: AdxHeader) -> None:
    """Raise `EncryptedAdxError` if the stream is encrypted and `UnsupportedCodecError` if it
    is not the standard 4-bit encoding in 18-byte blocks with a known version.
    """
    if header.encrypted:
        raise EncryptedAdxError(f"encrypted ADX (flags {header.flags:#04x})")
    if (header.encoding, header.frame_size, header.bits) != (ENCODING_STANDARD, FRAME_SIZE, 4):
        raise UnsupportedCodecError(
            f"non-standard ADX: encoding {header.encoding}, block {header.frame_size}, "
            f"{header.bits} bits"
        )
    if header.version not in VERSIONS:
        raise UnsupportedCodecError(f"unknown ADX header version {header.version}")


def compute_coefficients(cutoff: int, sample_rate: int) -> tuple[int, int]:
    """Predictor coefficients in x4096 fixed point from the highpass cutoff.

    `a = √2 − cos(2π·cutoff/sample_rate)`, `b = √2 − 1`, `c = (a − √((a+b)(a−b)))/b`,
    `coef1 = 2c` and `coef2 = −c²`. Truncated toward zero, like vgmstream's `(short)`.
    """
    if sample_rate <= 0 or not 0 <= cutoff * 2 < sample_rate:
        raise AudioFormatError(f"cutoff {cutoff} Hz impossible at {sample_rate} Hz")
    a = math.sqrt(2.0) - math.cos(2.0 * math.pi * cutoff / sample_rate)
    b = math.sqrt(2.0) - 1.0
    c = (a - math.sqrt((a + b) * (a - b))) / b
    scale = 1 << FIXED_POINT_BITS
    return int(c * 2.0 * scale), int(-(c * c) * scale)


def decode_channel(
    data: bytes,
    start: int,
    stride: int,
    frames: int,
    coefs: tuple[int, int],
    *,
    split_shift: bool,
) -> array[int]:
    """Decode `frames` blocks of one channel: the first at `start`, one every `stride` bytes.

    `split_shift` reproduces the version 3 library (each product is shifted separately);
    otherwise the sum is shifted, as in version 4.
    """
    out = array("h", bytes(2 * SAMPLES_PER_FRAME * frames))
    c1, c2 = coefs
    hi, lo = _NIBBLE_HI, _NIBBLE_LO
    h1 = h2 = 0
    k = 0
    pos = start
    for _ in range(frames):
        scale = ((data[pos] << 8) | data[pos + 1]) + 1
        for byte in data[pos + 2 : pos + FRAME_SIZE]:
            for nibble in (hi[byte], lo[byte]):
                if split_shift:
                    s = nibble * scale + (c1 * h1 >> 12) + (c2 * h2 >> 12)
                else:
                    s = nibble * scale + ((c1 * h1 + c2 * h2) >> 12)
                s = max(_INT16_MIN, min(_INT16_MAX, s))
                out[k] = s
                k += 1
                h2 = h1
                h1 = s
        pos += stride
    return out


def decode(raw: bytes) -> tuple[AdxHeader, array[int]]:
    """Header and interleaved PCM16 of a complete in-memory ADX."""
    header = parse_header(raw)
    return header, decode_pcm(raw, header)


def decode_pcm(raw: bytes, header: AdxHeader) -> array[int]:
    """Interleaved PCM16 of the ADX `raw`, whose header was already parsed into `header`."""
    check_supported(header)
    end = header.data_offset + header.data_size
    if len(raw) < end:
        raise AudioTruncatedError(f"truncated ADX: {len(raw)} bytes of {end} needed")
    coefs = compute_coefficients(header.cutoff, header.sample_rate)
    stride = header.channels * FRAME_SIZE
    decoded = [
        decode_channel(
            raw,
            header.data_offset + ch * FRAME_SIZE,
            stride,
            header.frames,
            coefs,
            split_shift=header.version == 3,
        )
        for ch in range(header.channels)
    ]
    return interleave(decoded, header.total_samples)


def looks_like_adx(prefix: bytes) -> bool:
    """`80 00` and `(c)CRI` right before the audio: enough to pick the decoder."""
    if len(prefix) < 4 or prefix[:2] != b"\x80\x00":
        return False
    data_offset = header_size(prefix)
    return prefix[data_offset - len(COPYRIGHT) : data_offset] == COPYRIGHT
