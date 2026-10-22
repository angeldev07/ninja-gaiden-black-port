"""Loaded XBE image: sections, VA -> file map, XOR keys and digests.

When an XBE starts, the kernel copies the header to `base_address` and each
section from `raw_addr` to `virtual_addr`; whatever the section occupies in
memory above its raw data (`virtual_size > raw_size`) is BSS and is zero-filled.
`AddressMap` reproduces that translation in reverse: given a VA, which file
offset backs it (if any).

An XBE can be hostile or corrupt, so the layout is validated before it is
walked: what is impossible (section table outside the header, ranges that
overflow 32 bits or run past the image, runaway counters) is an
`ImageLayoutError`; what is suspicious but analyzable (overlapping sections,
ambiguous XOR keys, digests that do not match) are validation issues.
"""

from __future__ import annotations

import hashlib
from dataclasses import dataclass, field
from enum import StrEnum
from itertools import combinations
from pathlib import Path
from typing import Final

from ngb_tools.xbe.binary import (
    DEFAULT_C_STRING_MAX,
    decode_ascii,
    decode_utf16z,
    read_c_bytes,
)
from ngb_tools.xbe.errors import ImageLayoutError, TruncatedDataError, UnmappedAddressError
from ngb_tools.xbe.structs import SECTION_HEADER_SIZE, ImageHeader, SectionHeader

HEADERS_REGION = "<headers>"
SECTION_NAME_MAX = 64
UNKNOWN_SECTION_NAME = "?"
DEFAULT_UTF16_MAX_CHARS = 512
ADDRESS_SPACE: Final = 1 << 32  # the Xbox is 32-bit x86
# Bound on the number of sections. Real XBEs have a few dozen (NGB's default.xbe, 16;
# the dashboard update ones, 27); with more than 256 the table is not real, and each
# entry costs a SHA-1 of its data.
MAX_SECTIONS: Final = 256


class BuildKind(StrEnum):
    """Build type according to which XOR key set de-obfuscates the header."""

    RETAIL = "retail"
    DEBUG = "debug"
    BETA = "beta"
    CHIHIRO = "chihiro"  # Sega arcade board based on the Xbox
    UNKNOWN = "unknown"


@dataclass(frozen=True, slots=True)
class XorKeys:
    """Keys that obfuscate the entry point and the kernel thunk for a build type."""

    kind: BuildKind
    entry_point: int
    kernel_thunk: int


# retail/debug/beta: xboxdevwiki; chihiro: Cxbx-Reloaded.
XOR_KEYS: tuple[XorKeys, ...] = (
    XorKeys(BuildKind.RETAIL, 0xA8FC57AB, 0x5B6D40B6),
    XorKeys(BuildKind.DEBUG, 0x94859D4B, 0xEFB1F152),
    XorKeys(BuildKind.BETA, 0xE682F45B, 0x46437DCD),
    XorKeys(BuildKind.CHIHIRO, 0x40B5C16E, 0x2290059D),
)


@dataclass(frozen=True, slots=True)
class XorCandidate:
    """Result of de-obfuscating with one key set."""

    kind: BuildKind
    entry_point: int
    kernel_thunk_addr: int
    valid: bool  # entry in backed code and thunk backed by the file


@dataclass(frozen=True, slots=True)
class XorDecoding:
    """All the candidates; one is chosen only if it is the only valid one."""

    candidates: tuple[XorCandidate, ...]

    @property
    def valid_candidates(self) -> tuple[XorCandidate, ...]:
        return tuple(candidate for candidate in self.candidates if candidate.valid)

    @property
    def chosen(self) -> XorCandidate | None:
        valid = self.valid_candidates
        return valid[0] if len(valid) == 1 else None

    @property
    def build_kind(self) -> BuildKind:
        chosen = self.chosen
        return chosen.kind if chosen else BuildKind.UNKNOWN

    @property
    def entry_point(self) -> int | None:
        chosen = self.chosen
        return chosen.entry_point if chosen else None

    @property
    def kernel_thunk_addr(self) -> int | None:
        chosen = self.chosen
        return chosen.kernel_thunk_addr if chosen else None

    @property
    def ambiguous(self) -> bool:
        """More than one key set yields plausible addresses: none is chosen."""
        return len(self.valid_candidates) > 1


def section_digest_ok(data: bytes, header: SectionHeader) -> bool | None:
    """Does the section SHA-1 match? Computed over `u32 raw_size || raw data`.

    None if the raw data runs past the file (cannot be verified).
    """
    end = header.raw_addr + header.raw_size
    if end > len(data):
        return None
    message = header.raw_size.to_bytes(4, "little") + data[header.raw_addr : end]
    return hashlib.sha1(message, usedforsecurity=False).digest() == header.digest


@dataclass(frozen=True, slots=True)
class Section:
    """Resolved section: its header, its name and the state of its digest."""

    index: int
    name: str
    header: SectionHeader
    digest_ok: bool | None


@dataclass(frozen=True, slots=True)
class _Backing:
    """Backing of a VA: the region that contains it and its file span `[offset, end)`."""

    section: Section | None  # None: headers region
    offset: int
    end: int


@dataclass(frozen=True, slots=True)
class AddressMap:
    """Translates loaded-image VAs to file offsets.

    The header is backed in `[base, base + size_of_headers)` with
    offset = VA - base. Each section backs only its first `backed_size`
    bytes; the rest of its virtual range is BSS and does not exist in the file. Every
    span is clipped to the end of the file (`file_size`): a truncated XBE does not
    back what it is missing.
    """

    base_address: int
    size_of_headers: int
    file_size: int
    sections: tuple[Section, ...] = ()

    @property
    def headers_end(self) -> int:
        return self.base_address + self.size_of_headers

    def _region_backing(self, va: int) -> _Backing | None:
        """Span declared by the region containing `va`, ignoring the file size."""
        if self.base_address <= va < self.headers_end:
            return _Backing(None, va - self.base_address, self.size_of_headers)
        for section in self.sections:
            start = section.header.virtual_addr
            backed = section.header.backed_size
            if start <= va < start + backed:
                raw = section.header.raw_addr
                return _Backing(section, raw + (va - start), raw + backed)
        return None

    def _backing(self, va: int) -> _Backing | None:
        """Like `_region_backing`, clipped to the end of the file."""
        backing = self._region_backing(va)
        if backing is None:
            return None
        end = min(backing.end, self.file_size)
        return _Backing(backing.section, backing.offset, end) if backing.offset < end else None

    def backed_span(self, va: int) -> tuple[int, int] | None:
        """`(offset, end)` of the file backing `va` up to the end of its region."""
        backing = self._backing(va)
        return (backing.offset, backing.end) if backing else None

    def to_offset(self, va: int) -> int | None:
        """File offset backing `va`, or None (BSS, outside the image or the file)."""
        backing = self._backing(va)
        return backing.offset if backing else None

    def require_offset(self, va: int, what: str) -> int:
        """Like `to_offset`, but `UnmappedAddressError` if there is no backing."""
        offset = self.to_offset(va)
        if offset is None:
            raise UnmappedAddressError(va, what)
        return offset

    def require_header_range(self, va: int, size: int, what: str) -> int:
        """Offset of `[va, va + size)`, which must fit entirely in the headers region.

        The structures the linker puts in the header (section table,
        certificate, library versions) cannot live anywhere else: if the pointer
        leaves it, the XBE is inconsistent.
        """
        if not (self.base_address <= va and va + size <= self.headers_end):
            raise ImageLayoutError(
                f"{what}: [0x{va:08X}, +0x{size:X}) outside the headers region "
                f"[0x{self.base_address:08X}, 0x{self.headers_end:08X})"
            )
        offset = va - self.base_address
        if offset + size > self.file_size:
            raise TruncatedDataError(offset, size)
        return offset

    def is_backed_code(self, va: int) -> bool:
        """Does `va` fall in file bytes of an EXECUTABLE section?"""
        backing = self._backing(va)
        return bool(backing and backing.section and backing.section.header.is_executable)

    def section_name(self, va: int) -> str | None:
        """Region containing `va` in memory, including its BSS."""
        if self.base_address <= va < self.headers_end:
            return HEADERS_REGION
        for section in self.sections:
            start = section.header.virtual_addr
            if start <= va < start + section.header.virtual_size:
                return section.name
        return None


def decode_xor(header: ImageHeader, address_map: AddressMap) -> XorDecoding:
    """De-obfuscate entry point and kernel thunk with each key set.

    There is no field saying whether the XBE is retail or debug: it is deduced because
    only the right key leaves the entry point in code of an EXECUTABLE section and
    the thunk in bytes the file backs. If none or several qualify, the
    type stays UNKNOWN.
    """
    candidates = []
    for keys in XOR_KEYS:
        entry = header.entry_point_encoded ^ keys.entry_point
        thunk = header.kernel_thunk_encoded ^ keys.kernel_thunk
        valid = address_map.is_backed_code(entry) and address_map.to_offset(thunk) is not None
        candidates.append(XorCandidate(keys.kind, entry, thunk, valid))
    return XorDecoding(tuple(candidates))


def _check_image_range(header: ImageHeader) -> None:
    if header.base_address + header.size_of_image > ADDRESS_SPACE:
        raise ImageLayoutError(
            f"the image [0x{header.base_address:08X}, +0x{header.size_of_image:X}) "
            "overflows 32 bits"
        )


def _check_section_range(section: SectionHeader, name: str, header: ImageHeader) -> None:
    """The section's virtual range must fit in 32 bits and inside the image."""
    start, end = section.virtual_addr, section.virtual_addr + section.virtual_size
    if end > ADDRESS_SPACE:
        raise ImageLayoutError(
            f"section {name!r}: [0x{start:08X}, +0x{section.virtual_size:X}) overflows 32 bits"
        )
    image_end = header.base_address + header.size_of_image
    if start < header.base_address or end > image_end:
        raise ImageLayoutError(
            f"section {name!r}: [0x{start:08X}, 0x{end:08X}) outside the image "
            f"[0x{header.base_address:08X}, 0x{image_end:08X})"
        )


def _read_sections(data: bytes, header: ImageHeader) -> tuple[Section, ...]:
    """Section table, validated before walking it; lives in the headers region."""
    count = header.section_count
    if count > MAX_SECTIONS:
        raise ImageLayoutError(f"section_count {count} exceeds the bound of {MAX_SECTIONS}")
    headers_only = AddressMap(header.base_address, header.size_of_headers, len(data))
    table = headers_only.require_header_range(
        header.section_headers_addr, count * SECTION_HEADER_SIZE, "section table"
    )
    sections: list[Section] = []
    for index in range(count):
        section_header = SectionHeader.parse(data, table + index * SECTION_HEADER_SIZE)
        name_offset = headers_only.to_offset(section_header.name_addr)
        name = (
            decode_ascii(read_c_bytes(data, name_offset, SECTION_NAME_MAX))
            if name_offset is not None
            else UNKNOWN_SECTION_NAME
        )
        _check_section_range(section_header, name, header)
        digest_ok = section_digest_ok(data, section_header)
        sections.append(Section(index, name, section_header, digest_ok))
    return tuple(sections)


@dataclass(frozen=True, slots=True)
class XbeImage:
    """An XBE in memory: file bytes + resolved header, sections and XOR keys."""

    name: str
    data: bytes = field(repr=False)
    header: ImageHeader
    address_map: AddressMap
    xor: XorDecoding

    @classmethod
    def from_bytes(cls, data: bytes, name: str = "<memory>") -> XbeImage:
        """Parse and validate the header and section table; the rest is read on demand."""
        header = ImageHeader.parse(data)
        _check_image_range(header)
        sections = _read_sections(data, header)
        address_map = AddressMap(header.base_address, header.size_of_headers, len(data), sections)
        return cls(
            name=name,
            data=data,
            header=header,
            address_map=address_map,
            xor=decode_xor(header, address_map),
        )

    @classmethod
    def load(cls, path: Path) -> XbeImage:
        """Read the whole file (XBEs fit in memory with plenty of room)."""
        return cls.from_bytes(path.read_bytes(), path.name)

    @property
    def sections(self) -> tuple[Section, ...]:
        return self.address_map.sections

    def c_bytes_at(self, va: int, max_len: int = DEFAULT_C_STRING_MAX) -> bytes | None:
        """C string at `va`, cut at the end of its region (there, in memory, are zeros)."""
        span = self.address_map.backed_span(va)
        if span is None:
            return None
        offset, end = span
        return read_c_bytes(self.data, offset, min(max_len, end - offset))

    def utf16_at(self, va: int, max_chars: int = DEFAULT_UTF16_MAX_CHARS) -> str | None:
        """0x0000-terminated UTF-16LE string at `va`, cut at the end of its region."""
        span = self.address_map.backed_span(va)
        if span is None:
            return None
        offset, end = span
        return decode_utf16z(self.data[offset : min(offset + 2 * max_chars, end)])

    def validation_issues(self) -> tuple[str, ...]:
        """Problems that cast doubt on the XBE's integrity (empty if all is well)."""
        issues = [
            _digest_issue(section) for section in self.sections if section.digest_ok is not True
        ]
        issues += self._overlap_issues()
        xor = self.xor
        if xor.ambiguous:
            kinds = ", ".join(candidate.kind for candidate in xor.valid_candidates)
            issues.append(f"ambiguous XOR keys ({kinds}): no build type is chosen")
        elif xor.chosen is None:
            issues.append(
                "no known XOR key yields an entry point in code and a kernel thunk "
                "backed by the file"
            )
        return tuple(issues)

    def _overlap_issues(self) -> list[str]:
        """Memory regions that overlap: the VA -> file map would stop being unique."""
        regions = [("the headers region", self.header.base_address, self.address_map.headers_end)]
        regions += [
            (
                f"section {s.name!r}",
                s.header.virtual_addr,
                s.header.virtual_addr + s.header.virtual_size,
            )
            for s in self.sections
            if s.header.virtual_size
        ]
        return [
            f"{first} and {second} overlap in memory"
            for (first, start1, end1), (second, start2, end2) in combinations(regions, 2)
            if start1 < end2 and start2 < end1
        ]


def _digest_issue(section: Section) -> str:
    if section.digest_ok is None:
        return f"section {section.name!r}: data outside the file, SHA-1 cannot be verified"
    return f"section {section.name!r}: the SHA-1 does not match the one in its header"
