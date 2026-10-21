"""Bancos de efectos `.wva` de Ninja Gaiden Black (formato propio de Team Ninja).

Disposición INFERRED, contrastada sobre los 326 `.wva` del disco (2966 registros, todos
coherentes con lo que sigue; todo little endian):

- `u32 N` y N punteros `u32`: los 31 bits bajos son el offset del registro dentro del
  `.wva` (0 = hueco vacío) y el bit 31 indica que el registro trae el bloque de parámetros
  largo. El primer registro empieza justo tras la tabla (4 + 4·N).
- Cada registro: `u32 (flags << 24 | tamaño de datos)`; un WAVEFORMATEX de 20 bytes (tag
  0x0069 Xbox ADPCM, canales, frecuencia, bytes/s, alineación 36·canales, 4 bits,
  `cbSize` 2 y 64 muestras por bloque); un bloque de parámetros de 24 bytes con el bit 31
  (floats, quizá volumen y distancias 3D) u 8 sin él; y después los datos.
- Los flags vistos son 0x00, 0x01 y 0x04 (sentido sin confirmar; quizá loop).
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
    """Registro `slot` del banco; `data_offset` es relativo al inicio del `.wva`."""

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
        """Muestras por canal de los bloques completos."""
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
        raise AudioTruncatedError(f"registro {slot} en {offset:#x} truncado")
    word = int.from_bytes(data[offset : offset + 4], "little")
    tag, channels, rate, _avg, align, bits, _cb, spb = _WAVEFORMAT.unpack_from(data, fmt_at)
    size = word & SIZE_MASK
    if data_at + size > len(data):
        raise AudioTruncatedError(f"registro {slot}: {size} bytes de datos más allá del final")
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
    """Registros no vacíos del banco, en el orden de la tabla de punteros."""
    if len(data) < 4:
        raise AudioTruncatedError("banco .wva truncado")
    count = int.from_bytes(data[:4], "little")
    table_end = 4 + 4 * count
    if count == 0 or count > MAX_RECORDS or table_end > len(data):
        raise AudioFormatError(f"tabla de {count} registros imposible en {len(data)} bytes")
    pointers = struct.unpack_from(f"<{count}I", data, 4)
    records = []
    for slot, pointer in enumerate(pointers):
        offset = pointer & OFFSET_MASK
        if offset == 0:
            continue
        if offset < table_end:
            raise AudioFormatError(f"registro {slot} en {offset:#x}, dentro de la tabla")
        records.append(_parse_record(data, slot, pointer))
    return tuple(records)


def check_xbox_adpcm(record: WvaRecord) -> None:
    """Lanza `UnsupportedCodecError` si el registro no es el Xbox ADPCM conocido."""
    expected = (FORMAT_TAG, 4, block_align(record.channels), SAMPLES_PER_BLOCK)
    found = (record.format_tag, record.bits, record.block_align, record.samples_per_block)
    if found != expected or not 1 <= record.channels <= 2:
        raise UnsupportedCodecError(
            f"registro {record.slot}: formato {record.format_tag:#06x}, {record.channels} "
            f"canales, alineación {record.block_align}, {record.samples_per_block} muestras"
        )


def record_data(data: bytes, record: WvaRecord) -> bytes:
    return data[record.data_offset : record.data_offset + record.data_size]
