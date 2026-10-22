"""XACT wave banks (`WBND`), versions 2 and 3 (XACT 1.x, original Xbox).

Layout cross-checked against vgmstream (`meta/xwb.c`); everything little endian:

- 0x00 `WBND`, 0x04 u32 version, 0x08 four `(offset, size)` segments: bank data, entry
  metadata, names and wave data.
- Bank data: u32 flags (0x00020000 = compact format, unsupported), u32 entries, 16-byte
  name, u32 size of each metadata, u32 size of each name (0x40), u32 alignment.
- Each metadata: u32 flags, u32 format, u32 offset (relative to the wave data), u32 size,
  u32 loop start and u32 loop length (in bytes).
- Format (version <= 3): bits 0-1 codec (0 PCM, 1 Xbox ADPCM, 2 WMA), 2-4 channels, 5-30
  sample rate and 31 bits per sample (1 = 16, 0 = 8).
"""

from __future__ import annotations

import struct
from array import array
from dataclasses import dataclass
from typing import Final

from ngb_tools.audio import xbox_adpcm
from ngb_tools.audio.errors import AudioFormatError, AudioTruncatedError, UnsupportedCodecError
from ngb_tools.audio.pcm import from_le_bytes, from_u8

MAGIC: Final = b"WBND"
VERSIONS: Final = frozenset({2, 3})
COMPACT_FLAG: Final = 0x0002_0000
NAME_SIZE: Final = 16
CODEC_PCM: Final = 0
CODEC_XBOX_ADPCM: Final = 1
CODEC_WMA: Final = 2
CODEC_NAMES: Final = {CODEC_PCM: "pcm", CODEC_XBOX_ADPCM: "xbox_adpcm", CODEC_WMA: "wma"}
_SEGMENTS: Final = struct.Struct("<4sI8I")
_BANK: Final = struct.Struct("<II16sIII")
_ENTRY: Final = struct.Struct("<6I")


@dataclass(frozen=True, slots=True)
class XwbEntry:
    """Bank entry; `offset` is absolute inside the `.xwb`."""

    index: int
    flags: int
    codec: int
    channels: int
    sample_rate: int
    bits: int
    offset: int
    size: int
    loop_start: int
    loop_length: int
    name: str | None

    @property
    def codec_name(self) -> str:
        return CODEC_NAMES.get(self.codec, f"codec_{self.codec}")


@dataclass(frozen=True, slots=True)
class XwbBank:
    version: int
    flags: int
    name: str
    entries: tuple[XwbEntry, ...]


def _segment(data: bytes, offset: int, size: int, what: str) -> None:
    if offset + size > len(data):
        raise AudioTruncatedError(f"segment {what} ({offset:#x}+{size:#x}) outside the file")


def _decode_format(fmt: int) -> tuple[int, int, int, int]:
    """(codec, channels, sample rate, bits) of the format dword for version <= 3."""
    return fmt & 0x3, (fmt >> 2) & 0x7, (fmt >> 5) & 0x3FF_FFFF, 16 if fmt >> 31 else 8


def parse_xwb(data: bytes) -> XwbBank:
    if len(data) < _SEGMENTS.size:
        raise AudioTruncatedError("truncated XWB header")
    magic, version, *segments = _SEGMENTS.unpack_from(data)
    if magic != MAGIC:
        raise AudioFormatError(f"invalid XWB magic {magic!r}")
    if version not in VERSIONS:
        raise UnsupportedCodecError(f"XWB version {version} not supported (only 2 and 3)")
    bank_off, bank_size, meta_off, meta_size, names_off, names_size, wave_off, wave_size = segments
    for off, size, what in (
        (bank_off, bank_size, "bank"),
        (meta_off, meta_size, "entries"),
        (names_off, names_size, "names"),
        (wave_off, wave_size, "wavedata"),
    ):
        _segment(data, off, size, what)
    if bank_size < _BANK.size:
        raise AudioFormatError(f"bank data of {bank_size} bytes")
    flags, count, raw_name, elem_size, name_size, _align = _BANK.unpack_from(data, bank_off)
    if flags & COMPACT_FLAG:
        raise UnsupportedCodecError("compact-format XWB not supported")
    if elem_size < _ENTRY.size or count * elem_size > meta_size:
        raise AudioFormatError(f"{count} entries of {elem_size} bytes do not fit in {meta_size}")
    entries = []
    for index in range(count):
        entry_flags, fmt, offset, size, loop_start, loop_length = _ENTRY.unpack_from(
            data, meta_off + index * elem_size
        )
        codec, channels, rate, bits = _decode_format(fmt)
        if offset + size > wave_size:
            raise AudioTruncatedError(f"entry {index} past the wave data")
        name = None
        if names_size and name_size and (index + 1) * name_size <= names_size:
            at = names_off + index * name_size
            name = data[at : at + name_size].split(b"\x00", 1)[0].decode("ascii", "replace")
        entries.append(
            XwbEntry(
                index=index,
                flags=entry_flags,
                codec=codec,
                channels=channels,
                sample_rate=rate,
                bits=bits,
                offset=wave_off + offset,
                size=size,
                loop_start=loop_start,
                loop_length=loop_length,
                name=name or None,
            )
        )
    bank_name = raw_name.split(b"\x00", 1)[0].decode("ascii", "replace")
    return XwbBank(version=version, flags=flags, name=bank_name, entries=tuple(entries))


def decode_entry(data: bytes, entry: XwbEntry) -> array[int]:
    """Interleaved PCM16 of the entry (8/16-bit PCM or Xbox ADPCM)."""
    if not 1 <= entry.channels <= 8 or entry.sample_rate == 0:
        raise AudioFormatError(f"entry {entry.index}: {entry.channels} channels impossible")
    payload = data[entry.offset : entry.offset + entry.size]
    if entry.codec == CODEC_XBOX_ADPCM:
        return xbox_adpcm.decode(payload, entry.channels)
    if entry.codec != CODEC_PCM:
        raise UnsupportedCodecError(f"entry {entry.index}: codec {entry.codec_name}")
    samples = from_le_bytes(payload) if entry.bits == 16 else from_u8(payload)
    usable = len(samples) - len(samples) % entry.channels
    return samples[:usable]
