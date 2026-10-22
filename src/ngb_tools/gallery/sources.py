"""Where the characters come from: the `chr_*.afs` files, their main KMD and motion bank.

- **Main KMD** of `chr_<name>.afs`: the `<name>.kmd` entry and, if there is none, the first
  KMD by index (`chr_heri` carries `heli.kmd`; `chr_arub`, `arub.kmd` at #4). Some AFS files
  have no KMD at all (`chr_dragn`, whose dragon lives in `chr_s_mur`, and `chr_e_ryu`).
- **Motion bank**: looked up by name, `<name>.mtn` (or `.mot`), first in the character's own
  AFS, then in `cmn.afs` and then in the `*_cmn.afs` files. Ryu's costumes (`ryua`...`ryug`)
  use `ryu.mtn` from `cmn.afs`. If the KMD has a different name, that one is tried too.

The AFS files are opened with `ngb_tools.afs.archive`: only the header and table are read,
and from each entry, the bytes that are requested.
"""

from __future__ import annotations

import os
from collections.abc import Iterable, Sequence
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Final

from ngb_tools.afs.archive import AfsArchive, AfsEntry, open_afs, read_at
from ngb_tools.afs.errors import AfsTruncatedError

CHARACTER_PREFIX: Final = "chr_"
COMMON_ARCHIVE: Final = "cmn.afs"
COMMON_SUFFIX: Final = "_cmn.afs"
MOTION_SUFFIXES: Final = (".mtn", ".mot")
RYU_COSTUME_PREFIX: Final = "ryu"


@dataclass(frozen=True, slots=True)
class EntryRef:
    """An entry of an AFS: file, index, name and byte extent."""

    archive: Path
    index: int
    name: str
    offset: int
    size: int

    def read(self) -> bytes:
        with self.archive.open("rb") as handle:
            data = read_at(handle, self.offset, self.size)
        if len(data) != self.size:
            raise OSError(f"{self.archive.name}#{self.index}: truncated entry")
        return data

    @property
    def stem(self) -> str:
        return PurePosixPath(self.name).stem

    @property
    def suffix(self) -> str:
        return PurePosixPath(self.name).suffix.lower()

    def __str__(self) -> str:
        return f"{self.archive.name}#{self.index} {self.name}"


def list_entries(path: Path) -> list[EntryRef]:
    """Named entries with data of an AFS; if any does not fit in the file, it is truncated.

    Without that check, a cut-off AFS loses its name table (it goes at the end) and looks
    like an AFS without a KMD.
    """
    with path.open("rb") as handle:
        archive: AfsArchive = open_afs(handle, path.name)
        size = handle.seek(0, os.SEEK_END)
    for entry in archive.data_entries():
        if entry.offset + entry.size > size:
            raise AfsTruncatedError(
                f"entry {entry.index} ends at {entry.offset + entry.size:#x}, "
                f"the file is {size:#x} long"
            )
    return [_ref(path, entry) for entry in archive.data_entries() if entry.name]


def _ref(path: Path, entry: AfsEntry) -> EntryRef:
    return EntryRef(path, entry.index, entry.name or "", entry.offset, entry.size)


def character_name(path: Path) -> str:
    """`chr_ryu.afs` -> `ryu`."""
    return path.stem.removeprefix(CHARACTER_PREFIX)


def character_archives(game_dir: Path) -> list[Path]:
    return sorted(game_dir.glob(f"{CHARACTER_PREFIX}*.afs"))


def common_archives(game_dir: Path) -> list[Path]:
    """`cmn.afs` first and then the `*_cmn.afs` files, by name."""
    common = [game_dir / COMMON_ARCHIVE] if (game_dir / COMMON_ARCHIVE).is_file() else []
    return common + sorted(p for p in game_dir.glob(f"*{COMMON_SUFFIX}") if p.is_file())


def main_kmd(entries: Sequence[EntryRef], name: str) -> EntryRef | None:
    """The KMD named like the character or, failing that, the first by index."""
    kmds = [e for e in entries if e.suffix == ".kmd"]
    named = [e for e in kmds if e.stem.lower() == name.lower()]
    if named:
        return named[0]
    return kmds[0] if kmds else None


def motion_candidates(name: str, kmd_stem: str | None) -> list[str]:
    """Bank names to try, in order."""
    names = [name, kmd_stem or name]
    if name.startswith(RYU_COSTUME_PREFIX):
        names.append(RYU_COSTUME_PREFIX)
    return list(dict.fromkeys(n.lower() for n in names))


class MotionIndex:
    """Motion banks by name in the common AFS files (read on demand)."""

    def __init__(self, archives: Iterable[Path]) -> None:
        self._archives = list(archives)
        self._index: dict[str, EntryRef] | None = None

    def _build(self) -> dict[str, EntryRef]:
        index: dict[str, EntryRef] = {}
        for archive in self._archives:
            for entry in list_entries(archive):
                if entry.suffix in MOTION_SUFFIXES:
                    index.setdefault(entry.stem.lower(), entry)
        return index

    def find(self, own: Sequence[EntryRef], candidates: Sequence[str]) -> EntryRef | None:
        """The first candidate in the AFS itself and, if absent, in the common ones."""
        local = {e.stem.lower(): e for e in own if e.suffix in MOTION_SUFFIXES}
        for name in candidates:
            if name in local:
                return local[name]
        if self._index is None:
            self._index = self._build()
        for name in candidates:
            if name in self._index:
                return self._index[name]
        return None
