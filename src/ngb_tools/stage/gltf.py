"""Una zona entera en una sola escena glTF 2.0: `.glb` (un archivo) o `.gltf` + `.bin`.

- **Nodos:** la raíz es la zona; cada parte (`boff_01`) es un hijo, y cada objeto de la parte
  un nieto con su malla. Los vértices de un escenario ya están en coordenadas de mundo
  (`ngb_tools.stage.model`), así que los nodos no llevan transformación salvo una T de
  OBJINFO distinta de 0 (props con HIERARC). S = (0, 0, 0) **no** es "oculto" en un
  escenario: se ignora y se anota en `extras.kmd_scale`; una S distinta de 0 y de 1 sí se
  escribe.
- **Mallas:** una por objeto, con una primitiva TRIANGLES por submalla. Cada parte del
  objeto tiene sus atributos (POSITION, NORMAL, TEXCOORD_0, COLOR_0) y sus primitivas los
  comparten. Las tiras se convierten con `ngb_tools.kmd.topology` (CCW). COLOR_0 es el
  D3DCOLOR del vértice (en los escenarios, la luz precalculada).
- **Materiales:** uno por (parte, textura de la primera etapa, blend, doble cara), como en
  `ngb_tools.kmd.gltf`: PBR sin metal con `baseColorTexture`, BLEND si la submalla activa
  el alpha blend y `doubleSided` si no hace cull. Las texturas son los PNG de
  `ngb_tools.stage.textures`: dentro del `.glb` (bufferView `image/png`) o por URI relativa
  en el `.gltf`.
- **Colisión (`collision=True`):** un nodo `<parte>_collision` por parte con toda su HITMDL
  en una primitiva, con un material magenta translúcido (BLEND, alfa 0.35, doble cara) y
  `extras.collision` y `extras.hidden_by_default` (glTF no tiene "oculto").
- **Ejes:** sin cambios, como `ngb_tools.kmd.gltf` (Y arriba).

TODO(kmd): el constructor de buffers y la conversión D3DCOLOR son los de
`ngb_tools.kmd.gltf`, que son privados.
"""

from __future__ import annotations

import json
import struct
from collections.abc import Iterable, Mapping, Sequence
from dataclasses import dataclass
from enum import StrEnum
from pathlib import Path
from typing import Any, Final
from urllib.parse import quote

from ngb_tools._paths import atomic_write_bytes, ensure_outside_repos
from ngb_tools.kmd.vertices import VertexData
from ngb_tools.stage.collision import CollisionMesh
from ngb_tools.stage.model import StageModel, StageObject, SubmeshRef

GENERATOR: Final = "ngb_tools.stage"
ARRAY_BUFFER: Final = 34962
ELEMENT_ARRAY_BUFFER: Final = 34963
FLOAT: Final = 5126
UNSIGNED_BYTE: Final = 5121
UNSIGNED_SHORT: Final = 5123
UNSIGNED_INT: Final = 5125
LINEAR: Final = 9729
REPEAT: Final = 10497
MAX_U16_INDEX: Final = 0xFFFF
GLB_MAGIC: Final = b"glTF"
GLB_VERSION: Final = 2
GLB_JSON: Final = 0x4E4F534A
GLB_BIN: Final = 0x004E4942
COLLISION_COLOR: Final = (1.0, 0.0, 1.0, 0.35)

type Json = dict[str, Any]
type Vec3 = tuple[float, float, float]


class SceneFormat(StrEnum):
    GLB = "glb"
    GLTF = "gltf"


@dataclass(frozen=True, slots=True)
class ScenePart:
    """Una parte ya parseada, con el nombre de su nodo y los PNG de sus texturas."""

    name: str
    model: StageModel
    textures: Mapping[int, Path]
    extras: Mapping[str, Any]


@dataclass(frozen=True, slots=True)
class SceneStats:
    parts: int
    objects: int
    vertices: int
    triangles: int
    primitives: int
    materials: int
    collision_triangles: int
    warnings: tuple[str, ...]


@dataclass(frozen=True, slots=True)
class _MaterialKey:
    part: str
    texture: int
    blend: bool
    double_sided: bool

    @property
    def name(self) -> str:
        base = f"{self.part}_tex{self.texture:03d}" if self.texture >= 0 else f"{self.part}_notex"
        return base + ("_blend" if self.blend else "") + ("_2s" if self.double_sided else "")


def _floats(values: Iterable[float]) -> bytes:
    data = list(values)
    return struct.pack(f"<{len(data)}f", *data)


def _f32(value: float) -> float:
    """El valor tal como queda en float32 (para que min/max cuadren al byte)."""
    return float(struct.unpack("<f", struct.pack("<f", value))[0])


def _bounds(points: Sequence[Vec3]) -> tuple[list[float], list[float]]:
    return (
        [_f32(min(p[i] for p in points)) for i in range(3)],
        [_f32(max(p[i] for p in points)) for i in range(3)],
    )


def _rgba(colors: Iterable[int]) -> bytes:
    """D3DCOLOR (u32 ARGB) a bytes RGBA."""
    out = bytearray()
    for c in colors:
        out += bytes(((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, (c >> 24) & 0xFF))
    return bytes(out)


def image_uri(path: Path, base: Path) -> str:
    """URI relativa de `path` vista desde la carpeta `base`, o `file:` si no hay camino."""
    try:
        relative = path.resolve().relative_to(base.resolve(), walk_up=True)
    except ValueError:
        return path.resolve().as_uri()
    return quote(relative.as_posix())


class _Buffer:
    """Un único buffer binario con un bufferView por accessor (o imagen), alineado a 4."""

    def __init__(self) -> None:
        self.data = bytearray()
        self.views: list[Json] = []
        self.accessors: list[Json] = []

    def view(self, payload: bytes, target: int | None = None) -> int:
        self.data += bytes(-len(self.data) % 4)
        view: Json = {"buffer": 0, "byteOffset": len(self.data), "byteLength": len(payload)}
        if target is not None:
            view["target"] = target
        self.data += payload
        self.views.append(view)
        return len(self.views) - 1

    def accessor(
        self,
        payload: bytes,
        *,
        component: int,
        count: int,
        kind: str,
        target: int,
        normalized: bool = False,
        bounds: tuple[list[float], list[float]] | None = None,
    ) -> int:
        accessor: Json = {
            "bufferView": self.view(payload, target),
            "componentType": component,
            "count": count,
            "type": kind,
        }
        if normalized:
            accessor["normalized"] = True
        if bounds is not None:
            accessor["min"], accessor["max"] = bounds
        self.accessors.append(accessor)
        return len(self.accessors) - 1

    def indices(self, flat: Sequence[int], vertex_count: int) -> int:
        wide = vertex_count > MAX_U16_INDEX
        payload = struct.pack(f"<{len(flat)}{'I' if wide else 'H'}", *flat)
        return self.accessor(
            payload,
            component=UNSIGNED_INT if wide else UNSIGNED_SHORT,
            count=len(flat),
            kind="SCALAR",
            target=ELEMENT_ARRAY_BUFFER,
        )


class _SceneWriter:
    def __init__(self, embed_images: bool, base: Path) -> None:
        self.embed_images = embed_images
        self.base = base
        self.buffer = _Buffer()
        self.nodes: list[Json] = []
        self.meshes: list[Json] = []
        self.materials: list[Json] = []
        self.material_index: dict[_MaterialKey, int] = {}
        self.images: list[Json] = []
        self.textures: list[Json] = []
        self.texture_index: dict[tuple[str, int], int] = {}
        self.collision_material: int | None = None
        self.warnings: list[str] = []
        self.objects = self.vertices = self.triangles = self.primitives = 0
        self.collision_triangles = 0

    def _texture(self, part: ScenePart, ordinal: int) -> int:
        key = (part.name, ordinal)
        if key not in self.texture_index:
            path = part.textures[ordinal]
            image: Json = {"name": f"{part.name}_tex{ordinal:03d}"}
            if self.embed_images:
                image["bufferView"] = self.buffer.view(path.read_bytes())
                image["mimeType"] = "image/png"
            else:
                image["uri"] = image_uri(path, self.base)
            self.images.append(image)
            self.texture_index[key] = len(self.textures)
            self.textures.append({"sampler": 0, "source": len(self.images) - 1})
        return self.texture_index[key]

    def _material(self, part: ScenePart, ref: SubmeshRef) -> int:
        sub = part.model.submesh(ref)
        texture = sub.base_texture
        if texture >= 0 and texture not in part.textures:
            self.warnings.append(f"{part.name} obj{ref[0]}: textura {texture} sin PNG")
            texture = -1
        key = _MaterialKey(part.name, texture, sub.alpha_blend, sub.double_sided)
        if key in self.material_index:
            return self.material_index[key]
        pbr: Json = {"metallicFactor": 0.0, "roughnessFactor": 1.0}
        if texture >= 0:
            pbr["baseColorTexture"] = {"index": self._texture(part, texture)}
        material: Json = {
            "name": key.name,
            "pbrMetallicRoughness": pbr,
            "extras": {
                "kmd_texture": sub.base_texture,
                "stages": [[s.name, s.texture] for s in sub.stages],
                "blend_factors": list(sub.blend_factors),
            },
        }
        if key.blend:
            material["alphaMode"] = "BLEND"
        if key.double_sided:
            material["doubleSided"] = True
        self.material_index[key] = len(self.materials)
        self.materials.append(material)
        return self.material_index[key]

    def _attributes(self, data: VertexData) -> Json:
        buffer, count = self.buffer, len(data)
        attributes: Json = {
            "POSITION": buffer.accessor(
                _floats(v for p in data.positions for v in p),
                component=FLOAT,
                count=count,
                kind="VEC3",
                target=ARRAY_BUFFER,
                bounds=_bounds(data.positions),
            )
        }
        if data.normals is not None:
            attributes["NORMAL"] = buffer.accessor(
                _floats(v for n in data.normals for v in n),
                component=FLOAT,
                count=count,
                kind="VEC3",
                target=ARRAY_BUFFER,
            )
        if data.uvs is not None:
            attributes["TEXCOORD_0"] = buffer.accessor(
                _floats(v for uv in data.uvs for v in uv),
                component=FLOAT,
                count=count,
                kind="VEC2",
                target=ARRAY_BUFFER,
            )
        if data.colors is not None:
            attributes["COLOR_0"] = buffer.accessor(
                _rgba(data.colors),
                component=UNSIGNED_BYTE,
                count=count,
                kind="VEC4",
                target=ARRAY_BUFFER,
                normalized=True,
            )
        return attributes

    def _mesh(self, part: ScenePart, obj: StageObject) -> int | None:
        model = part.model
        attributes: dict[int, Json] = {}
        primitives: list[Json] = []
        for ref, sub in obj.submeshes():
            if not model.exportable(ref):
                self.warnings.append(f"{part.name} obj{ref[0]} submalla {sub.index}: saltada")
                continue
            triangles = model.triangles(ref)
            if not triangles:
                continue
            p = ref[1]
            if p not in attributes:
                data = model.vertices(obj.index, p)
                attributes[p] = self._attributes(data)
                self.vertices += len(data)
            count = obj.parts[p].mesh.vertex_count
            flat = [i for t in triangles for i in t]
            primitives.append(
                {
                    "attributes": attributes[p],
                    "indices": self.buffer.indices(flat, count),
                    "material": self._material(part, ref),
                    "extras": {"kmd_part": p, "kmd_submesh": sub.index},
                }
            )
            self.triangles += len(triangles)
        if not primitives:
            return None
        self.primitives += len(primitives)
        self.meshes.append({"name": f"{part.name}_obj{obj.index:03d}", "primitives": primitives})
        return len(self.meshes) - 1

    def _object_node(self, part: ScenePart, obj: StageObject) -> int | None:
        mesh = self._mesh(part, obj)
        if mesh is None:
            return None
        info = obj.info
        node: Json = {
            "name": f"{part.name}_obj{obj.index:03d}_g{obj.group}",
            "mesh": mesh,
            "extras": {
                "kmd_object": obj.index,
                "group": obj.group,
                "flags": f"{info.flags:#x}",
                "fvf": [f"{item.mesh.fvf:#05x}" for item in obj.parts],
            },
        }
        translation = part.model.world_translation(obj.index)
        if any(translation):
            node["translation"] = list(translation)
        if info.scale == (0.0, 0.0, 0.0):
            node["extras"]["kmd_scale"] = [0.0, 0.0, 0.0]
        elif info.scale != (1.0, 1.0, 1.0):
            node["scale"] = list(info.scale)
        self.objects += 1
        self.nodes.append(node)
        return len(self.nodes) - 1

    def _collision_node(self, part: ScenePart) -> int | None:
        meshes: list[CollisionMesh] = [
            part.model.collision[k] for k in sorted(part.model.collision)
        ]
        positions: list[Vec3] = []
        flat: list[int] = []
        for mesh in meshes:
            base = len(positions)
            positions += mesh.vertices
            flat += [base + i for tri in mesh.triangles for i in tri]
        if not flat:
            return None
        if self.collision_material is None:
            self.collision_material = len(self.materials)
            self.materials.append(
                {
                    "name": "collision",
                    "pbrMetallicRoughness": {
                        "baseColorFactor": list(COLLISION_COLOR),
                        "metallicFactor": 0.0,
                        "roughnessFactor": 1.0,
                    },
                    "alphaMode": "BLEND",
                    "doubleSided": True,
                }
            )
        position = self.buffer.accessor(
            _floats(v for p in positions for v in p),
            component=FLOAT,
            count=len(positions),
            kind="VEC3",
            target=ARRAY_BUFFER,
            bounds=_bounds(positions),
        )
        primitive = {
            "attributes": {"POSITION": position},
            "indices": self.buffer.indices(flat, len(positions)),
            "material": self.collision_material,
        }
        self.meshes.append({"name": f"{part.name}_collision", "primitives": [primitive]})
        self.collision_triangles += len(flat) // 3
        self.nodes.append(
            {
                "name": f"{part.name}_collision",
                "mesh": len(self.meshes) - 1,
                "extras": {"collision": True, "hidden_by_default": True, "objects": len(meshes)},
            }
        )
        return len(self.nodes) - 1

    def build(self, zone: str, parts: Sequence[ScenePart], collision: bool) -> Json:
        part_nodes: list[int] = []
        for part in parts:
            children = [
                n for obj in part.model.objects if (n := self._object_node(part, obj)) is not None
            ]
            if collision and (n := self._collision_node(part)) is not None:
                children.append(n)
            node: Json = {"name": part.name, "extras": dict(part.extras)}
            if children:
                node["children"] = children
            part_nodes.append(len(self.nodes))
            self.nodes.append(node)
        root = len(self.nodes)
        self.nodes.append({"name": zone, "children": part_nodes, "extras": {"zone": zone}})
        gltf: Json = {
            "asset": {"version": "2.0", "generator": GENERATOR},
            "scene": 0,
            "scenes": [{"name": zone, "nodes": [root]}],
            "nodes": self.nodes,
        }
        for key, items in (
            ("meshes", self.meshes),
            ("materials", self.materials),
            ("textures", self.textures),
            ("images", self.images),
        ):
            if items:
                gltf[key] = items
        if self.textures:
            gltf["samplers"] = [
                {"magFilter": LINEAR, "minFilter": LINEAR, "wrapS": REPEAT, "wrapT": REPEAT}
            ]
        if self.buffer.accessors:
            gltf["accessors"] = self.buffer.accessors
        if self.buffer.views:
            gltf["bufferViews"] = self.buffer.views
        return gltf

    def stats(self, parts: int) -> SceneStats:
        return SceneStats(
            parts=parts,
            objects=self.objects,
            vertices=self.vertices,
            triangles=self.triangles,
            primitives=self.primitives,
            materials=len(self.materials),
            collision_triangles=self.collision_triangles,
            warnings=tuple(self.warnings),
        )


def build_scene(
    zone: str,
    parts: Sequence[ScenePart],
    *,
    collision: bool = False,
    embed_images: bool = True,
    base: Path = Path(),
) -> tuple[Json, bytes, SceneStats]:
    """Documento glTF (sin `buffers`), contenido del buffer y recuento, sin escribir nada.

    `base` es la carpeta del `.gltf`: las URI de las imágenes (sin `embed_images`) son
    relativas a ella.
    """
    writer = _SceneWriter(embed_images, base)
    gltf = writer.build(zone, parts, collision)
    return gltf, bytes(writer.buffer.data), writer.stats(len(parts))


def glb_bytes(gltf: Json, data: bytes) -> bytes:
    """Contenedor GLB: cabecera, chunk JSON (relleno con espacios) y chunk BIN (con ceros)."""
    document = dict(gltf)
    document["buffers"] = [{"byteLength": len(data)}]
    text = json.dumps(document, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    text += b" " * (-len(text) % 4)
    binary = data + bytes(-len(data) % 4)
    total = 12 + 8 + len(text) + 8 + len(binary)
    return (
        struct.pack("<4sII", GLB_MAGIC, GLB_VERSION, total)
        + struct.pack("<II", len(text), GLB_JSON)
        + text
        + struct.pack("<II", len(binary), GLB_BIN)
        + binary
    )


def write_scene(
    zone: str,
    parts: Sequence[ScenePart],
    out_dir: Path,
    *,
    fmt: SceneFormat = SceneFormat.GLB,
    collision: bool = False,
) -> tuple[tuple[Path, ...], SceneStats]:
    """Escribe `<zona>.glb`, o `<zona>.bin` y después `<zona>.gltf`; atómicos y fuera de repos."""
    out = ensure_outside_repos(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    embed = fmt is SceneFormat.GLB
    gltf, data, stats = build_scene(zone, parts, collision=collision, embed_images=embed, base=out)
    if embed:
        glb_path = out / f"{zone}.glb"
        atomic_write_bytes(glb_path, glb_bytes(gltf, data), fsync=True)
        return (glb_path,), stats
    gltf_path, bin_path = out / f"{zone}.gltf", out / f"{zone}.bin"
    gltf["buffers"] = [{"uri": quote(bin_path.name), "byteLength": len(data)}]
    atomic_write_bytes(bin_path, data)
    text = json.dumps(gltf, indent=1, ensure_ascii=False) + "\n"
    # El .gltf da por buena la exportación: va el último y con fsync.
    atomic_write_bytes(gltf_path, text.encode("utf-8"), fsync=True)
    return (gltf_path, bin_path), stats
