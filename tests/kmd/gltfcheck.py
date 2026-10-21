"""Validador estructural de glTF 2.0, propio y sencillo, solo para los tests.

No sustituye al validador oficial de Khronos: comprueba lo que un exportador puede romper
sin darse cuenta. Lo que revisa:

- índices válidos en las referencias entre objetos (escenas, nodos, mallas, accessors...);
- rangos de bufferViews y accessors dentro del buffer;
- alineación de los accessors a su tamaño de componente;
- que POSITION tenga min/max y coincidan con los datos;
- conteos iguales en los atributos de una primitiva;
- índices de triángulos múltiplos de 3 y menores que el número de vértices;
- un árbol de nodos sin ciclos ni hijos con dos padres;
- skins con una matriz por joint.

`read_accessor` decodifica un accessor y lo reutiliza el render de validación.
"""

from __future__ import annotations

import json
import struct
from collections.abc import Sequence
from pathlib import Path
from typing import Any
from urllib.parse import unquote

COMPONENTS = {5120: "b", 5121: "B", 5122: "h", 5123: "H", 5125: "I", 5126: "f"}
WIDTHS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT2": 4, "MAT3": 9, "MAT4": 16}
TRIANGLES = 4


def load_gltf(path: Path) -> tuple[dict[str, Any], bytes]:
    """El JSON y el contenido del único buffer (externo, por URI relativa)."""
    gltf = json.loads(path.read_text(encoding="utf-8"))
    uri = gltf["buffers"][0]["uri"]
    return gltf, (path.parent / unquote(uri)).read_bytes()


def read_accessor(gltf: dict[str, Any], data: bytes, index: int) -> list[tuple[float, ...]]:
    accessor = gltf["accessors"][index]
    view = gltf["bufferViews"][accessor["bufferView"]]
    fmt = COMPONENTS[accessor["componentType"]]
    width = WIDTHS[accessor["type"]]
    item = struct.calcsize("<" + fmt) * width
    stride = view.get("byteStride", item)
    start = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    unpack = struct.Struct(f"<{width}{fmt}").unpack_from
    return [unpack(data, start + i * stride) for i in range(accessor["count"])]


def _check_ref(errors: list[str], where: str, value: Any, items: Sequence[Any]) -> bool:
    if not isinstance(value, int) or not 0 <= value < len(items):
        errors.append(f"{where}: referencia {value!r} fuera de [0, {len(items)})")
        return False
    return True


def _check_views(gltf: dict[str, Any], data: bytes, errors: list[str]) -> None:
    buffers = gltf.get("buffers", [])
    if len(buffers) != 1 or buffers[0].get("byteLength") != len(data):
        errors.append(f"buffer: byteLength {buffers[0].get('byteLength')} != {len(data)} bytes")
    for i, view in enumerate(gltf.get("bufferViews", [])):
        _check_ref(errors, f"bufferViews[{i}].buffer", view.get("buffer"), buffers)
        end = view.get("byteOffset", 0) + view["byteLength"]
        if end > len(data):
            errors.append(f"bufferViews[{i}]: acaba en {end} > {len(data)}")


def _check_accessors(gltf: dict[str, Any], data: bytes, errors: list[str]) -> None:
    views = gltf.get("bufferViews", [])
    for i, accessor in enumerate(gltf.get("accessors", [])):
        if not _check_ref(errors, f"accessors[{i}].bufferView", accessor.get("bufferView"), views):
            continue
        fmt = COMPONENTS.get(accessor.get("componentType", -1))
        width = WIDTHS.get(accessor.get("type", ""))
        if fmt is None or width is None or accessor.get("count", 0) < 1:
            errors.append(f"accessors[{i}]: tipo, componente o count inválidos")
            continue
        size = struct.calcsize("<" + fmt)
        view = views[accessor["bufferView"]]
        offset = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
        if offset % size:
            errors.append(f"accessors[{i}]: offset {offset} no alineado a {size}")
        need = accessor.get("byteOffset", 0) + size * width * accessor["count"]
        if need > view["byteLength"]:
            errors.append(f"accessors[{i}]: {need} bytes > bufferView de {view['byteLength']}")


def _check_position(gltf: dict[str, Any], data: bytes, index: int, errors: list[str]) -> None:
    accessor = gltf["accessors"][index]
    if "min" not in accessor or "max" not in accessor:
        errors.append(f"accessors[{index}]: POSITION sin min/max")
        return
    values = read_accessor(gltf, data, index)
    lo = [min(v[k] for v in values) for k in range(3)]
    hi = [max(v[k] for v in values) for k in range(3)]
    if lo != accessor["min"] or hi != accessor["max"]:
        errors.append(
            f"accessors[{index}]: min/max {accessor['min']}/{accessor['max']} != {lo}/{hi}"
        )


def _check_meshes(gltf: dict[str, Any], data: bytes, errors: list[str]) -> None:
    accessors = gltf.get("accessors", [])
    materials = gltf.get("materials", [])
    for m, mesh in enumerate(gltf.get("meshes", [])):
        for p, prim in enumerate(mesh["primitives"]):
            where = f"meshes[{m}].primitives[{p}]"
            attributes = prim.get("attributes", {})
            if "POSITION" not in attributes:
                errors.append(f"{where}: sin POSITION")
                continue
            if not all(_check_ref(errors, where, a, accessors) for a in attributes.values()):
                continue
            counts = {accessors[a]["count"] for a in attributes.values()}
            if len(counts) != 1:
                errors.append(f"{where}: atributos con conteos distintos {counts}")
            _check_position(gltf, data, attributes["POSITION"], errors)
            if "material" in prim:
                _check_ref(errors, f"{where}.material", prim["material"], materials)
            if prim.get("mode", TRIANGLES) != TRIANGLES or "indices" not in prim:
                errors.append(f"{where}: se esperaban TRIANGLES indexados")
                continue
            if not _check_ref(errors, f"{where}.indices", prim["indices"], accessors):
                continue
            indices = [v[0] for v in read_accessor(gltf, data, prim["indices"])]
            vertex_count = accessors[attributes["POSITION"]]["count"]
            if len(indices) % 3 or (indices and max(indices) >= vertex_count):
                errors.append(f"{where}: índices inválidos para {vertex_count} vértices")


def _check_nodes(gltf: dict[str, Any], errors: list[str]) -> None:
    nodes = gltf.get("nodes", [])
    parent: dict[int, int] = {}
    for i, node in enumerate(nodes):
        if "mesh" in node:
            _check_ref(errors, f"nodes[{i}].mesh", node["mesh"], gltf.get("meshes", []))
        if "skin" in node:
            _check_ref(errors, f"nodes[{i}].skin", node["skin"], gltf.get("skins", []))
        for child in node.get("children", []):
            if not _check_ref(errors, f"nodes[{i}].children", child, nodes):
                continue
            if child in parent:
                errors.append(f"nodes[{child}]: dos padres ({parent[child]} y {i})")
            parent[child] = i
    for start in range(len(nodes)):
        seen, node = set(), start
        while node in parent:
            if node in seen:
                errors.append(f"nodes[{start}]: ciclo en la jerarquía")
                break
            seen.add(node)
            node = parent[node]
    for scene in gltf.get("scenes", []):
        for root in scene.get("nodes", []):
            if _check_ref(errors, "scene.nodes", root, nodes) and root in parent:
                errors.append(f"scene: el nodo {root} no es raíz")


def _check_materials(gltf: dict[str, Any], base: Path | None, errors: list[str]) -> None:
    textures, images = gltf.get("textures", []), gltf.get("images", [])
    for i, material in enumerate(gltf.get("materials", [])):
        info = material.get("pbrMetallicRoughness", {}).get("baseColorTexture")
        if info is not None:
            _check_ref(errors, f"materials[{i}].baseColorTexture", info.get("index"), textures)
    for i, texture in enumerate(textures):
        _check_ref(errors, f"textures[{i}].source", texture.get("source"), images)
        if "sampler" in texture:
            _check_ref(
                errors, f"textures[{i}].sampler", texture["sampler"], gltf.get("samplers", [])
            )
    if base is not None:
        for i, image in enumerate(images):
            if not (base / unquote(image["uri"])).is_file():
                errors.append(f"images[{i}]: no existe {image['uri']}")


def _check_skins(gltf: dict[str, Any], errors: list[str]) -> None:
    for i, skin in enumerate(gltf.get("skins", [])):
        for joint in skin["joints"]:
            _check_ref(errors, f"skins[{i}].joints", joint, gltf.get("nodes", []))
        ibm = skin.get("inverseBindMatrices")
        if ibm is not None and _check_ref(
            errors, f"skins[{i}].ibm", ibm, gltf.get("accessors", [])
        ):
            accessor = gltf["accessors"][ibm]
            if accessor["type"] != "MAT4" or accessor["count"] != len(skin["joints"]):
                errors.append(f"skins[{i}]: inverseBindMatrices no cuadra con los joints")


def validate(gltf: dict[str, Any], data: bytes, base: Path | None = None) -> list[str]:
    """Lista de errores estructurales (vacía si el documento es coherente)."""
    errors: list[str] = []
    if gltf.get("asset", {}).get("version") != "2.0":
        errors.append("asset.version no es 2.0")
    _check_views(gltf, data, errors)
    _check_accessors(gltf, data, errors)
    if errors:
        return errors
    _check_meshes(gltf, data, errors)
    _check_nodes(gltf, errors)
    _check_materials(gltf, base, errors)
    _check_skins(gltf, errors)
    return errors
