"""Streaming extraction of AFS entries, always outside any git repo.

The game data is copyrighted and must never end up versioned, so the output path goes
through `ngb_tools._paths.ensure_outside_repos` (no exceptions: not even `build/`). By
default it extracts to `$NGB_DATA/extracted/<AFS path inside game/, without .afs>`.

The whole request is validated (format, indices and bounds) before anything is written;
each file is written atomically (`.part` and rename), so a file with its final name is
always complete.
"""

from __future__ import annotations

import json
import re
from collections.abc import Iterable, Mapping, Sequence
from pathlib import Path, PurePosixPath
from typing import Final, TypedDict

from ngb_tools._paths import (
    EXTRACTED_SUBDIR,
    GAME_SUBDIR,
    atomic_write_bytes,
    atomic_write_chunks,
    data_root,
    ensure_outside_repos,
)
from ngb_tools.afs.archive import (
    AfsArchive,
    AfsEntry,
    iter_entry_chunks,
    open_afs,
    read_entry_prefix,
)
from ngb_tools.afs.classify import ENTROPY_WINDOW, classify
from ngb_tools.afs.errors import AfsTruncatedError, AfsUsageError

MANIFEST_NAME: Final = "_manifest.json"
UNNAMED: Final = "unnamed"
_BAD_CHARS: Final = re.compile(r'[<>:"/\\|?*\x00-\x1f]')


class ManifestEntry(TypedDict):
    index: int
    file: str
    name: str | None
    offset: int
    size: int
    timestamp: str | None
    custom: int | None
    kind: str


class ExtractManifest(TypedDict):
    source: str
    count: int
    attr_location: str
    entries: list[ManifestEntry]


# --- Paths --------------------------------------------------------------------------------


def default_extract_dir(afs_path: Path, environ: Mapping[str, str] | None = None) -> Path:
    """Default extraction folder for `afs_path`, inside `$NGB_DATA/extracted/`.

    The path relative to `game/` (or, failing that, to the data root) is kept, without the
    extension, so two AFS files with the same name in subfolders do not clobber each other:
    `game/cmn.afs` goes to `extracted/cmn` and `game/Media/cmn.afs` to `extracted/Media/cmn`.
    An AFS outside the data root uses only its name.
    """
    root = data_root(environ)
    resolved = afs_path.resolve()
    for base in ((root / GAME_SUBDIR).resolve(), root.resolve()):
        if resolved.is_relative_to(base):
            return root / EXTRACTED_SUBDIR / resolved.relative_to(base).with_suffix("")
    return root / EXTRACTED_SUBDIR / afs_path.stem


def safe_filename(name: str) -> str:
    """Valid Windows name: no reserved characters nor trailing dot or space."""
    return _BAD_CHARS.sub("_", name).rstrip(" .") or UNNAMED


def entry_filename(entry: AfsEntry, guessed_ext: str) -> str:
    """`NNNN_<name>`; if the name has no extension, the deduced one is added."""
    base = safe_filename(entry.name) if entry.name else UNNAMED
    stem = f"{entry.index:04d}_{base}"
    return stem if PurePosixPath(base).suffix else f"{stem}.{guessed_ext}"


# --- Extraction ---------------------------------------------------------------------------


def _select(archive: AfsArchive, indices: Sequence[int] | None) -> list[AfsEntry]:
    """Requested entries, without repeats and in the requested order; null ones have no data."""
    if indices is None:
        wanted = list(archive.entries)
    else:
        unknown = [i for i in indices if not 0 <= i < archive.count]
        if unknown:
            raise AfsUsageError(f"indices out of range 0..{archive.count - 1}: {unknown}")
        wanted = [archive.entries[i] for i in dict.fromkeys(indices)]
    return [e for e in wanted if not e.is_null]


def _check_bounds(archive: AfsArchive, entries: Iterable[AfsEntry]) -> None:
    cut = [f"#{e.index}" for e in entries if e.rel_offset + e.size > archive.limit]
    if cut:
        raise AfsTruncatedError(f"entries past the end of the file: {', '.join(cut)}")


def _write_manifest(out_dir: Path, manifest: ExtractManifest) -> None:
    text = json.dumps(manifest, indent=2, ensure_ascii=False) + "\n"
    # Small: fsync is cheap and the manifest is what vouches for the extraction.
    atomic_write_bytes(out_dir / MANIFEST_NAME, text.encode("utf-8"), fsync=True)


def extract_archive(
    afs_path: Path, out_dir: Path, indices: Sequence[int] | None = None
) -> ExtractManifest:
    """Extract the entries (all, or `indices`) to `out_dir` and write the manifest."""
    out = ensure_outside_repos(out_dir)
    with afs_path.open("rb") as source:
        archive = open_afs(source, afs_path.name)
        entries = _select(archive, indices)
        _check_bounds(archive, entries)
        out.mkdir(parents=True, exist_ok=True)
        records: list[ManifestEntry] = []
        for entry in entries:
            found = classify(read_entry_prefix(source, entry, ENTROPY_WINDOW))
            target = out / entry_filename(entry, found.ext)
            atomic_write_chunks(target, iter_entry_chunks(source, entry))
            records.append(
                ManifestEntry(
                    index=entry.index,
                    file=target.name,
                    name=entry.name,
                    offset=entry.rel_offset,
                    size=entry.size,
                    timestamp=entry.attr.timestamp if entry.attr is not None else None,
                    custom=entry.attr.custom if entry.attr is not None else None,
                    kind=found.kind,
                )
            )
    manifest = ExtractManifest(
        source=str(afs_path),
        count=archive.count,
        attr_location=archive.attr_ptr.location.value,
        entries=records,
    )
    _write_manifest(out, manifest)
    return manifest
