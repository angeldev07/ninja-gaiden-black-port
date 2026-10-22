"""The parts of a zone: the `<zone>_NN` KMDs of its AFS files, without repeating identical variants.

A zone (`boff`, `tln_a`...) is the `<zone>_NN.afs` files on the disc plus `<zone>_cmn.afs`
(VERIFIED: 17 zones in `game/`; `hell`, `pyrmd` and `stdim` only have `_cmn`). Each
`<zone>_NN.afs` carries its stage `<zone>_NN.kmd` (entry 0, except `boff_05.afs`, which has
it at entry 2) and sometimes props. `<zone>_cmn.afs` carries the common props and other
part-named KMDs that are also in world coordinates (`boff_99.kmd`: sky and background).

A part is every `<zone>_<digits>.kmd` entry of those AFS files. Props (`bf_door1.kmd`...) are
excluded: their vertices are local and their placement lives in the `.sty` scripts (not
yet deciphered).

**Variants.** Some parts are the same file under another number: `tomb_01` = `tomb_04`,
`tomb_02` = `tomb_05`, `tomb_03` = `tomb_06` and `tln_b_04` = `tln_b_05` (identical sha256).
The first is exported and the others are flagged as duplicates. Others are distinct variants
of the same place (`tln_a_01` and `tln_a_11`) and overlap if requested together.
"""

from __future__ import annotations

import hashlib
import re
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Final

from ngb_tools.afs import AfsEntry, iter_entry_chunks, open_afs
from ngb_tools.stage.errors import ZoneNotFoundError

COMMON_SUFFIX: Final = "cmn"
ZONE_PATTERN: Final = re.compile(r"[a-z][a-z0-9]*(?:_[a-z][a-z0-9]*)*")
_NUMBER: Final = r"(\d{1,3})"


@dataclass(frozen=True, slots=True)
class ZonePart:
    """A stage KMD inside a zone AFS."""

    zone: str
    number: str
    afs: Path
    entry: AfsEntry
    common: bool

    @property
    def name(self) -> str:
        return f"{self.zone}_{self.number}"


@dataclass(frozen=True, slots=True)
class LoadedPart:
    """A part with its bytes; `duplicate_of` is the identical part already loaded, if any."""

    part: ZonePart
    data: bytes
    sha256: str
    duplicate_of: str | None = None


def check_zone_name(zone: str) -> str:
    """The lowercase name, or `ZoneNotFoundError` if it is not a valid zone name.

    Only letters, digits and `_` between words: the name ends up in output paths, so no
    separators, `..` or drive letters.
    """
    name = zone.lower()
    if not ZONE_PATTERN.fullmatch(name):
        raise ZoneNotFoundError(f"invalid zone name: {zone!r}")
    return name


def _zone_afs(game_dir: Path, zone: str) -> list[tuple[Path, bool]]:
    pattern = re.compile(rf"{re.escape(zone)}_(?:{_NUMBER}|{COMMON_SUFFIX})\.afs", re.IGNORECASE)
    found = []
    for path in sorted(game_dir.iterdir()):
        match = pattern.fullmatch(path.name)
        if match is not None and path.is_file():
            found.append((path, match.group(1) is None))
    return found


def find_zone_parts(game_dir: Path, zone: str) -> list[ZonePart]:
    """The zone parts in `game_dir`, by number (those from `_cmn` last on ties)."""
    name = check_zone_name(zone)
    archives = _zone_afs(game_dir, name)
    if not archives:
        raise ZoneNotFoundError(f"no AFS for zone '{name}' in {game_dir}")
    pattern = re.compile(rf"{re.escape(name)}_{_NUMBER}\.kmd", re.IGNORECASE)
    parts: list[ZonePart] = []
    for path, common in archives:
        with path.open("rb") as handle:
            archive = open_afs(handle, path.name)
        for entry in archive.data_entries():
            match = pattern.fullmatch(entry.name or "")
            if match is not None:
                parts.append(ZonePart(name, match.group(1), path, entry, common))
    if not parts:
        raise ZoneNotFoundError(f"the AFS files of '{name}' contain no {name}_NN.kmd")
    return sorted(parts, key=lambda p: (int(p.number), p.common, p.afs.name, p.entry.index))


def select_parts(parts: Sequence[ZonePart], wanted: Sequence[str] | None) -> list[ZonePart]:
    """The parts requested by number (`01`, `1` or `boff_01`); `None` = all."""
    if wanted is None:
        return list(parts)
    chosen: list[ZonePart] = []
    for item in wanted:
        key = item.lower().rsplit("_", 1)[-1]
        if not key.isdigit():
            raise ZoneNotFoundError(f"invalid part: {item!r} (a number is expected, e.g. 01)")
        matches = [p for p in parts if int(p.number) == int(key) and p not in chosen]
        if not matches:
            available = ", ".join(p.number for p in parts)
            raise ZoneNotFoundError(f"no part {item!r}; available: {available}")
        chosen += matches
    return chosen


def read_part(part: ZonePart) -> bytes:
    with part.afs.open("rb") as handle:
        return b"".join(iter_entry_chunks(handle, part.entry))


def load_parts(parts: Sequence[ZonePart]) -> list[LoadedPart]:
    """Read each part and mark those that repeat an earlier one byte for byte."""
    seen: dict[str, str] = {}
    loaded: list[LoadedPart] = []
    for part in parts:
        data = read_part(part)
        digest = hashlib.sha256(data).hexdigest()
        loaded.append(LoadedPart(part, data, digest, seen.get(digest)))
        seen.setdefault(digest, part.name)
    return loaded
