"""Ninja Gaiden Black `.wva` effect banks (Team Ninja's own format).

INFERRED layout, checked against the 326 `.wva` files on the disc (2966 records, all
consistent with what follows; everything little endian):

- `u32 N` and N `u32` pointers: the low 31 bits are the record's offset inside the `.wva`
  (0 = empty slot) and bit 31 says the record carries the long parameter block. The first
  record starts right after the table (4 + 4*N).
- Each record: `u32 (flags << 24 | data size)`; a 20-byte WAVEFORMATEX (tag 0x0069 Xbox
  ADPCM, channels, sample rate, bytes/s, alignment 36*channels, 4 bits, `cbSize` 2 and 64
  samples per block); a 24-byte parameter block with bit 31 (floats, perhaps volume and 3D
  distances) or 8 bytes without it; and then the data.
- The flags seen are 0x00, 0x01 and 0x04 (meaning unconfirmed; perhaps loop).
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from typing import Final

from ngb_tools.audio.errors import AudioFormatError, AudioTruncatedError, UnsupportedCodecError
from ngb_tools.audio.xbox_adpcm import FORMAT_TAG, SAMPLES_PER_BLOCK, block_align

LONG_PARAMS_BIT: Final = 0x8000_0000
OFFSET_MASK: Final = 0x7FFF_FFFF
SIZE_MASK: Final = 0x00FF_FFFF
WAVEFORMAT_SIZE: Final = 20
PARAMS_LONG: Final = 24
PARAMS_SHORT: Final = 8
MAX_RECORDS: Final = 4096
_WAVEFORMAT: Final = struct.Struct("<HHIIHHHH")


@dataclass(frozen=True, slots=True)
class WvaRecord:
    """Record `slot` of the bank; `data_offset` is relative to the start of the `.wva`."""

    slot: int
    offset: int
    flags: int
    format_tag: int
    channels: int
    sample_rate: int
    block_align: int
    bits: int
    samples_per_block: int
    long_params: bool
    params: bytes
    data_offset: int
    data_size: int

    @property
    def samples(self) -> int:
        """Samples per channel of the complete blocks."""
        if self.block_align <= 0:
            return 0
        return self.data_size // self.block_align * self.samples_per_block


def _parse_record(data: bytes, slot: int, pointer: int) -> WvaRecord:
    offset = pointer & OFFSET_MASK
    long_params = bool(pointer & LONG_PARAMS_BIT)
    params_size = PARAMS_LONG if long_params else PARAMS_SHORT
    fmt_at = offset + 4
    params_at = fmt_at + WAVEFORMAT_SIZE
    data_at = params_at + params_size
    if data_at > len(data):
        raise AudioTruncatedError(f"record {slot} at {offset:#x} truncated")
    word = int.from_bytes(data[offset : offset + 4], "little")
    tag, channels, rate, _avg, align, bits, _cb, spb = _WAVEFORMAT.unpack_from(data, fmt_at)
    size = word & SIZE_MASK
    if data_at + size > len(data):
        raise AudioTruncatedError(f"record {slot}: {size} bytes of data past the end")
    return WvaRecord(
        slot=slot,
        offset=offset,
        flags=word >> 24,
        format_tag=tag,
        channels=channels,
        sample_rate=rate,
        block_align=align,
        bits=bits,
        samples_per_block=spb,
        long_params=long_params,
        params=data[params_at:data_at],
        data_offset=data_at,
        data_size=size,
    )


def parse_wva(data: bytes) -> tuple[WvaRecord, ...]:
    """Non-empty records of the bank, in pointer table order."""
    if len(data) < 4:
        raise AudioTruncatedError("truncated .wva bank")
    count = int.from_bytes(data[:4], "little")
    table_end = 4 + 4 * count
    if count == 0 or count > MAX_RECORDS or table_end > len(data):
        raise AudioFormatError(f"table of {count} records impossible in {len(data)} bytes")
    pointers = struct.unpack_from(f"<{count}I", data, 4)
    records = []
    for slot, pointer in enumerate(pointers):
        offset = pointer & OFFSET_MASK
        if offset == 0:
            continue
        if offset < table_end:
            raise AudioFormatError(f"record {slot} at {offset:#x}, inside the table")
        records.append(_parse_record(data, slot, pointer))
    return tuple(records)


def check_xbox_adpcm(record: WvaRecord) -> None:
    """Raise `UnsupportedCodecError` if the record is not the known Xbox ADPCM."""
    expected = (FORMAT_TAG, 4, block_align(record.channels), SAMPLES_PER_BLOCK)
    found = (record.format_tag, record.bits, record.block_align, record.samples_per_block)
    if found != expected or not 1 <= record.channels <= 2:
        raise UnsupportedCodecError(
            f"record {record.slot}: format {record.format_tag:#06x}, {record.channels} "
            f"channels, alignment {record.block_align}, {record.samples_per_block} samples"
        )


def record_data(data: bytes, record: WvaRecord) -> bytes:
    return data[record.data_offset : record.data_offset + record.data_size]
