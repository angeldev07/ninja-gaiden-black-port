"""glTF-ready animation clips: per-node (KMD object) TRS samples.

This is the intermediate format between `ngb_tools.kmd.rig` (which resolves the motions) and
`ngb_tools.kmd.gltf` (which writes them as `animations`, with LINEAR interpolation). Each
channel is a glTF path (`translation` or `rotation`) of one object, with one value per instant
in `times`. Rotations are glTF quaternions (x, y, z, w).
"""

from __future__ import annotations

from collections.abc import Mapping
from dataclasses import dataclass, field
from typing import Final, Literal

TRANSLATION: Final = "translation"
ROTATION: Final = "rotation"

type ChannelPath = Literal["translation", "rotation"]


@dataclass(frozen=True, slots=True)
class NodeChannel:
    """Values of one node property: VEC3 for translation and VEC4 for rotation."""

    obj: int
    path: ChannelPath
    values: tuple[tuple[float, ...], ...]


@dataclass(frozen=True, slots=True)
class AnimationClip:
    """A clip: instants in seconds (increasing) and channels with one value per instant."""

    name: str
    times: tuple[float, ...]
    channels: tuple[NodeChannel, ...]
    extras: Mapping[str, str | int | float] = field(default_factory=dict)
