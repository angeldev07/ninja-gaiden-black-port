"""ADX de CRI Middleware: cabecera, coeficientes del predictor y decodificación a PCM16.

Disposición (todo big endian), contrastada con vgmstream (`meta/adx.c`,
`coding/adx_decoder.c`) y FFmpeg (`libavcodec/adx.c`):

- 0x00 u16 `0x8000`; 0x02 u16 offset del copyright: el audio empieza en offset + 4 y los 6
  bytes previos son `(c)CRI`.
- 0x04 u8 codificación (3 = ADPCM estándar con coeficientes desde el corte), 0x05 u8 tamaño
  de bloque (18), 0x06 u8 bits por muestra (4), 0x07 u8 canales.
- 0x08 u32 frecuencia, 0x0C u32 muestras por canal, 0x10 u16 corte del highpass (Hz).
- 0x12 u8 versión (3, 4 o 5) y 0x13 u8 flags: 0x08 y 0x09 son los dos cifrados de CRI.
- Loops (solo metadatos): en la versión 3, en 0x14; en la 4, tras la historia inicial
  (0x18 + 4·canales, mínimo 8) y, si lo hay, un bloque `AINF`. Cada bloque de loop es
  `u16 relleno, u16 1, u32 flag, u32 muestra inicial, u32 offset inicial, u32 muestra
  final, u32 offset final`.

Cada bloque de 18 bytes es de un canal (los canales se intercalan bloque a bloque): u16
escala y 32 nibbles con signo, el alto primero. Muestra:
`n·(escala+1) + predicción`, con la predicción `coef1·h1 + coef2·h2` en punto fijo ×4096.
La biblioteca de la versión 3 desplaza cada producto por separado; la de la 4, la suma
(vgmstream lo distingue así; la diferencia es de ±1 LSB). Todo se satura a 16 bits.
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


# Nibble alto y bajo de cada byte, ya con signo: evita desplazamientos en el bucle caliente.
_NIBBLE_HI: Final = tuple(_signed_nibble(b >> 4) for b in range(256))
_NIBBLE_LO: Final = tuple(_signed_nibble(b & 0x0F) for b in range(256))


@dataclass(frozen=True, slots=True)
class AdxLoop:
    """Loop declarado en la cabecera (muestras por canal y offsets absolutos en el ADX)."""

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
        """Bloques por canal que hacen falta para `total_samples`."""
        return -(-self.total_samples // SAMPLES_PER_FRAME)

    @property
    def data_size(self) -> int:
        """Bytes de audio que cubren `total_samples` (sin el bloque de fin de flujo)."""
        return self.frames * self.channels * self.frame_size


def header_size(prefix: bytes) -> int:
    """Bytes de cabecera (hasta el primer bloque de audio) según los 4 primeros bytes."""
    if len(prefix) < 4:
        raise AudioTruncatedError("cabecera ADX truncada")
    magic, offset = struct.unpack_from(">HH", prefix)
    if magic != MAGIC:
        raise AudioFormatError(f"magia ADX inválida {magic:#06x}")
    return int(offset) + 4


def _loop_at(raw: bytes, pos: int) -> AdxLoop | None:
    if pos + _LOOP.size > len(raw):
        return None
    _pad, _one, flag, start, start_off, end, end_off = _LOOP.unpack_from(raw, pos)
    return AdxLoop(start, start_off, end, end_off) if flag else None


def parse_loop(raw: bytes, version: int, channels: int, data_offset: int) -> AdxLoop | None:
    """Loop de las versiones 3 y 4 si la cabecera tiene sitio para él (como vgmstream)."""
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
    """Cabecera completa (`raw` empieza en el ADX y llega al menos al primer bloque).

    Solo valida la estructura; si el flujo se puede decodificar lo dice `check_supported`.
    """
    data_offset = header_size(raw)
    if data_offset < MIN_HEADER + len(COPYRIGHT) or len(raw) < data_offset:
        raise AudioTruncatedError(f"cabecera ADX de {data_offset} bytes truncada o imposible")
    fields: tuple[int, ...] = _BASE.unpack_from(raw)
    _, _, encoding, frame_size, bits, channels, rate, total, cutoff, version, flags = fields
    if raw[data_offset - len(COPYRIGHT) : data_offset] != COPYRIGHT:
        raise AudioFormatError("falta la firma (c)CRI antes del audio")
    if not 1 <= channels <= MAX_CHANNELS or rate == 0:
        raise AudioFormatError(f"canales {channels} o frecuencia {rate} imposibles")
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
    """Lanza `EncryptedAdxError` si el flujo va cifrado y `UnsupportedCodecError` si no es la
    codificación estándar de 4 bits en bloques de 18 bytes con una versión conocida.
    """
    if header.encrypted:
        raise EncryptedAdxError(f"ADX cifrado (flags {header.flags:#04x})")
    if (header.encoding, header.frame_size, header.bits) != (ENCODING_STANDARD, FRAME_SIZE, 4):
        raise UnsupportedCodecError(
            f"ADX no estándar: codificación {header.encoding}, bloque {header.frame_size}, "
            f"{header.bits} bits"
        )
    if header.version not in VERSIONS:
        raise UnsupportedCodecError(f"versión de cabecera ADX {header.version} desconocida")


def compute_coefficients(cutoff: int, sample_rate: int) -> tuple[int, int]:
    """Coeficientes del predictor en punto fijo ×4096 desde el corte del highpass.

    `a = √2 − cos(2π·corte/frecuencia)`, `b = √2 − 1`, `c = (a − √((a+b)(a−b)))/b`,
    `coef1 = 2c` y `coef2 = −c²`. Se trunca hacia cero, como el `(short)` de vgmstream.
    """
    if sample_rate <= 0 or not 0 <= cutoff * 2 < sample_rate:
        raise AudioFormatError(f"corte {cutoff} Hz imposible a {sample_rate} Hz")
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
    """Decodifica `frames` bloques de un canal: el primero en `start`, cada `stride` bytes.

    `split_shift` reproduce la biblioteca de la versión 3 (cada producto se desplaza por
    separado); si no, se desplaza la suma, como en la 4.
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
    """Cabecera y PCM16 intercalado de un ADX completo en memoria."""
    header = parse_header(raw)
    return header, decode_pcm(raw, header)


def decode_pcm(raw: bytes, header: AdxHeader) -> array[int]:
    """PCM16 intercalado del ADX `raw`, cuya cabecera ya se parseó en `header`."""
    check_supported(header)
    end = header.data_offset + header.data_size
    if len(raw) < end:
        raise AudioTruncatedError(f"ADX truncado: {len(raw)} bytes de {end} necesarios")
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
    """`80 00` y `(c)CRI` justo antes del audio: basta para elegir el decodificador."""
    if len(prefix) < 4 or prefix[:2] != b"\x80\x00":
        return False
    data_offset = header_size(prefix)
    return prefix[data_offset - len(COPYRIGHT) : data_offset] == COPYRIGHT
