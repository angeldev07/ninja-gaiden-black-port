"""A whole zone in a single glTF 2.0 scene: `.glb` (one file) or `.gltf` + `.bin`.

- **Nodes:** the root is the zone; each part (`boff_01`) is a child, and each object of the
  part a grandchild with its mesh. The vertices of a stage are already in world coordinates
  (`ngb_tools.stage.model`), so nodes carry no transform except a non-zero OBJINFO T (props
  with HIERARC). S = (0, 0, 0) is **not** "hidden" in a stage: it is ignored and noted in
  `extras.kmd_scale`; an S other than 0 and 1 is written.
- **Meshes:** one per object, with one TRIANGLES primitive per submesh. Each part of the
  object has its own attributes (POSITION, NORMAL, TEXCOORD_0, COLOR_0) and its primitives
  share them. Strips are converted with `ngb_tools.kmd.topology` (CCW). COLOR_0 is the
  vertex D3DCOLOR (in stages, the baked light).
- **Materials:** one per (part, first-stage texture, blend, double sided), as in
  `ngb_tools.kmd.gltf`: PBR without metal with `baseColorTexture`, BLEND if the submesh
  enables alpha blend and `doubleSided` if it does no cull. The textures are the PNGs from
  `ngb_tools.stage.textures`: inside the `.glb` (`image/png` bufferView) or by relative URI
  in the `.gltf`.
- **Collision (`collision=True`):** a `<part>_collision` node per part with its whole HITMDL
  in one primitive, with a translucent magenta material (BLEND, alpha 0.35, double sided)
  and `extras.collision` and `extras.hidden_by_default` (glTF has no "hidden").
- **Axes:** unchanged, like `ngb_tools.kmd.gltf` (Y up).

TODO(kmd): the buffer builder and the D3DCOLOR conversion are those of
`ngb_tools.kmd.gltf`, which are private.
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
    """An already-parsed part, with its node name and the PNGs of its textures."""

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
    """The value as stored in float32 (so min/max match to the byte)."""
    return float(struct.unpack("<f", struct.pack("<f", value))[0])


def _bounds(points: Sequence[Vec3]) -> tuple[list[float], list[float]]:
    return (
        [_f32(min(p[i] for p in points)) for i in range(3)],
        [_f32(max(p[i] for p in points)) for i in range(3)],
    )


def _rgba(colors: Iterable[int]) -> bytes:
    """D3DCOLOR (u32 ARGB) to RGBA bytes."""
    out = bytearray()
    for c in colors:
        out += bytes(((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, (c >> 24) & 0xFF))
    return bytes(out)


def image_uri(path: Path, base: Path) -> str:
    """Relative URI of `path` seen from the `base` folder, or `file:` if there is no path."""
    try:
        relative = path.resolve().relative_to(base.resolve(), walk_up=True)
    except ValueError:
        return path.resolve().as_uri()
    return quote(relative.as_posix())


class _Buffer:
    """A single binary buffer with one bufferView per accessor (or image), 4-byte aligned."""

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
            self.warnings.append(f"{part.name} obj{ref[0]}: texture {texture} without PNG")
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
                self.warnings.append(f"{part.name} obj{ref[0]} submesh {sub.index}: skipped")
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
    """glTF document (without `buffers`), buffer contents and counts, without writing anything.

    `base` is the folder of the `.gltf`: the image URIs (without `embed_images`) are relative
    to it.
    """
    writer = _SceneWriter(embed_images, base)
    gltf = writer.build(zone, parts, collision)
    return gltf, bytes(writer.buffer.data), writer.stats(len(parts))


def glb_bytes(gltf: Json, data: bytes) -> bytes:
    """GLB container: header, JSON chunk (space-padded) and BIN chunk (zero-padded)."""
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
    """Write `<zone>.glb`, or `<zone>.bin` and then `<zone>.gltf`; atomic and outside repos."""
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
    # The .gltf marks the export as good: it goes last and with fsync.
    atomic_write_bytes(gltf_path, text.encode("utf-8"), fsync=True)
    return (gltf_path, bin_path), stats
