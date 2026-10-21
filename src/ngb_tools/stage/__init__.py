"""Escenarios de Team Ninja: KMD de zona con varias partes por objeto y colisión (HITMDL).

Usa `ngb_tools.kmd` como librería (OBJINFO, HIERARC, descriptor multiparte, FVF, push
buffers, tiras) y añade lo que solo aparece en los escenarios. Módulos, de abajo arriba:

- `collision`: HITMDL, la malla de colisión (registros `VHD`).
- `model`: el KMD de escenario entero, emparejamiento de partes y buffers, invariantes.
- `zone`: las partes `<zona>_NN` de los AFS de la zona, sin repetir variantes idénticas.
- `textures`: los PNG de `ngb_tools.xpr` de cada parte (reutilizados o exportados).
- `gltf`: la zona en una sola escena glTF (`.glb` o `.gltf` + `.bin`), parte a parte.
- `cli`: `python -m ngb_tools.stage {info,export}`.
"""

from __future__ import annotations

from ngb_tools.stage.collision import CollisionMesh, parse_collision_record, parse_hitmdl
from ngb_tools.stage.errors import StageError, StageFormatError, ZoneNotFoundError
from ngb_tools.stage.model import (
    StageModel,
    StageObject,
    StagePart,
    SubmeshRef,
    parse_stage_kmd,
)

__all__ = [
    "CollisionMesh",
    "StageError",
    "StageFormatError",
    "StageModel",
    "StageObject",
    "StagePart",
    "SubmeshRef",
    "ZoneNotFoundError",
    "parse_collision_record",
    "parse_hitmdl",
    "parse_stage_kmd",
]
