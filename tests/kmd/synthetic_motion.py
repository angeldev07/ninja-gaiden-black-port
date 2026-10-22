"""Synthetic builders of motion banks and of a humanoid KMD: never game data.

They reproduce what was observed in `ryu.mtn`: id table terminated by 0, the data of each
track right before the header of its motion (offsets terminated by 0) and keys with a u16
control word (kind in bits 0-1, frames in bits 2-11 and the high bits of a 20-bit value in
bits 12-15).
"""

from __future__ import annotations

import struct
from collections.abc import Sequence

from teamninja.synthetic import build_chunk

from .synthetic import SynthObject

TRACKS = 44


def key(kind: int, frames: int, value: int, *slopes: int) -> bytes:
    raw = value & 0xFFFFF
    ctrl = kind | (frames << 2) | ((raw >> 16) << 12)
    return struct.pack("<HH", ctrl, raw & 0xFFFF) + struct.pack(f"<{len(slopes)}h", *slopes)


def constant(value: int, frames: int) -> bytes:
    """Constant track: one value key and the end key."""
    return key(1, frames, value) + key(0, 0, value)


def build_bank(motions: Sequence[Sequence[bytes]], table: Sequence[int]) -> bytes:
    """Bank body (like a `.mot`): `table[id]` = index of the motion for that id."""
    body = bytearray(4 * (len(table) + 1))
    body += bytes(-len(body) % 16)
    headers: list[int] = []
    for tracks in motions:
        offsets = []
        for track in tracks:
            offsets.append(len(body))
            body += track
        body += bytes(-len(body) % 4)
        headers.append(len(body))
        body += struct.pack(f"<{len(offsets)}I", *offsets) + bytes(4)
    for motion_id, motion in enumerate(table):
        struct.pack_into("<I", body, 4 * motion_id, headers[motion])
    return bytes(body)


def chr_dat(body: bytes) -> bytes:
    """The same bank wrapped in a 3-slot `chr_dat`, like a `.mtn`."""
    return build_chunk(b"chr_dat", [body, bytes(16), struct.pack("<4I", 8, 0, 0x100, 0)])


def humanoid() -> list[SynthObject]:
    """15 bones like Ryu: 0 hip, 1 chest, 2 head, arms 3-5 (-x) and 6-8 (+x), legs
    9-11 (-x) and 12-14 (+x). Arms of 0.25 + 0.25 m in T-pose and legs of 0.4 + 0.4 m."""
    t = [
        (0.0, 0.0, 0.0), (0.0, 0.2, 0.0), (0.0, 0.3, 0.0),
        (-0.2, 0.25, 0.0), (-0.25, 0.0, 0.0), (-0.25, 0.0, 0.0),
        (0.2, 0.25, 0.0), (0.25, 0.0, 0.0), (0.25, 0.0, 0.0),
        (-0.1, -0.05, 0.0), (0.0, -0.4, 0.0), (0.0, -0.4, 0.0),
        (0.1, -0.05, 0.0), (0.0, -0.4, 0.0), (0.0, -0.4, 0.0),
    ]  # fmt: skip
    children = {0: (1, 9, 12), 1: (2, 3, 6), 3: (4,), 4: (5,), 6: (7,), 7: (8,)}
    children |= {9: (10,), 10: (11,), 12: (13,), 13: (14,)}
    return [SynthObject(translation=t[k], children=children.get(k, ())) for k in range(15)]


def standing_values() -> list[int]:
    """Pose with hand-computed values (units of 1e-4).

    Hip at (0, 0.9, 0) with no rotations. Arms stretched in T-pose: the target is 0.5 m
    from the shoulder, exactly the reach. Legs with the ankle 0.48 m below the hip and
    H = ±90° (knee toward +z). By the law of cosines: cos a = 0.6 and sin a = 0.8, so the
    knee ends up at hip + (0, -0.24, 0.32).
    """
    v = [0] * TRACKS
    v[1] = 9000
    v[12:15] = [-7000, 13500, 0]  # -x arm: shoulder (-0.2, 1.35, 0) + 0.5 along -x
    v[20:23] = [7000, 13500, 0]
    v[28:31] = [-1000, 3700, 0]  # -x leg: hip (-0.1, 0.85, 0) - 0.48 in y
    v[35] = 15708  # H = +90°: the -x side rotates (-1, 0, 0) to +z
    v[36:39] = [1000, 3700, 0]
    v[43] = -15708
    return v


def standing_motion(frames: int = 2) -> list[bytes]:
    return [constant(value, frames) for value in standing_values()]
