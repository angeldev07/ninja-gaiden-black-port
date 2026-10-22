"""Team Ninja KMD models: strict parsing and export to glTF 2.0 or OBJ (milestone H3).

A KMD is a Team Ninja chunk holding an XPR0 (textures and GPU buffers) plus its own
subchunks. Modules, bottom-up:

- `objinfo`: OBJINFO, the local transform, volumes and group of each object.
- `hierarchy`: HIERARC, the object tree (parent, children and root).
- `mesh`: mesh descriptor (XPR0 USERDATA): FVF, submeshes, material and stages.
- `vertices`: VB decoding according to the D3D8 FVF.
- `pushbuffer`: the NV2A push buffers, which can only be a triangle strip.
- `topology`: from strips to CCW triangles.
- `model`: the whole KMD, object/buffer pairing, invariants and placement.
- `textures`: the `ngb_tools.xpr` PNGs that materials use.
- `motion`: `.mtn`/`.mot` motion banks (tracks with 20-bit keys, Hermite).
- `rig`: humanoid rig (trunk FK + limb IK) and animation clips.
- `animation`: per-node TRS clips for glTF.
- `attach`: weapon anchors from `.itm` files and models mounted on a bone.
- `gltf`, `obj`: exporters (`gltf` also writes GLB).
- `cli`: `python -m ngb_tools.kmd {info,export}`.

Format verified on `ryu.kmd` and `r_ryu.kmd`. Anything INFERRED is marked in each module.
"""

from __future__ import annotations

from ngb_tools.kmd.animation import AnimationClip, NodeChannel
from ngb_tools.kmd.attach import (
    Attachment,
    ItemAnchor,
    ItemFormatError,
    find_anchor,
    parse_item_anchors,
)
from ngb_tools.kmd.errors import (
    KmdError,
    KmdFormatError,
    PushBufferError,
    UnsupportedVertexFormatError,
)
from ngb_tools.kmd.gltf import ExportOptions, build_glb, build_gltf, write_glb, write_gltf
from ngb_tools.kmd.hierarchy import Hierarchy, HierarchyNode, parse_hierarchy
from ngb_tools.kmd.mesh import MeshDescriptor, Submesh, parse_mesh_descriptor
from ngb_tools.kmd.model import Issue, KmdModel, KmdObject, parse_kmd
from ngb_tools.kmd.motion import Motion, MotionBank, MotionFormatError, parse_motion_bank
from ngb_tools.kmd.obj import build_obj, write_obj
from ngb_tools.kmd.objinfo import ObjectGroup, ObjectInfo, parse_objinfo
from ngb_tools.kmd.pushbuffer import decode_push_buffer
from ngb_tools.kmd.rig import HumanoidRig, RigError, find_rig, motion_clip
from ngb_tools.kmd.topology import strip_to_triangles
from ngb_tools.kmd.vertices import VertexData, VertexLayout, decode_vertices, vertex_layout

__all__ = [
    "AnimationClip",
    "Attachment",
    "ExportOptions",
    "Hierarchy",
    "HierarchyNode",
    "HumanoidRig",
    "Issue",
    "ItemAnchor",
    "ItemFormatError",
    "KmdError",
    "KmdFormatError",
    "KmdModel",
    "KmdObject",
    "MeshDescriptor",
    "Motion",
    "MotionBank",
    "MotionFormatError",
    "NodeChannel",
    "ObjectGroup",
    "ObjectInfo",
    "PushBufferError",
    "RigError",
    "Submesh",
    "UnsupportedVertexFormatError",
    "VertexData",
    "VertexLayout",
    "build_glb",
    "build_gltf",
    "build_obj",
    "decode_push_buffer",
    "decode_vertices",
    "find_anchor",
    "find_rig",
    "motion_clip",
    "parse_hierarchy",
    "parse_item_anchors",
    "parse_kmd",
    "parse_mesh_descriptor",
    "parse_motion_bank",
    "parse_objinfo",
    "strip_to_triangles",
    "vertex_layout",
    "write_glb",
    "write_gltf",
    "write_obj",
]
