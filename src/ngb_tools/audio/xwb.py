"""Wave banks de XACT (`WBND`) de las versiones 2 y 3 (XACT 1.x, Xbox original).

Disposición contrastada con vgmstream (`meta/xwb.c`); todo little endian:

- 0x00 `WBND`, 0x04 u32 versión, 0x08 cuatro segmentos `(offset, tamaño)`: datos del
  banco, metadatos de las entradas, nombres y datos de onda.
- Datos del banco: u32 flags (0x00020000 = formato compacto, no soportado), u32 entradas,
  nombre de 16 bytes, u32 tamaño de cada metadato, u32 tamaño de cada nombre (0x40),
  u32 alineación.
- Cada metadato: u32 flags, u32 formato, u32 offset (relativo a los datos de onda),
  u32 tamaño, u32 inicio y u32 longitud del loop (en bytes).
- Formato (versión ≤ 3): bits 0-1 códec (0 PCM, 1 Xbox ADPCM, 2 WMA), 2-4 canales,
  5-30 frecuencia y 31 bits por muestra (1 = 16, 0 = 8).
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
    """Entrada del banco; `offset` es absoluto dentro del `.xwb`."""

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
        raise AudioTruncatedError(f"segmento {what} ({offset:#x}+{size:#x}) fuera del archivo")


def _decode_format(fmt: int) -> tuple[int, int, int, int]:
    """(códec, canales, frecuencia, bits) del dword de formato de la versión ≤ 3."""
    return fmt & 0x3, (fmt >> 2) & 0x7, (fmt >> 5) & 0x3FF_FFFF, 16 if fmt >> 31 else 8


def parse_xwb(data: bytes) -> XwbBank:
    if len(data) < _SEGMENTS.size:
        raise AudioTruncatedError("cabecera XWB truncada")
    magic, version, *segments = _SEGMENTS.unpack_from(data)
    if magic != MAGIC:
        raise AudioFormatError(f"magia XWB inválida {magic!r}")
    if version not in VERSIONS:
        raise UnsupportedCodecError(f"versión XWB {version} no soportada (solo 2 y 3)")
    bank_off, bank_size, meta_off, meta_size, names_off, names_size, wave_off, wave_size = segments
    for off, size, what in (
        (bank_off, bank_size, "bank"),
        (meta_off, meta_size, "entries"),
        (names_off, names_size, "names"),
        (wave_off, wave_size, "wavedata"),
    ):
        _segment(data, off, size, what)
    if bank_size < _BANK.size:
        raise AudioFormatError(f"datos del banco de {bank_size} bytes")
    flags, count, raw_name, elem_size, name_size, _align = _BANK.unpack_from(data, bank_off)
    if flags & COMPACT_FLAG:
        raise UnsupportedCodecError("XWB en formato compacto no soportado")
    if elem_size < _ENTRY.size or count * elem_size > meta_size:
        raise AudioFormatError(f"{count} entradas de {elem_size} bytes no caben en {meta_size}")
    entries = []
    for index in range(count):
        entry_flags, fmt, offset, size, loop_start, loop_length = _ENTRY.unpack_from(
            data, meta_off + index * elem_size
        )
        codec, channels, rate, bits = _decode_format(fmt)
        if offset + size > wave_size:
            raise AudioTruncatedError(f"entrada {index} más allá de los datos de onda")
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
    """PCM16 intercalado de la entrada (PCM de 8/16 bits o Xbox ADPCM)."""
    if not 1 <= entry.channels <= 8 or entry.sample_rate == 0:
        raise AudioFormatError(f"entrada {entry.index}: {entry.channels} canales imposibles")
    payload = data[entry.offset : entry.offset + entry.size]
    if entry.codec == CODEC_XBOX_ADPCM:
        return xbox_adpcm.decode(payload, entry.channels)
    if entry.codec != CODEC_PCM:
        raise UnsupportedCodecError(f"entrada {entry.index}: códec {entry.codec_name}")
    samples = from_le_bytes(payload) if entry.bits == 16 else from_u8(payload)
    usable = len(samples) - len(samples) % entry.channels
    return samples[:usable]
