"""Reader, extractor and inventory tool for AFS containers (CRI Middleware).

Modules, bottom up:

- `archive`: structures and parsing of header, TOC and attributes; streaming reads.
- `invariants`: audit of an already-parsed AFS (alignment, overlaps, custom field).
- `classify`: what is inside each entry (magics, Team Ninja chunks, entropy).
- `records`: exact shape (TypedDict) of the inventory JSON documents.
- `inventory`: cards and aggregates for all the AFS files in a directory.
- `report`: text presentation (listing, Markdown and JSON).
- `extract`: streaming extraction, always outside any git repo.
- `cli`: `python -I -m ngb_tools.afs {list,extract,inventory}`.

The paths (data root, repo guard and atomic writes) come from `ngb_tools._paths`, shared
with the other tools.

Only the read API is re-exported here; the rest is imported from its module.
"""

from __future__ import annotations

from ngb_tools.afs.archive import (
    MAGIC,
    MAGIC_VARIANT,
    AfsArchive,
    AfsEntry,
    AttrEntry,
    AttrLocation,
    AttrPointer,
    ByteSource,
    iter_entry_chunks,
    open_afs,
    read_entry_prefix,
)
from ngb_tools.afs.errors import (
    AfsError,
    AfsFormatError,
    AfsTruncatedError,
    AfsUsageError,
    UnsafeOutputError,
)

__all__ = [
    "MAGIC",
    "MAGIC_VARIANT",
    "AfsArchive",
    "AfsEntry",
    "AfsError",
    "AfsFormatError",
    "AfsTruncatedError",
    "AfsUsageError",
    "AttrEntry",
    "AttrLocation",
    "AttrPointer",
    "ByteSource",
    "UnsafeOutputError",
    "iter_entry_chunks",
    "open_afs",
    "read_entry_prefix",
]
