"""Clips de animación listos para el glTF: muestras TRS por nodo (objeto del KMD).

Es el formato intermedio entre `ngb_tools.kmd.rig` (que resuelve las motions) y
`ngb_tools.kmd.gltf` (que los escribe como `animations`, con interpolación LINEAR). Cada
canal es una ruta glTF (`translation` o `rotation`) de un objeto, con un valor por instante
de `times`. Las rotaciones son cuaterniones glTF (x, y, z, w).
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
    """Valores de una propiedad de un nodo: VEC3 para traslación y VEC4 para rotación."""

    obj: int
    path: ChannelPath
    values: tuple[tuple[float, ...], ...]


@dataclass(frozen=True, slots=True)
class AnimationClip:
    """Un clip: instantes en segundos (crecientes) y canales con un valor por instante."""

    name: str
    times: tuple[float, ...]
    channels: tuple[NodeChannel, ...]
    extras: Mapping[str, str | int | float] = field(default_factory=dict)
