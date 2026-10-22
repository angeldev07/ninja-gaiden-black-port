"""Team Ninja stages: zone KMDs with several parts per object and collision (HITMDL).

Uses `ngb_tools.kmd` as a library (OBJINFO, HIERARC, multi-part descriptor, FVF, push
buffers, strips) and adds what only appears in stages. Modules, bottom-up:

- `collision`: HITMDL, the collision mesh (`VHD` records).
- `model`: the whole stage KMD, pairing of parts and buffers, invariants.
- `zone`: the `<zone>_NN` parts of the zone AFS files, without repeating identical variants.
- `textures`: the `ngb_tools.xpr` PNGs of each part (reused or exported).
- `gltf`: the zone as a single glTF scene (`.glb` or `.gltf` + `.bin`), part by part.
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
