"""Team Ninja motion banks (`.mtn` and `.mot`): strict parsing and track sampling.

A `.mtn` is a 3-slot `chr_dat` chunk (`ryu.mtn` in `cmn.afs` #22, `eryun.mtn` in
`boff_cmn.afs` #3). A `.mot` (`rm_kt.mot`, Ryu's weapons) is directly the body of slot 0,
without a chunk header. Only slot 0 is read; slot 1 (another offset table) and slot 2
(16 bytes) are left uninterpreted. Everything is little-endian u32/u16. Offsets are relative
to the body.

**Motion table.** One u32 per motion id up to a 0 (1890 ids in `ryu.mtn`, 1893 in
`rm_kt.mot`). Several ids share an offset: empty ones point to a 4-frame filler motion. The
ids look like a global space that the weapon banks fill in (INFERRED).

**Motion.** At its offset there is a u32 header terminated by 0, one entry per track: the
offset of each track. The track data sits right before the header, in order. Observed: 44
tracks in all 258 motions of `ryu.mtn` and in the 167 of `rm_kt.mot`, and all the tracks of
a motion last the same (VERIFIED: 0 discrepancies).

**Track.** A list of keys; each starts with a u16 control word:

    bits 0-1    kind: 1 = value; 2 = value + 1 tangent; 3 = value + 2 tangents; 0 = end
    bits 2-11   frames until the next key
    bits 12-15  bits 16-19 of the value (20-bit signed integer)

It is followed by the low u16 of the value and 0, 1 or 2 s16 tangents. The end key carries
only the value. 97 % of the keys are kind 1 at 1-frame distance: already-sampled animation.

**Interpolation.** Kind 1, linear. Kinds 2 and 3, cubic Hermite: key k stores the outgoing
tangent A and the incoming tangent B of segment k -> k+1 (kind 2: the same at both ends).
Evidence: the B of a segment matches the A of the next one (median 6 % relative error,
versus 26 % if they were per-segment tangents). Tangents are slopes per frame x 16
(INFERRED: the minimum-energy fit gives 17.9 and the per-segment median, 14.3). Units of the
values in `ngb_tools.kmd.rig`.
"""

from __future__ import annotations

import struct
from collections.abc import Mapping
from dataclasses import dataclass
from itertools import pairwise
from types import MappingProxyType
from typing import Final

from ngb_tools.kmd.errors import KmdError
from ngb_tools.teamninja import TeamNinjaError, parse_chunk

CHR_DAT_TAG: Final = b"chr_dat\0"
KIND_END: Final = 0
KIND_VALUE: Final = 1
KIND_ONE_SLOPE: Final = 2
KIND_TWO_SLOPES: Final = 3
SLOPE_SCALE: Final = 16.0
# Sanity caps (observed: 1893 ids and 44 tracks).
MAX_MOTION_IDS: Final = 65536
MAX_TRACKS: Final = 256


class MotionFormatError(KmdError):
    """The bytes do not form a readable motion bank."""


@dataclass(frozen=True, slots=True)
class Key:
    """A key: absolute frame, raw value and slopes of the segment that starts here."""

    frame: int
    value: int
    kind: int
    slope_out: int = 0
    slope_in_next: int = 0


@dataclass(frozen=True, slots=True)
class Track:
    keys: tuple[Key, ...]

    @property
    def frames(self) -> int:
        return self.keys[-1].frame

    def sample(self, frame: float) -> float:
        """Raw value at `frame` (clamped at the ends)."""
        keys = self.keys
        if frame <= keys[0].frame or len(keys) == 1:
            return float(keys[0].value)
        if frame >= keys[-1].frame:
            return float(keys[-1].value)
        lo, hi = 0, len(keys) - 1
        while hi - lo > 1:  # binary search for the segment
            mid = (lo + hi) // 2
            if keys[mid].frame <= frame:
                lo = mid
            else:
                hi = mid
        k0, k1 = keys[lo], keys[hi]
        span = k1.frame - k0.frame
        t = (frame - k0.frame) / span
        if k0.kind not in (KIND_ONE_SLOPE, KIND_TWO_SLOPES):
            return k0.value + (k1.value - k0.value) * t
        m0 = k0.slope_out * span / SLOPE_SCALE
        m1 = k0.slope_in_next * span / SLOPE_SCALE
        t2, t3 = t * t, t * t * t
        return (
            (2 * t3 - 3 * t2 + 1) * k0.value
            + (t3 - 2 * t2 + t) * m0
            + (-2 * t3 + 3 * t2) * k1.value
            + (t3 - t2) * m1
        )


@dataclass(frozen=True, slots=True)
class Motion:
    offset: int
    tracks: tuple[Track, ...]

    @property
    def frames(self) -> int:
        return self.tracks[0].frames if self.tracks else 0

    def sample(self, frame: float) -> tuple[float, ...]:
        return tuple(track.sample(frame) for track in self.tracks)


@dataclass(frozen=True, slots=True)
class MotionBank:
    """Id table (offset of each id) and motions by offset."""

    table: tuple[int, ...]
    motions: Mapping[int, Motion]

    def motion(self, motion_id: int) -> Motion:
        if not 0 <= motion_id < len(self.table):
            raise MotionFormatError(f"motion {motion_id} outside the {len(self.table)} ids")
        return self.motions[self.table[motion_id]]

    def distinct_ids(self) -> tuple[int, ...]:
        """The first id of each distinct motion, in id order."""
        seen: dict[int, int] = {}
        for motion_id, offset in enumerate(self.table):
            seen.setdefault(offset, motion_id)
        return tuple(sorted(seen.values()))


def _s16(value: int) -> int:
    return value - 0x10000 if value & 0x8000 else value


def _value20(ctrl: int, low: int) -> int:
    raw = ((ctrl >> 12) << 16) | low
    return raw - (1 << 20) if raw & 0x80000 else raw


def parse_track(body: bytes, start: int, end: int) -> Track:
    """Keys of the track that occupies `body[start:end]` (padding may follow the end key)."""
    keys: list[Key] = []
    pos, frame = start, 0
    while True:
        if pos + 4 > end:
            raise MotionFormatError(f"track at {start:#x}: no end key before {end:#x}")
        ctrl, low = struct.unpack_from("<HH", body, pos)
        kind = ctrl & 0x3
        size = 4 if kind in (KIND_END, KIND_VALUE) else 2 * (kind + 1)
        if pos + size > end:
            raise MotionFormatError(f"track at {start:#x}: key truncated at {pos:#x}")
        slopes = struct.unpack_from(f"<{(size - 4) // 2}H", body, pos + 4)
        first = _s16(slopes[0]) if slopes else 0
        second = _s16(slopes[1]) if len(slopes) > 1 else first
        keys.append(Key(frame, _value20(ctrl, low), kind, first, second))
        if kind == KIND_END:
            return Track(tuple(keys))
        frame += (ctrl >> 2) & 0x3FF
        pos += size


def _u32_run(body: bytes, start: int, limit: int, what: str) -> list[int]:
    """u32 values from `start` up to a 0 (which is not included)."""
    values: list[int] = []
    pos = start
    while True:
        if pos + 4 > len(body):
            raise MotionFormatError(f"{what} at {start:#x} without a 0 terminator")
        (value,) = struct.unpack_from("<I", body, pos)
        if value == 0:
            return values
        if len(values) == limit:
            raise MotionFormatError(f"{what} at {start:#x}: more than {limit} entries")
        values.append(value)
        pos += 4


def _parse_motion(body: bytes, offset: int, data_floor: int) -> Motion:
    track_offsets = _u32_run(body, offset, MAX_TRACKS, "motion header")
    bounds = [*track_offsets, offset]
    for a, b in pairwise(bounds):
        if not data_floor <= a < b:
            raise MotionFormatError(f"motion at {offset:#x}: track offsets out of order")
    tracks = tuple(parse_track(body, a, b) for a, b in pairwise(bounds))
    lengths = {track.frames for track in tracks}
    if len(lengths) > 1:
        raise MotionFormatError(f"motion at {offset:#x}: tracks of different length {lengths}")
    return Motion(offset, tracks)


def motion_body(data: bytes) -> bytes:
    """The bank body: slot 0 of a `chr_dat`, or the whole file (`.mot`)."""
    if not data.startswith(CHR_DAT_TAG):
        return data
    try:
        chunk = parse_chunk(data)
    except TeamNinjaError as error:
        raise MotionFormatError(f"chr_dat: {error}") from error
    first = min(chunk.slots, key=lambda slot: slot.index, default=None)
    if first is None or first.index != 0:
        raise MotionFormatError("chr_dat without slot 0")
    return data[first.offset : first.end]


def parse_motion_bank(data: bytes) -> MotionBank:
    """Parse a `.mtn` (chr_dat) or a `.mot` (bare body)."""
    body = motion_body(data)
    table = tuple(_u32_run(body, 0, MAX_MOTION_IDS, "motion table"))
    if not table:
        raise MotionFormatError("empty motion table")
    table_end = 4 * (len(table) + 1)
    motions: dict[int, Motion] = {}
    for offset in sorted(set(table)):
        if offset % 4 or not table_end <= offset < len(body):
            raise MotionFormatError(f"motion offset {offset:#x} outside the body or unaligned")
        motions[offset] = _parse_motion(body, offset, table_end)
    return MotionBank(table, MappingProxyType(motions))
