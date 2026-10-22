"""Exact shape of the inventory JSON documents (one TypedDict per object).

`inventory` builds them, `report` presents them and the JSON on disk is their dump as is,
so changing a class here changes the report format.
"""

from __future__ import annotations

from typing import NotRequired, TypedDict


class IssueRecord(TypedDict):
    code: str
    index: int | None
    detail: str


class AttrRecord(TypedDict):
    location: str
    offset: int
    size: int


class CustomFieldRecord(TypedDict):
    equals_header_u32: int
    equals_size: int
    neither: int


class EntryRecord(TypedDict):
    index: int
    offset: int
    rel_offset: int
    size: int
    name: str | None
    timestamp: str | None
    kind: str
    ext: str
    entropy: float
    band: str
    head_hex: str
    head_ascii: str
    nested: NotRequired[ArchiveRecord]
    nested_error: NotRequired[str]


class ArchiveRecord(TypedDict):
    """Card for one AFS. `count` is the TOC's; `null_entries`, `entries_bytes` and
    `first_data_offset` come from the entries listed in `entries`."""

    label: str
    size: int
    magic: str
    count: int
    null_entries: int
    has_names: bool
    attr: AttrRecord
    entries_bytes: int
    first_data_offset: int
    custom_field: CustomFieldRecord
    issues: list[IssueRecord]
    entries: list[EntryRecord]
    file: NotRequired[str]
    error: NotRequired[str]
    truncated: NotRequired[bool]  # only present (and True) if a budget cut the traversal short


class TotalsRecord(TypedDict):
    archives: int
    archives_with_names: int
    entries: int
    entries_all_levels: int
    null_entries: int
    named_entries: int
    pct_named: float
    archive_bytes: int
    entry_bytes: int


class ExampleRecord(TypedDict):
    ref: str
    name: str | None
    size: int
    head_hex: str


class KindRecord(TypedDict):
    kind: str
    entries: int
    bytes: int
    bands: dict[str, int]
    exts: dict[str, int]
    examples: list[ExampleRecord]
    entropy_mean: float
    description: str


class BandRecord(TypedDict):
    entries: int
    bytes: int


class NestedRecord(TypedDict):
    ref: str
    name: str | None
    size: int
    count: int | None
    error: str | None


class ExtensionRecord(TypedDict):
    ext: str
    entries: int
    bytes: int
    entropy_mean: float
    kinds: dict[str, int]


class InvariantsRecord(TypedDict):
    archives_ok: int
    issue_counts: dict[str, int]
    issue_examples: dict[str, list[str]]


class SummaryRecord(TypedDict):
    totals: TotalsRecord
    attr_pointer_locations: dict[str, int]
    custom_field: dict[str, int]
    magic_histogram: list[KindRecord]
    entropy_bands: dict[str, BandRecord]
    nested_afs: list[NestedRecord]
    name_extensions: list[ExtensionRecord]
    invariants: InvariantsRecord


class InventoryRecord(TypedDict):
    generated: str
    root: str
    tool: str
    entropy_window: int
    summary: SummaryRecord
    archives: list[ArchiveRecord]
