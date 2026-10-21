"""Modelos KMD de Team Ninja: parseo estricto y exportación a glTF 2.0 u OBJ (hito H3).

Un KMD es un chunk de Team Ninja con un XPR0 (texturas y buffers de GPU) y subchunks
propios. Módulos, de abajo arriba:

- `objinfo`: OBJINFO, transformación local, volúmenes y grupo de cada objeto.
- `hierarchy`: HIERARC, el árbol de objetos (padre, hijos y raíz).
- `mesh`: descriptor de malla (USERDATA del XPR0): FVF, submallas, material y etapas.
- `vertices`: decodificación de los VB según el FVF de D3D8.
- `pushbuffer`: los push buffers NV2A, que solo pueden ser una tira de triángulos.
- `topology`: de tiras a triángulos CCW.
- `model`: el KMD entero, emparejamiento de objetos y buffers, invariantes y colocación.
- `textures`: los PNG de `ngb_tools.xpr` que usan los materiales.
- `gltf`, `obj`: exportadores.
- `cli`: `python -m ngb_tools.kmd {info,export}`.

Formato verificado en `ryu.kmd` y `r_ryu.kmd`. Lo INFERRED está marcado en cada módulo.
"""

from __future__ import annotations

from ngb_tools.kmd.errors import (
    KmdError,
    KmdFormatError,
    PushBufferError,
    UnsupportedVertexFormatError,
)
from ngb_tools.kmd.gltf import ExportOptions, build_gltf, write_gltf
from ngb_tools.kmd.hierarchy import Hierarchy, HierarchyNode, parse_hierarchy
from ngb_tools.kmd.mesh import MeshDescriptor, Submesh, parse_mesh_descriptor
from ngb_tools.kmd.model import Issue, KmdModel, KmdObject, parse_kmd
from ngb_tools.kmd.obj import build_obj, write_obj
from ngb_tools.kmd.objinfo import ObjectGroup, ObjectInfo, parse_objinfo
from ngb_tools.kmd.pushbuffer import decode_push_buffer
from ngb_tools.kmd.topology import strip_to_triangles
from ngb_tools.kmd.vertices import VertexData, VertexLayout, decode_vertices, vertex_layout

__all__ = [
    "ExportOptions",
    "Hierarchy",
    "HierarchyNode",
    "Issue",
    "KmdError",
    "KmdFormatError",
    "KmdModel",
    "KmdObject",
    "MeshDescriptor",
    "ObjectGroup",
    "ObjectInfo",
    "PushBufferError",
    "Submesh",
    "UnsupportedVertexFormatError",
    "VertexData",
    "VertexLayout",
    "build_gltf",
    "build_obj",
    "decode_push_buffer",
    "decode_vertices",
    "parse_hierarchy",
    "parse_kmd",
    "parse_mesh_descriptor",
    "parse_objinfo",
    "strip_to_triangles",
    "vertex_layout",
    "write_gltf",
    "write_obj",
]
