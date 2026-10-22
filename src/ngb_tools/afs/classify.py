"""What is inside an entry: public magics, Team Ninja chunks and entropy.

AFS files do not store the type of their content, so it is deduced from the first bytes.
The order of the detectors matters: first the exact public magics (XPR0, DDS...), then the
signatures with validation (RIFF, ADX, zlib), then text and, last, the ASCII tags of
Team Ninja's own formats (`KMD\\0`, `chr_dat\\0`...). Whatever does not fit is grouped by its
first 4 bytes (`hex:...`) so it can be studied further.
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

ENTROPY_WINDOW: Final = 64 * 1024  # initial bytes analyzed for each entry
LOW_ENTROPY_BELOW: Final = 5.0
HIGH_ENTROPY_ABOVE: Final = 7.5
TEAM_NINJA_PREFIX: Final = "tag:"
UNKNOWN_PREFIX: Final = "hex:"


@dataclass(frozen=True, slots=True)
class Magic:
    """Public signature: exact prefix -> type, suggested extension and description."""

    prefix: bytes
    kind: str
    ext: str
    description: str


@dataclass(frozen=True, slots=True)
class Classification:
    """Result of classifying a header."""

    kind: str
    ext: str


KNOWN_MAGICS: Final = (
    Magic(b"AFS\x00", "AFS", "afs", "AFS container (nested)"),
    Magic(b"AFS\x20", "AFS", "afs", "AFS container, 0x20 variant (nested)"),
    Magic(b"XPR0", "XPR0", "xpr", "Xbox Packed Resource (D3D8 textures/resources)"),
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
    Magic(b"XBEH", "XBE", "xbe", "Xbox executable"),
    Magic(b"\x00\x00\x01\xba", "MPEG-PS", "mpg", "MPEG program stream (Sofdec)"),
    Magic(b"@UTF", "CRI-UTF", "utf", "CRI @UTF table"),
    Magic(b"CPK ", "CRI-CPK", "cpk", "CRI CPK container"),
    Magic(b"AIXF", "CRI-AIX", "aix", "CRI AIX audio"),
    Magic(b"CRILAYLA", "CRILAYLA", "bin", "CRILAYLA compression"),
    Magic(b"Yaz0", "Yaz0", "szs", "Yaz0 compression"),
    Magic(b"\x30\x26\xb2\x75\x8e\x66\xcf\x11", "ASF", "wma", "ASF/WMA"),
    Magic(b"<?xml", "XML", "xml", "XML"),
    Magic(b"\xef\xbb\xbf", "text-utf8", "txt", "UTF-8 text with BOM"),
    Magic(b"\xff\xfe", "text-utf16", "txt", "UTF-16LE text with BOM"),
)

# If two magics share a type (both AFS ones), the first wins: the more general one.
KIND_DESCRIPTIONS: Final = MappingProxyType(
    {
        **{m.kind: m.description for m in reversed(KNOWN_MAGICS)},
        "RIFF/WAVE": "RIFF WAVE (PCM/Xbox ADPCM)",
        "RIFF": "generic RIFF",
        "ADX": "CRI ADX audio",
        "zlib": "zlib stream",
        "text": "ASCII text",
        "empty": "empty entry",
    }
)

EMPTY: Final = Classification("empty", "bin")
_TEXT_BYTES: Final = frozenset(range(0x20, 0x7F)) | {0x09, 0x0A, 0x0D}
_TAG_RE: Final = re.compile(rb"[A-Za-z0-9_\-. ]{3,12}")


def is_adx(data: bytes) -> bool:
    """ADX: `80 00` + u16be offset; `(c)CRI` ends right where the audio starts.

    Two signature bytes give many false positives; the copyright in its place rules them out.
    """
    if len(data) < 16 or data[:2] != b"\x80\x00":
        return False
    pos = int.from_bytes(data[2:4], "big") - 2
    return pos >= 4 and data[pos : pos + 6] == b"(c)CRI"


def is_zlib(data: bytes) -> bool:
    """zlib header: CMF 0x78, a typical FLG and (CMF*256 + FLG) a multiple of 31."""
    return (
        len(data) >= 2
        and data[0] == 0x78
        and data[1] in (0x01, 0x5E, 0x9C, 0xDA)
        and ((data[0] << 8) | data[1]) % 31 == 0
    )


def looks_like_text(data: bytes) -> bool:
    """At least 95% printable bytes among the first 256."""
    sample = data[:256]
    return len(sample) >= 4 and sum(b in _TEXT_BYTES for b in sample) >= 0.95 * len(sample)


def team_ninja_tag(data: bytes) -> str | None:
    """ASCII tag that Team Ninja's own formats open with.

    Real examples: `KMD\\0` (models), `chr_dat\\0` (.mtn), `itm_dat2` (.sob/.itm),
    `stry_dat` (.sty). 4 or more printable characters qualify, or 3 followed by NUL (like
    `KMD\\0`).
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
    """Classify by header; unknown data is grouped by its first 4 bytes."""
    if not data:
        return EMPTY
    for detect in _DETECTORS:
        found = detect(data)
        if found is not None:
            return found
    return Classification(f"{UNKNOWN_PREFIX}{data[:4].hex()}", "bin")


def describe_kind(kind: str) -> str:
    """Human-readable description of a type returned by `classify`."""
    if kind in KIND_DESCRIPTIONS:
        return KIND_DESCRIPTIONS[kind]
    if kind.startswith(TEAM_NINJA_PREFIX):
        return "Team Ninja chunk (ASCII tag)"
    return "unknown"


def shannon_entropy(data: bytes) -> float:
    """Shannon entropy in bits/byte: 0 = constant, 8 = uniform (compressed or encrypted)."""
    if not data:
        return 0.0
    total = len(data)
    h = -sum((c / total) * math.log2(c / total) for c in Counter(data).values())
    return h if h > 0 else 0.0  # avoids the -0.0 of a constant entry


class EntropyBand(StrEnum):
    """Entropy band: low < 5 <= mid <= 7.5 < high; empty if there is no data."""

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
    """ASCII view of a header: non-printable bytes are shown as a dot."""
    return "".join(chr(b) if 0x20 <= b < 0x7F else "." for b in data)
