"""Team Ninja chunk header and search for the XPR0 slot of a KMD.

Many AFS files (KMD, itm_dat2, chr_dat...) start with the same 0x20-byte header, all
fields little-endian u32 (verified in `ryu.kmd`):

    0x00  tag[12]    NUL-padded ASCII name ("KMD", "OBJINFO"...)
    0x0C  size       in ryu.kmd, bytes of the chunk; in r_ryu.kmd it does not cover the last
                     two slots, so its meaning is unconfirmed and it is not used as a bound
    0x10  n_slots    entries of the offset table
    0x14  n_used     non-empty entries
    0x18  table_pos  position of the table, relative to the chunk
    0x1C  0x20       size of this header

At `table_pos` there are `u32 off[n_slots]` relative to the start of the chunk; 0 marks an
empty slot. A slot does not declare its size: it extends up to the next occupied offset and
the last one up to the end of the data (which may carry the AFS sector padding).

In `ryu.kmd` the table is at 0x80 and slot 1 is an XPR0 (GPU textures and buffers) at
0x3000. The rest of the KMD (bones, materials, meshes) is left for milestone H3.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from typing import Final

HEADER_SIZE: Final = 0x20
TAG_SIZE: Final = 12
XPR0_MAGIC: Final = b"XPR0"
# Cap on table entries. A KMD has 10 (ryu.kmd, r_ryu.kmd); 4096 leaves
# room for other chunks and stops a hostile `n_slots` from making us unpack
# millions of integers before discovering that the table does not fit.
MAX_SLOTS: Final = 4096

_HEADER: Final = struct.Struct("<12s5I")


class TeamNinjaError(Exception):
    """Root of the `ngb_tools.teamninja` errors."""


class ChunkFormatError(TeamNinjaError):
    """The bytes do not form a coherent Team Ninja chunk."""


class SlotNotFoundError(TeamNinjaError):
    """The chunk does not have the requested slot, or has it more than once."""


@dataclass(frozen=True, slots=True)
class ChunkSlot:
    """An occupied slot: index in the table and extent `[offset, end)` within the chunk."""

    index: int
    offset: int
    end: int

    @property
    def size(self) -> int:
        return self.end - self.offset


@dataclass(frozen=True, slots=True)
class TeamNinjaChunk:
    """Validated header and occupied slots, in table order."""

    tag: str
    size: int
    n_slots: int
    n_used: int
    table_pos: int
    slots: tuple[ChunkSlot, ...]


def _decode_tag(raw: bytes) -> str:
    tag = raw.rstrip(b"\0")
    if not tag or not all(0x20 <= byte <= 0x7E for byte in tag):
        raise ChunkFormatError(f"non-printable tag: {raw!r}")
    return tag.decode("ascii")


def _read_offsets(data: bytes, table_pos: int, n_slots: int) -> list[int]:
    if n_slots > MAX_SLOTS:
        raise ChunkFormatError(f"n_slots = {n_slots} exceeds the cap of {MAX_SLOTS}")
    limit = len(data)
    table_end = table_pos + 4 * n_slots
    if table_pos < HEADER_SIZE or table_end > limit:
        raise ChunkFormatError(
            f"slot table [{table_pos:#x}, {table_end:#x}) outside the {limit:#x} bytes"
        )
    offsets = list(struct.unpack_from(f"<{n_slots}I", data, table_pos))
    for index, offset in enumerate(offsets):
        if offset and not table_end <= offset < limit:
            raise ChunkFormatError(
                f"slot {index}: offset {offset:#x} outside [{table_end:#x}, {limit:#x})"
            )
    return offsets


def _slots_from_offsets(offsets: list[int], limit: int) -> tuple[ChunkSlot, ...]:
    """Each occupied slot extends to the next larger offset; the last one, to `limit`."""
    used = sorted({offset for offset in offsets if offset})
    if not used:  # chunk with no occupied slots (HITMDL of a scenery without collision)
        return ()
    ends = dict(zip(used, [*used[1:], limit], strict=True))
    return tuple(
        ChunkSlot(index, offset, ends[offset]) for index, offset in enumerate(offsets) if offset
    )


def parse_chunk(data: bytes) -> TeamNinjaChunk:
    """Parse and validate the chunk header that starts at `data[0]`."""
    if len(data) < HEADER_SIZE:
        raise ChunkFormatError(f"{len(data)} bytes: the {HEADER_SIZE:#x}-byte header does not fit")
    raw_tag, size, n_slots, n_used, table_pos, header_size = _HEADER.unpack_from(data)
    tag = _decode_tag(raw_tag)
    if header_size != HEADER_SIZE:
        raise ChunkFormatError(f"field 0x1C = {header_size:#x}, expected {HEADER_SIZE:#x}")
    offsets = _read_offsets(data, table_pos, n_slots)
    if (found := sum(1 for offset in offsets if offset)) != n_used:
        raise ChunkFormatError(f"n_used = {n_used} but the table has {found} occupied slots")
    if len(set(filter(None, offsets))) != n_used:
        raise ChunkFormatError("two slots share an offset")
    slots = _slots_from_offsets(offsets, len(data))
    return TeamNinjaChunk(tag, size, n_slots, n_used, table_pos, slots)


def find_xpr0_slot(data: bytes, chunk: TeamNinjaChunk | None = None) -> ChunkSlot:
    """The only slot of the chunk whose payload starts with the `XPR0` magic."""
    parsed = parse_chunk(data) if chunk is None else chunk
    matches = [
        slot
        for slot in parsed.slots
        if data[slot.offset : slot.offset + len(XPR0_MAGIC)] == XPR0_MAGIC
    ]
    if not matches:
        raise SlotNotFoundError(f"chunk '{parsed.tag}' has no XPR0 slot")
    if len(matches) > 1:
        indices = ", ".join(str(slot.index) for slot in matches)
        raise SlotNotFoundError(f"ambiguous XPR0: {len(matches)} slots ({indices})")
    return matches[0]
