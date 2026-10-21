"""Qué hay dentro de una entrada: magias públicas, chunks de Team Ninja y entropía.

Los AFS no guardan el tipo de su contenido, así que se deduce de los primeros bytes. El
orden de los detectores importa: primero las magias públicas exactas (XPR0, DDS...),
después las firmas con validación (RIFF, ADX, zlib), luego el texto y, al final, las
etiquetas ASCII de los formatos propios de Team Ninja (`KMD\\0`, `chr_dat\\0`...).
Lo que no encaja se agrupa por sus 4 primeros bytes (`hex:...`) para seguir estudiándolo.
"""

from __future__ import annotations

import math
import re
from collections import Counter
from collections.abc import Callable
from dataclasses import dataclass
from enum import StrEnum
from types import MappingProxyType
from typing import Final

ENTROPY_WINDOW: Final = 64 * 1024  # bytes iniciales que se analizan de cada entrada
LOW_ENTROPY_BELOW: Final = 5.0
HIGH_ENTROPY_ABOVE: Final = 7.5
TEAM_NINJA_PREFIX: Final = "tag:"
UNKNOWN_PREFIX: Final = "hex:"


@dataclass(frozen=True, slots=True)
class Magic:
    """Firma pública: prefijo exacto → tipo, extensión sugerida y descripción."""

    prefix: bytes
    kind: str
    ext: str
    description: str


@dataclass(frozen=True, slots=True)
class Classification:
    """Resultado de clasificar una cabecera."""

    kind: str
    ext: str


KNOWN_MAGICS: Final = (
    Magic(b"AFS\x00", "AFS", "afs", "contenedor AFS (anidado)"),
    Magic(b"AFS\x20", "AFS", "afs", "contenedor AFS variante 0x20 (anidado)"),
    Magic(b"XPR0", "XPR0", "xpr", "Xbox Packed Resource (texturas/recursos D3D8)"),
    Magic(b"XPR1", "XPR1", "xpr", "Xbox Packed Resource v1"),
    Magic(b"XPR2", "XPR2", "xpr", "Xbox Packed Resource v2"),
    Magic(b"DDS ", "DDS", "dds", "DirectDraw Surface"),
    Magic(b"WBND", "XWB", "xwb", "XACT wave bank"),
    Magic(b"SDBK", "XSB", "xsb", "XACT sound bank"),
    Magic(b"\x1f\x8b", "gzip", "gz", "gzip"),
    Magic(b"PK\x03\x04", "ZIP", "zip", "ZIP"),
    Magic(b"\x89PNG", "PNG", "png", "PNG"),
    Magic(b"\xff\xd8\xff", "JPEG", "jpg", "JPEG"),
    Magic(b"OggS", "Ogg", "ogg", "Ogg"),
    Magic(b"XBEH", "XBE", "xbe", "ejecutable Xbox"),
    Magic(b"\x00\x00\x01\xba", "MPEG-PS", "mpg", "MPEG program stream (Sofdec)"),
    Magic(b"@UTF", "CRI-UTF", "utf", "tabla @UTF de CRI"),
    Magic(b"CPK ", "CRI-CPK", "cpk", "contenedor CPK de CRI"),
    Magic(b"AIXF", "CRI-AIX", "aix", "audio AIX de CRI"),
    Magic(b"CRILAYLA", "CRILAYLA", "bin", "compresión CRILAYLA"),
    Magic(b"Yaz0", "Yaz0", "szs", "compresión Yaz0"),
    Magic(b"\x30\x26\xb2\x75\x8e\x66\xcf\x11", "ASF", "wma", "ASF/WMA"),
    Magic(b"<?xml", "XML", "xml", "XML"),
    Magic(b"\xef\xbb\xbf", "text-utf8", "txt", "texto UTF-8 con BOM"),
    Magic(b"\xff\xfe", "text-utf16", "txt", "texto UTF-16LE con BOM"),
)

# Si dos magias comparten tipo (las dos de AFS), manda la primera: la más general.
KIND_DESCRIPTIONS: Final = MappingProxyType(
    {
        **{m.kind: m.description for m in reversed(KNOWN_MAGICS)},
        "RIFF/WAVE": "RIFF WAVE (PCM/Xbox ADPCM)",
        "RIFF": "RIFF genérico",
        "ADX": "audio ADX de CRI",
        "zlib": "flujo zlib",
        "text": "texto ASCII",
        "empty": "entrada vacía",
    }
)

EMPTY: Final = Classification("empty", "bin")
_TEXT_BYTES: Final = frozenset(range(0x20, 0x7F)) | {0x09, 0x0A, 0x0D}
_TAG_RE: Final = re.compile(rb"[A-Za-z0-9_\-. ]{3,12}")


def is_adx(data: bytes) -> bool:
    """ADX: `80 00` + u16be offset; `(c)CRI` acaba justo donde empieza el audio.

    Dos bytes de firma dan muchos falsos positivos; el copyright en su sitio los descarta.
    """
    if len(data) < 16 or data[:2] != b"\x80\x00":
        return False
    pos = int.from_bytes(data[2:4], "big") - 2
    return pos >= 4 and data[pos : pos + 6] == b"(c)CRI"


def is_zlib(data: bytes) -> bool:
    """Cabecera zlib: CMF 0x78, un FLG típico y (CMF*256 + FLG) múltiplo de 31."""
    return (
        len(data) >= 2
        and data[0] == 0x78
        and data[1] in (0x01, 0x5E, 0x9C, 0xDA)
        and ((data[0] << 8) | data[1]) % 31 == 0
    )


def looks_like_text(data: bytes) -> bool:
    """Al menos un 95 % de bytes imprimibles entre los 256 primeros."""
    sample = data[:256]
    return len(sample) >= 4 and sum(b in _TEXT_BYTES for b in sample) >= 0.95 * len(sample)


def team_ninja_tag(data: bytes) -> str | None:
    """Etiqueta ASCII con la que abren los formatos propios de Team Ninja.

    Ejemplos reales: `KMD\\0` (modelos), `chr_dat\\0` (.mtn), `itm_dat2` (.sob/.itm),
    `stry_dat` (.sty). Valen 4 o más imprimibles, o 3 seguidos de NUL (como `KMD\\0`).
    """
    match = _TAG_RE.match(data[:12])
    if match is None:
        return None
    raw = match.group(0)
    if len(raw) >= 4 or (len(raw) == 3 and data[3:4] == b"\x00"):
        return raw.decode("ascii").rstrip() or None
    return None


def _by_known_magic(data: bytes) -> Classification | None:
    for magic in KNOWN_MAGICS:
        if data.startswith(magic.prefix):
            return Classification(magic.kind, magic.ext)
    return None


def _by_riff(data: bytes) -> Classification | None:
    if not data.startswith(b"RIFF"):
        return None
    if data[8:12] == b"WAVE":
        return Classification("RIFF/WAVE", "wav")
    return Classification("RIFF", "riff")


def _by_adx(data: bytes) -> Classification | None:
    return Classification("ADX", "adx") if is_adx(data) else None


def _by_zlib(data: bytes) -> Classification | None:
    return Classification("zlib", "zlib") if is_zlib(data) else None


def _by_text(data: bytes) -> Classification | None:
    return Classification("text", "txt") if looks_like_text(data) else None


def _by_team_ninja_tag(data: bytes) -> Classification | None:
    tag = team_ninja_tag(data)
    return None if tag is None else Classification(f"{TEAM_NINJA_PREFIX}{tag}", "bin")


_DETECTORS: Final[tuple[Callable[[bytes], Classification | None], ...]] = (
    _by_known_magic,
    _by_riff,
    _by_adx,
    _by_zlib,
    _by_text,
    _by_team_ninja_tag,
)


def classify(data: bytes) -> Classification:
    """Clasifica por cabecera; lo desconocido se agrupa por sus 4 primeros bytes."""
    if not data:
        return EMPTY
    for detect in _DETECTORS:
        found = detect(data)
        if found is not None:
            return found
    return Classification(f"{UNKNOWN_PREFIX}{data[:4].hex()}", "bin")


def describe_kind(kind: str) -> str:
    """Descripción legible de un tipo devuelto por `classify`."""
    if kind in KIND_DESCRIPTIONS:
        return KIND_DESCRIPTIONS[kind]
    if kind.startswith(TEAM_NINJA_PREFIX):
        return "chunk de Team Ninja (etiqueta ASCII)"
    return "desconocido"


def shannon_entropy(data: bytes) -> float:
    """Entropía de Shannon en bits/byte: 0 = constante, 8 = uniforme (comprimido o cifrado)."""
    if not data:
        return 0.0
    total = len(data)
    h = -sum((c / total) * math.log2(c / total) for c in Counter(data).values())
    return h if h > 0 else 0.0  # evita el -0.0 de una entrada constante


class EntropyBand(StrEnum):
    """Banda de entropía: low < 5 <= mid <= 7.5 < high; empty si no hay datos."""

    LOW = "low"
    MID = "mid"
    HIGH = "high"
    EMPTY = "empty"


def entropy_band(entropy: float, size: int) -> EntropyBand:
    if size == 0:
        return EntropyBand.EMPTY
    if entropy < LOW_ENTROPY_BELOW:
        return EntropyBand.LOW
    return EntropyBand.MID if entropy <= HIGH_ENTROPY_ABOVE else EntropyBand.HIGH


def head_ascii(data: bytes) -> str:
    """Vista ASCII de una cabecera: lo no imprimible se muestra como punto."""
    return "".join(chr(b) if 0x20 <= b < 0x7F else "." for b in data)
