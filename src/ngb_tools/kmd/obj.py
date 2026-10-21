"""Exportación a Wavefront OBJ + MTL: la alternativa sencilla al glTF, sin jerarquía.

Cada objeto es un `o` con sus vértices ya colocados en el modelo (traslación del mundo de
`ngb_tools.kmd.model`) y cada submalla, un `g` con su `usemtl`. OBJ pone el origen de las
UV abajo a la izquierda, así que `vt = (u, 1 - v)`. Las texturas trabajan con wrap: v en
[-1, 0] da vt en [1, 2], y los visores repiten. Los triángulos son los de
`ngb_tools.kmd.topology`, ya CCW. Del material solo va la textura de la primera etapa
(`map_Kd`, ruta relativa) y Ka/Ks/Ns del D3DMATERIAL8; los nombres de material son los
mismos que en el glTF.
"""

from __future__ import annotations

from collections.abc import Mapping
from dataclasses import dataclass
from pathlib import Path
from urllib.parse import unquote

from ngb_tools._paths import atomic_write_bytes, ensure_outside_repos
from ngb_tools.kmd.gltf import (
    ExportOptions,
    MaterialKey,
    material_key,
    selected_objects,
    texture_uri,
)
from ngb_tools.kmd.mesh import Material
from ngb_tools.kmd.model import KmdModel


@dataclass(frozen=True, slots=True)
class ObjStats:
    objects: int
    vertices: int
    triangles: int
    materials: int
    warnings: tuple[str, ...]


def _mtl(
    materials: Mapping[MaterialKey, Material | None], files: Mapping[int, Path], base: Path
) -> str:
    lines = ["# materiales de ngb_tools.kmd (solo la textura de la primera etapa)"]
    for key, material in sorted(materials.items(), key=lambda item: item[0].name):
        lines.append(f"newmtl {key.name}")
        if material is not None:
            a, s = material.ambient, material.specular
            lines += [
                f"Ka {a[0]:.3f} {a[1]:.3f} {a[2]:.3f}",
                "Kd 1.000 1.000 1.000",
                f"Ks {s[0]:.3f} {s[1]:.3f} {s[2]:.3f}",
                f"Ns {material.power:.1f}",
            ]
        if key.texture >= 0:
            lines.append(f"map_Kd {unquote(texture_uri(files[key.texture], base))}")
        lines.append("")
    return "\n".join(lines)


def build_obj(
    model: KmdModel, files: Mapping[int, Path], base: Path, options: ExportOptions, mtl_name: str
) -> tuple[str, str, ObjStats]:
    """Texto del OBJ y del MTL, sin escribir nada."""
    lines = ["# ngb_tools.kmd", f"mtllib {mtl_name}"]
    materials: dict[MaterialKey, Material | None] = {}
    warnings: list[str] = []
    base_index = 1
    objects = vertices = triangles = 0
    for obj in selected_objects(model, options.groups):
        item = model.objects[obj]
        if item.layout is None:
            warnings.append(f"obj{obj}: FVF no admitido, sin malla")
            continue
        data = model.vertices(obj)
        ox, oy, oz = model.world_translation(obj)
        lines.append(f"o obj{obj:02d}")
        lines += [f"v {x + ox:.6f} {y + oy:.6f} {z + oz:.6f}" for x, y, z in data.positions]
        if data.uvs is not None:
            lines += [f"vt {u:.6f} {1.0 - v:.6f}" for u, v in data.uvs]
        if data.normals is not None:
            lines += [f"vn {x:.6f} {y:.6f} {z:.6f}" for x, y, z in data.normals]
        for position, sub in enumerate(item.mesh.submeshes):
            if not model.exportable(obj, position):
                warnings.append(f"obj{obj} submalla {sub.index}: saltada (ver incidencias)")
                continue
            key = material_key(sub.base_texture, sub.alpha_blend, sub.double_sided, files)
            materials.setdefault(key, sub.material)
            lines += [f"g obj{obj:02d}_sub{sub.index}", f"usemtl {key.name}"]
            for tri in model.triangles(obj, position):
                refs = []
                for i in tri:
                    n = i + base_index
                    uv = str(n) if data.uvs is not None else ""
                    refs.append(
                        f"{n}/{uv}/{n}" if data.normals is not None else f"{n}/{uv}".rstrip("/")
                    )
                lines.append("f " + " ".join(refs))
                triangles += 1
        base_index += len(data)
        vertices += len(data)
        objects += 1
    stats = ObjStats(objects, vertices, triangles, len(materials), tuple(warnings))
    return "\n".join(lines) + "\n", _mtl(materials, files, base), stats


def write_obj(
    model: KmdModel,
    out_dir: Path,
    stem: str,
    texture_files: Mapping[int, Path],
    options: ExportOptions,
) -> tuple[tuple[Path, Path], ObjStats]:
    """Escribe `<stem>.mtl` y después `<stem>.obj`, atómicos y fuera de los repos."""
    out = ensure_outside_repos(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    obj_path, mtl_path = out / f"{stem}.obj", out / f"{stem}.mtl"
    obj_text, mtl_text, stats = build_obj(model, texture_files, out, options, mtl_path.name)
    atomic_write_bytes(mtl_path, mtl_text.encode("utf-8"))
    atomic_write_bytes(obj_path, obj_text.encode("utf-8"), fsync=True)
    return (obj_path, mtl_path), stats
