"""Structures and parsing of AFS containers (CRI Middleware) without loading their data.

Format (little endian; cross-checked against MaikelChan's AFSLib and Sewer56's AfsLib)::

    0x00  char[4]  magic "AFS\\0" (some games use "AFS " = 0x20)
    0x04  u32      N = number of entries
    0x08  N x {u32 offset, u32 size}   TOC; (0, 0) = null entry
    ...   {u32 offset, u32 size}       pointer to the attribute table, at one of two
                                       positions: right after the TOC (0x08 + N*8) or in
                                       the 8 bytes before the first data. May be missing.

    Attributes: N x 0x30 bytes = name[32] (ASCII, NUL-padded), 6 x u16 date
                (year, month, day, hour, minute, second) and a "custom" u32.
    Data aligned to 0x800 (the DVD sector).

The TOC offsets are relative to the start of the AFS. `AfsEntry.offset` is absolute in the
physical file, so a nested AFS is read in place, without copying it.
"""

from __future__ import annotations

import datetime as dt
import os
import struct
from collections.abc import Iterator, Sequence
from dataclasses import dataclass
from enum import StrEnum
from typing import Final, Protocol

from ngb_tools.afs.errors import AfsFormatError, AfsTruncatedError, AfsUsageError

MAGIC: Final = b"AFS\x00"
MAGIC_VARIANT: Final = b"AFS\x20"
HEADER_SIZE: Final = 8  # magic + N
TOC_ENTRY_SIZE: Final = 8  # offset + size
ATTR_PTR_SIZE: Final = 8  # offset + size of the attribute table
ATTR_ENTRY_SIZE: Final = 0x30  # name[32] + 6 x u16 + u32
ATTR_NAME_SIZE: Final = 32
SECTOR: Final = 0x800
CHUNK_SIZE: Final = 1 << 20

_HEADER: Final = struct.Struct("<4sI")
_PAIR: Final = struct.Struct("<II")  # (offset, size): TOC entry and attribute pointer
_ATTR: Final = struct.Struct(f"<{ATTR_NAME_SIZE}s6HI")

Date = tuple[int, int, int, int, int, int]


class ByteSource(Protocol):
    """All the parser asks of a file: seek and read bytes."""

    def seek(self, offset: int, whence: int = 0, /) -> int: ...

    def read(self, size: int = -1, /) -> bytes: ...


class AttrLocation(StrEnum):
    """Where the pointer to the attribute table appeared."""

    AFTER_TOC = "after_toc"
    BEFORE_DATA = "before_data"
    NONE = "none"


@dataclass(frozen=True, slots=True)
class AttrPointer:
    """Pointer (offset, size) to the attribute table, relative to the start of the AFS."""

    location: AttrLocation
    offset: int = 0
    size: int = 0


NO_ATTR_POINTER: Final = AttrPointer(AttrLocation.NONE)


@dataclass(frozen=True, slots=True)
class AttrEntry:
    """0x30-byte entry of the attribute table.

    In Ninja Gaiden Black `custom` is not the size, as other tools assume: it is the copy
    of the u32 at offset 4 + 4*i of the AFS itself (the header read as u32[]).
    """

    name: str
    date: Date
    custom: int

    @property
    def timestamp(self) -> str | None:
        """Date in ISO 8601, or None if the six fields do not form a real date."""
        try:
            return dt.datetime(*self.date).isoformat(sep=" ")
        except ValueError:
            return None


@dataclass(frozen=True, slots=True)
class AfsEntry:
    """TOC entry. `offset` is absolute; `rel_offset` is the TOC value as is."""

    index: int
    offset: int
    size: int
    rel_offset: int
    attr: AttrEntry | None = None

    @property
    def is_null(self) -> bool:
        """A (0, 0) entry reserves the index without occupying data."""
        return self.rel_offset == 0 and self.size == 0

    @property
    def name(self) -> str | None:
        return self.attr.name if self.attr is not None and self.attr.name else None


@dataclass(frozen=True, slots=True)
class AfsArchive:
    """Parsed AFS (metadata only). `base` > 0 if it lives inside another file."""

    label: str
    base: int
    limit: int
    magic: bytes
    count: int
    attr_ptr: AttrPointer
    entries: tuple[AfsEntry, ...]

    @property
    def toc_end(self) -> int:
        """Relative end of the TOC: first candidate position of the attribute pointer."""
        return HEADER_SIZE + self.count * TOC_ENTRY_SIZE

    @property
    def header_end(self) -> int:
        """End of header + TOC + pointer: no data can start before it."""
        return self.toc_end + ATTR_PTR_SIZE

    @property
    def has_names(self) -> bool:
        return self.attr_ptr.location is not AttrLocation.NONE

    def data_entries(self) -> tuple[AfsEntry, ...]:
        """The entries that occupy bytes (without the null ones)."""
        return tuple(e for e in self.entries if not e.is_null)


def read_at(source: ByteSource, pos: int, size: int) -> bytes:
    """Up to `size` bytes from absolute position `pos` (fewer if the file ends)."""
    source.seek(pos)
    return source.read(size)


def parse_header(raw: bytes) -> tuple[bytes, int]:
    """8-byte header -> (magic, N)."""
    if len(raw) < HEADER_SIZE:
        raise AfsTruncatedError("truncated header")
    header: tuple[bytes, int] = _HEADER.unpack_from(raw)
    magic, count = header
    if magic not in (MAGIC, MAGIC_VARIANT):
        raise AfsFormatError(f"invalid magic {magic!r}")
    return magic, count


def _pair_at(raw: bytes, offset: int) -> tuple[int, int]:
    pair: tuple[int, int] = _PAIR.unpack_from(raw, offset)
    return pair


def parse_toc(raw: bytes, count: int) -> tuple[tuple[int, int], ...]:
    """TOC of N x (offset, size) pairs."""
    if len(raw) < count * TOC_ENTRY_SIZE:
        raise AfsTruncatedError("truncated TOC")
    return tuple(_pair_at(raw, i * TOC_ENTRY_SIZE) for i in range(count))


def parse_attr_entry(raw: bytes, offset: int = 0) -> AttrEntry:
    """Attribute entry at `raw[offset:]`: name[32], 6 x u16 date, u32 custom."""
    fields: tuple[bytes, int, int, int, int, int, int, int] = _ATTR.unpack_from(raw, offset)
    name_raw, year, month, day, hour, minute, second, custom = fields
    name = name_raw.split(b"\x00", 1)[0].decode("ascii", "backslashreplace")
    return AttrEntry(name=name, date=(year, month, day, hour, minute, second), custom=custom)


def attr_pointer_is_valid(offset: int, size: int, count: int, min_offset: int, limit: int) -> bool:
    """AFSLib criterion: non-null, after the header, inside the AFS and with room for N entries."""
    return (
        offset > 0
        and size > 0
        and offset >= min_offset
        and offset + size <= limit
        and size >= count * ATTR_ENTRY_SIZE
    )


def locate_attr_pointer(
    source: ByteSource, base: int, count: int, toc: Sequence[tuple[int, int]], limit: int
) -> AttrPointer:
    """Try the pointer after the TOC and, if invalid, the one just before the first data."""
    toc_end = HEADER_SIZE + count * TOC_ENTRY_SIZE
    candidates = [(AttrLocation.AFTER_TOC, toc_end)]
    first_data = min((offset for offset, _ in toc if offset), default=0)
    if first_data >= toc_end + ATTR_PTR_SIZE:
        candidates.append((AttrLocation.BEFORE_DATA, first_data - ATTR_PTR_SIZE))
    for location, pos in candidates:
        if pos + ATTR_PTR_SIZE > limit:
            continue
        raw = read_at(source, base + pos, ATTR_PTR_SIZE)
        if len(raw) < ATTR_PTR_SIZE:
            continue
        offset, size = _pair_at(raw, 0)
        if attr_pointer_is_valid(offset, size, count, toc_end + ATTR_PTR_SIZE, limit):
            return AttrPointer(location, offset, size)
    return NO_ATTR_POINTER


def read_attr_table(
    source: ByteSource, base: int, ptr: AttrPointer, count: int
) -> tuple[AttrEntry, ...]:
    """The N attribute entries that `ptr` points to."""
    size = count * ATTR_ENTRY_SIZE
    raw = read_at(source, base + ptr.offset, size)
    if len(raw) < size:
        raise AfsTruncatedError(f"truncated attribute table at {ptr.offset:#x}")
    return tuple(parse_attr_entry(raw, i * ATTR_ENTRY_SIZE) for i in range(count))


def open_afs(
    source: ByteSource, label: str, *, base: int = 0, limit: int | None = None
) -> AfsArchive:
    """Parse header, TOC and attributes of the AFS starting at `base`, without reading the data.

    `limit` is the size of the AFS; by default, from `base` to the end of the file.
    """
    if limit is None:
        limit = source.seek(0, os.SEEK_END) - base
    magic, count = parse_header(read_at(source, base, HEADER_SIZE))
    toc_size = count * TOC_ENTRY_SIZE
    if HEADER_SIZE + toc_size > limit:
        raise AfsTruncatedError(f"TOC of {count} entries does not fit in {limit} bytes")
    toc = parse_toc(read_at(source, base + HEADER_SIZE, toc_size), count)
    ptr = locate_attr_pointer(source, base, count, toc, limit)
    attrs: Sequence[AttrEntry | None] = (
        read_attr_table(source, base, ptr, count)
        if ptr.location is not AttrLocation.NONE
        else (None,) * count
    )
    entries = tuple(
        AfsEntry(index, base + offset, size, offset, attr)
        for index, ((offset, size), attr) in enumerate(zip(toc, attrs, strict=True))
    )
    return AfsArchive(label, base, limit, magic, count, ptr, entries)


def read_entry_prefix(source: ByteSource, entry: AfsEntry, size: int) -> bytes:
    """The first `size` bytes of the entry (or fewer, if the entry is shorter)."""
    return read_at(source, entry.offset, min(size, entry.size))


def iter_entry_chunks(
    source: ByteSource, entry: AfsEntry, chunk_size: int = CHUNK_SIZE
) -> Iterator[bytes]:
    """Walk the entry's content in blocks, without loading it whole into memory.

    Each block is read with its own `seek`, so interleaving other reads of the same
    `source` while the iterator is consumed does not scramble the data.
    """
    if chunk_size <= 0:
        raise AfsUsageError(f"chunk_size must be positive, not {chunk_size}")
    pos, remaining = entry.offset, entry.size
    while remaining > 0:
        block = read_at(source, pos, min(chunk_size, remaining))
        if not block:
            raise AfsTruncatedError(f"entry {entry.index} truncated: {remaining} bytes missing")
        pos += len(block)
        remaining -= len(block)
        yield block
