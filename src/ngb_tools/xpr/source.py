"""Opening an XPR0: standalone (`.xpr`, `.spr`) or inside a KMD chunk (`--kmd`).

In a KMD the XPR0 is one of the slots of the Team Ninja chunk (see `ngb_tools.teamninja`).
What matters for the manifest is where it starts inside the file (`offset`): the
`Data` fields are relative to the XPR0 data region, not to the file.

The file is read once; the XPR0 is a view (`memoryview`) over those bytes, with no copy.
If the file is not what was asked for (no XPR0 magic, or a chunk without an XPR0 slot), the
error is an input error (`XprInputError`); if it is an XPR0 but broken, a format error
(`XprFormatError`).
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import StrEnum
from pathlib import Path

from ngb_tools.teamninja import TeamNinjaError, find_xpr0_slot, parse_chunk
from ngb_tools.xpr.errors import XprInputError
from ngb_tools.xpr.resources import XPR_MAGIC, XprFile, parse_xpr


class Container(StrEnum):
    """Where the XPR0 came from."""

    XPR = "xpr"
    KMD = "kmd"


@dataclass(frozen=True, slots=True)
class XprSource:
    """A parsed XPR0 and its position in the source file."""

    path: Path
    container: Container
    offset: int
    slot: int | None
    xpr: XprFile


def _chunk_hint(data: bytes) -> str:
    """If the bytes look like a Team Ninja chunk, suggest `--kmd`."""
    try:
        chunk = parse_chunk(data)
    except TeamNinjaError:
        return ""
    return f"; looks like a Team Ninja chunk '{chunk.tag}': try --kmd"


def _open_kmd(path: Path, data: bytes) -> XprSource:
    try:
        slot = find_xpr0_slot(data)
    except TeamNinjaError as error:
        raise XprInputError(f"{path.name}: {error}") from error
    xpr = parse_xpr(memoryview(data)[slot.offset : slot.end])
    return XprSource(path, Container.KMD, slot.offset, slot.index, xpr)


def open_source(path: Path, *, kmd: bool) -> XprSource:
    """Read `path` and parse its XPR0; with `kmd`, look for it among the chunk slots."""
    data = path.read_bytes()
    if kmd:
        return _open_kmd(path, data)
    if not data.startswith(XPR_MAGIC):
        raise XprInputError(f"{path.name}: does not start with {XPR_MAGIC!r}{_chunk_hint(data)}")
    return XprSource(path, Container.XPR, 0, None, parse_xpr(data))
