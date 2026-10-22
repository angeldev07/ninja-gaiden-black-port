"""Audit of an already-parsed AFS: what a healthy AFS satisfies and this file does not.

The rules come from how CRI packs data for the DVD: each entry starts at a sector (0x800),
after the header, inside the file and without overlapping another; the attribute table
takes exactly N x 0x30 bytes and is also aligned.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import StrEnum
from typing import NamedTuple

from ngb_tools.afs.archive import (
    ATTR_ENTRY_SIZE,
    MAGIC_VARIANT,
    SECTOR,
    AfsArchive,
    ByteSource,
    read_at,
)


class IssueCode(StrEnum):
    """Issue type; the value is the stable code that appears in the reports."""

    MAGIC_VARIANT = "magic_variant"
    MISALIGNED = "misaligned"
    INSIDE_HEADER = "inside_header"
    OUT_OF_BOUNDS = "out_of_bounds"
    ATTR_SIZE = "attr_size"
    ATTR_MISALIGNED = "attr_misaligned"
    BAD_DATE = "bad_date"
    OVERLAP = "overlap"
    UNREADABLE = "unreadable"
    NESTED_REVISIT = "nested_revisit"  # a nested AFS already inventoried in the same file
    ENTRY_BUDGET = "entry_budget"  # the per-file card limit was exhausted
    METADATA_BUDGET = "metadata_budget"  # the children metadata byte limit was exhausted


@dataclass(frozen=True, slots=True)
class Issue:
    """An issue; `index` is the affected entry, or None if it concerns the whole file."""

    code: IssueCode
    detail: str
    index: int | None = None

    def __str__(self) -> str:
        where = "" if self.index is None else f" #{self.index}"
        return f"{self.code}{where}: {self.detail}"


class _Span(NamedTuple):
    """Range [start, end) relative to the AFS; owner None = the attribute table."""

    start: int
    end: int
    owner: int | None

    @property
    def label(self) -> str:
        return "attr" if self.owner is None else str(self.owner)


def _entry_issues(archive: AfsArchive) -> list[Issue]:
    issues: list[Issue] = []
    for e in archive.data_entries():
        if e.rel_offset % SECTOR:
            detail = f"offset {e.rel_offset:#x} not a multiple of {SECTOR:#x}"
            issues.append(Issue(IssueCode.MISALIGNED, detail, e.index))
        if e.rel_offset < archive.header_end:
            detail = f"offset {e.rel_offset:#x} < header end {archive.header_end:#x}"
            issues.append(Issue(IssueCode.INSIDE_HEADER, detail, e.index))
        if e.rel_offset + e.size > archive.limit:
            detail = f"{e.rel_offset:#x}+{e.size:#x} > {archive.limit:#x}"
            issues.append(Issue(IssueCode.OUT_OF_BOUNDS, detail, e.index))
    return issues


def _attr_issues(archive: AfsArchive) -> list[Issue]:
    if not archive.has_names:
        return []
    ptr, expected = archive.attr_ptr, archive.count * ATTR_ENTRY_SIZE
    issues: list[Issue] = []
    if ptr.size != expected:
        detail = f"table {ptr.size:#x} != N*0x30 = {expected:#x}"
        issues.append(Issue(IssueCode.ATTR_SIZE, detail))
    if ptr.offset % SECTOR:
        issues.append(Issue(IssueCode.ATTR_MISALIGNED, f"table at {ptr.offset:#x}"))
    for e in archive.data_entries():
        # An all-zero date means "no date", not a corrupt date.
        if e.attr is not None and e.attr.timestamp is None and any(e.attr.date):
            issues.append(Issue(IssueCode.BAD_DATE, f"invalid date {e.attr.date}", e.index))
    return issues


def _overlap_issues(archive: AfsArchive) -> list[Issue]:
    spans = [_Span(e.rel_offset, e.rel_offset + e.size, e.index) for e in archive.data_entries()]
    if archive.has_names:
        ptr = archive.attr_ptr
        spans.append(_Span(ptr.offset, ptr.offset + ptr.size, None))
    spans.sort(key=lambda s: (s.start, s.end))
    # Sweep: each range is compared with the one that reaches farthest among the previous
    # ones, not just with its neighbor. That way an entry that wraps several overlaps all
    # of them.
    issues: list[Issue] = []
    reach: _Span | None = None
    for span in spans:
        if reach is not None and span.start < reach.end:
            detail = (
                f"{reach.label} [{reach.start:#x},{reach.end:#x}) overlaps {span.label} "
                f"at {span.start:#x}"
            )
            issues.append(Issue(IssueCode.OVERLAP, detail, span.owner))
        if reach is None or span.end > reach.end:
            reach = span
    return issues


def validate_archive(archive: AfsArchive) -> tuple[Issue, ...]:
    """All the AFS issues, in a stable order (header, entries, table, overlaps)."""
    head = (
        [Issue(IssueCode.MAGIC_VARIANT, "magic 'AFS ' (0x20)")]
        if archive.magic == MAGIC_VARIANT
        else []
    )
    return (*head, *_entry_issues(archive), *_attr_issues(archive), *_overlap_issues(archive))


@dataclass(frozen=True, slots=True)
class CustomFieldStats:
    """Count of hypotheses about the `custom` u32 (they are not mutually exclusive)."""

    equals_header_u32: int
    equals_size: int
    neither: int


def custom_field_semantics(source: ByteSource, archive: AfsArchive) -> CustomFieldStats:
    """Compare each entry's `custom` with its size and with the u32 at offset 4 + 4*i.

    The full header (8 + 8*N bytes) always covers those 4 + 4*N bytes, so the read cannot
    come up short on an AFS that could be opened.
    """
    if not archive.has_names:
        return CustomFieldStats(0, 0, 0)
    raw = read_at(source, archive.base, 4 + 4 * archive.count)
    header_hits = size_hits = neither = 0
    for e in archive.entries:
        if e.attr is None:
            continue
        pos = 4 + 4 * e.index
        hit_header = e.attr.custom == int.from_bytes(raw[pos : pos + 4], "little")
        hit_size = e.attr.custom == e.size
        header_hits += hit_header
        size_hits += hit_size
        neither += not (hit_header or hit_size)
    return CustomFieldStats(header_hits, size_hits, neither)
