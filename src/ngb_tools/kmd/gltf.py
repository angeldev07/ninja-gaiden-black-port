"""glTF 2.0 export (`.gltf` JSON + `.bin`, or a self-contained `.glb`) with the stdlib.

- **Nodes:** one per exported object and one per ancestor of it in HIERARC (even if its mesh
  is not exported), with its local OBJINFO translation and its scale if not 1. A scale of 0
  (effects in `ryu.kmd`, the face in `do_face.kmd`) looks like "hidden by default"
  (INFERRED): it is not written and the node carries `extras.hidden_by_default`. Pieces with
  a blend weight go under their parent without their own translation (see
  `ngb_tools.kmd.model`).
- **Meshes:** one per object and one TRIANGLES primitive per submesh: strips are converted
  with `ngb_tools.kmd.topology`, which leaves the triangles CCW. The attributes (POSITION,
  NORMAL, TEXCOORD_0, COLOR_0) are shared among the primitives of the object. UVs go as they
  are: glTF, like D3D, puts the origin at the top left.
- **Axes:** unchanged. The model faces +Z with Y up, as glTF requires. The right-handedness
  is INFERRED: the CW cull of the submeshes only matches the stored normals if the view is
  right-handed.
- **Materials:** PBR without metal, with `baseColorTexture` = the first-stage texture, which
  is a PNG exported by `ngb_tools.xpr` and referenced by relative path. `alphaMode` is BLEND
  if the submesh enables alpha blend (+0x28, INFERRED). Otherwise it is OPAQUE, even if the
  texture is DXT5: in `ryu.kmd` the alpha of opaque DXT5 textures is not transparency.
  Additive blend (destination ONE) is approximated with BLEND. `doubleSided` if the submesh
  does no cull. The D3DMATERIAL8 and the extra stages go in `extras`.
- **Skin (`skin=True`, optional):** XYZB1 pieces move to model space and are bound to a
  single skin with JOINTS_0 = (grandparent, parent) and WEIGHTS_0 = (w, 1-w), semantics
  INFERRED. The inverse bind matrices are translations by -world(joint). Without skin, each
  piece is rigid under its parent node, as in the spike.

- **Mounted models (`attachments`, optional):** each one is an anchor node with its local
  translation and rotation, a child of the bone node (`ngb_tools.kmd.attach`), so it
  inherits the animation. Inside goes the weapon model with its hierarchy. Its meshes,
  materials and images carry the `<weapon>_` prefix and its nodes, `extras.attachment`.
- **Animations (`animations`, optional):** clips from `ngb_tools.kmd.animation` (generated
  by `ngb_tools.kmd.rig` from the motions). Each channel yields a LINEAR sampler with its
  translation or rotation per instant, and all the samplers of a clip share the same time
  input.

Groups: by default only detail (1) and skin (4). LOD, shadow and effect (`groups=None`) come
out as separate objects that overlap the body.
"""

from __future__ import annotations

import json
import struct
from collections.abc import Iterable, Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Final
from urllib.parse import quote

from ngb_tools._paths import atomic_write_bytes, ensure_outside_repos
from ngb_tools.kmd.animation import ROTATION, AnimationClip
from ngb_tools.kmd.attach import Attachment
from ngb_tools.kmd.model import KmdModel
from ngb_tools.kmd.objinfo import ObjectGroup, Vec3, group_name
from ngb_tools.kmd.vertices import VertexData

GENERATOR: Final = "ngb_tools.kmd"
DEFAULT_GROUPS: Final = frozenset({ObjectGroup.DETAIL.value, ObjectGroup.SKIN.value})
ARRAY_BUFFER: Final = 34962
ELEMENT_ARRAY_BUFFER: Final = 34963
FLOAT: Final = 5126
UNSIGNED_BYTE: Final = 5121
UNSIGNED_SHORT: Final = 5123
UNSIGNED_INT: Final = 5125
LINEAR: Final = 9729
REPEAT: Final = 10497
MAX_U16_INDEX: Final = 0xFFFF

type Json = dict[str, Any]


@dataclass(frozen=True, slots=True)
class ExportOptions:
    """What to export: groups (`None` = all), skin, clips and mounted models (weapons)."""

    groups: frozenset[int] | None = DEFAULT_GROUPS
    skin: bool = False
    animations: tuple[AnimationClip, ...] = ()
    attachments: tuple[Attachment, ...] = ()


@dataclass(frozen=True, slots=True)
class ExportStats:
    """Counts of what was exported and warnings (skipped submeshes, textures without PNG)."""

    objects: int
    vertices: int
    triangles: int
    primitives: int
    materials: int
    warnings: tuple[str, ...]
    animations: int = 0


@dataclass(frozen=True, slots=True)
class MaterialKey:
    """What distinguishes an exported material."""

    texture: int
    blend: bool
    double_sided: bool

    @property
    def name(self) -> str:
        base = f"tex{self.texture:02d}" if self.texture >= 0 else "notex"
        return base + ("_blend" if self.blend else "") + ("_2s" if self.double_sided else "")


def selected_objects(model: KmdModel, groups: frozenset[int] | None) -> list[int]:
    return [o.index for o in model.objects if groups is None or o.group in groups]


def material_key(
    texture: int, blend: bool, double_sided: bool, files: Mapping[int, Path]
) -> MaterialKey:
    """Material key; a texture without a PNG counts as no texture."""
    return MaterialKey(texture if texture in files else -1, blend, double_sided)


def texture_uri(texture: Path, base: Path) -> str:
    """Relative path (URI) of `texture` seen from the `base` folder, or `file:` if none."""
    try:
        relative = texture.resolve().relative_to(base.resolve(), walk_up=True)
    except ValueError:
        return texture.resolve().as_uri()
    return quote(relative.as_posix())


class _BufferBuilder:
    """A single binary buffer with one bufferView per accessor, 4-byte aligned."""

    def __init__(self) -> None:
        self.data = bytearray()
        self.views: list[Json] = []
        self.accessors: list[Json] = []

    def add(
        self,
        payload: bytes,
        *,
        component: int,
        count: int,
        kind: str,
        target: int | None,
        normalized: bool = False,
        bounds: tuple[list[float], list[float]] | None = None,
    ) -> int:
        self.data += bytes(-len(self.data) % 4)
        view: Json = {"buffer": 0, "byteOffset": len(self.data), "byteLength": len(payload)}
        if target is not None:
            view["target"] = target
        self.data += payload
        self.views.append(view)
        accessor: Json = {
            "bufferView": len(self.views) - 1,
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


class _GltfWriter:
    def __init__(
        self, model: KmdModel, files: Mapping[int, Path], base: Path, options: ExportOptions
    ) -> None:
        self.model = model
        self.files = files
        self.base = base
        self.options = options
        self.buffer = _BufferBuilder()
        self.nodes: list[Json] = []
        self.meshes: list[Json] = []
        self.materials: list[Json] = []
        # Keys carry the model prefix: a mounted weapon has its own textures.
        self.tag = ""
        self.material_index: dict[tuple[str, MaterialKey], int] = {}
        self.textures: list[Json] = []
        self.texture_index: dict[tuple[str, int], int] = {}
        self.image_paths: list[Path] = []
        self.image_names: list[str] = []
        self.joint_slot: dict[int, int] = {}
        self.animation_count = 0
        self.warnings: list[str] = []
        self.vertices = self.triangles = self.primitives = 0

    def _texture(self, ordinal: int) -> int:
        key = (self.tag, ordinal)
        if key not in self.texture_index:
            self.texture_index[key] = len(self.textures)
            self.textures.append({"sampler": 0, "source": len(self.image_paths)})
            self.image_paths.append(self.files[ordinal])
            self.image_names.append(f"{self.tag}tex{ordinal:02d}")
        return self.texture_index[key]

    def _material(self, obj: int, position: int) -> int:
        sub = self.model.objects[obj].mesh.submeshes[position]
        texture = sub.base_texture
        if texture >= 0 and texture not in self.files:
            self.warnings.append(f"obj{obj} submesh {sub.index}: texture {texture} without PNG")
        key = material_key(texture, sub.alpha_blend, sub.double_sided, self.files)
        if (self.tag, key) in self.material_index:
            return self.material_index[(self.tag, key)]
        pbr: Json = {"metallicFactor": 0.0, "roughnessFactor": 1.0}
        if key.texture >= 0:
            pbr["baseColorTexture"] = {"index": self._texture(key.texture)}
        extras: Json = {
            "kmd_texture": texture,
            "stages": [[s.name, s.texture] for s in sub.stages],
            "blend_factors": list(sub.blend_factors),
        }
        if sub.material is not None:
            m = sub.material
            extras["d3d_material"] = {
                "diffuse": list(m.diffuse),
                "ambient": list(m.ambient),
                "specular": list(m.specular),
                "emissive": list(m.emissive),
                "power": m.power,
            }
        material: Json = {
            "name": self.tag + key.name,
            "pbrMetallicRoughness": pbr,
            "extras": extras,
        }
        if key.blend:
            material["alphaMode"] = "BLEND"
        if key.double_sided:
            material["doubleSided"] = True
        self.material_index[(self.tag, key)] = len(self.materials)
        self.materials.append(material)
        return self.material_index[(self.tag, key)]

    def _attributes(self, obj: int, data: VertexData, offset: Vec3, skinned: bool) -> Json:
        buffer = self.buffer
        count = len(data)
        positions = [(x + offset[0], y + offset[1], z + offset[2]) for x, y, z in data.positions]
        attributes: Json = {
            "POSITION": buffer.add(
                _floats(v for p in positions for v in p),
                component=FLOAT,
                count=count,
                kind="VEC3",
                target=ARRAY_BUFFER,
                bounds=_bounds(positions),
            )
        }
        if data.normals is not None:
            attributes["NORMAL"] = buffer.add(
                _floats(v for n in data.normals for v in n),
                component=FLOAT,
                count=count,
                kind="VEC3",
                target=ARRAY_BUFFER,
            )
        if data.uvs is not None:
            attributes["TEXCOORD_0"] = buffer.add(
                _floats(v for uv in data.uvs for v in uv),
                component=FLOAT,
                count=count,
                kind="VEC2",
                target=ARRAY_BUFFER,
            )
        if data.colors is not None:
            attributes["COLOR_0"] = buffer.add(
                _rgba(data.colors),
                component=UNSIGNED_BYTE,
                count=count,
                kind="VEC4",
                target=ARRAY_BUFFER,
                normalized=True,
            )
        if skinned and data.weights is not None:
            attributes.update(self._skin_attributes(obj, data.weights))
        return attributes

    def _skin_attributes(self, obj: int, weights: Sequence[float]) -> Json:
        joints = self.model.skin_joints(obj)
        if joints is None:  # build() only creates skin for pieces with a parent
            return {}
        a, b = (self.joint_slot[j] for j in joints)
        clamped = [min(1.0, max(0.0, w)) for w in weights]
        joint_data = struct.pack(f"<{4 * len(weights)}H", *([a, b, 0, 0] * len(weights)))
        weight_data = _floats(v for w in clamped for v in (w, 1.0 - w, 0.0, 0.0))
        return {
            "JOINTS_0": self.buffer.add(
                joint_data,
                component=UNSIGNED_SHORT,
                count=len(weights),
                kind="VEC4",
                target=ARRAY_BUFFER,
            ),
            "WEIGHTS_0": self.buffer.add(
                weight_data,
                component=FLOAT,
                count=len(weights),
                kind="VEC4",
                target=ARRAY_BUFFER,
            ),
        }

    def _primitives(self, obj: int, attributes: Json, vertex_count: int) -> list[Json]:
        model = self.model
        primitives: list[Json] = []
        wide = vertex_count > MAX_U16_INDEX
        for position, sub in enumerate(model.objects[obj].mesh.submeshes):
            if not model.exportable(obj, position):
                self.warnings.append(f"obj{obj} submesh {sub.index}: skipped (see issues)")
                continue
            triangles = model.triangles(obj, position)
            if not triangles:
                self.warnings.append(f"obj{obj} submesh {sub.index}: no triangles")
                continue
            flat = [i for t in triangles for i in t]
            payload = struct.pack(f"<{len(flat)}{'I' if wide else 'H'}", *flat)
            indices = self.buffer.add(
                payload,
                component=UNSIGNED_INT if wide else UNSIGNED_SHORT,
                count=len(flat),
                kind="SCALAR",
                target=ELEMENT_ARRAY_BUFFER,
            )
            primitives.append(
                {
                    "attributes": attributes,
                    "indices": indices,
                    "material": self._material(obj, position),
                    "extras": {"kmd_submesh": sub.index},
                }
            )
            self.triangles += len(triangles)
        return primitives

    def _mesh(self, obj: int, skinned: bool) -> int | None:
        model = self.model
        if model.objects[obj].layout is None:
            self.warnings.append(f"obj{obj}: unsupported FVF, no mesh")
            return None
        data = model.vertices(obj)
        offset = model.world_translation(obj) if skinned else (0.0, 0.0, 0.0)
        attributes = self._attributes(obj, data, offset, skinned)
        primitives = self._primitives(obj, attributes, len(data))
        if not primitives:
            return None
        self.vertices += len(data)
        self.primitives += len(primitives)
        self.meshes.append({"name": f"{self.tag}obj{obj:02d}", "primitives": primitives})
        return len(self.meshes) - 1

    def build(self) -> Json:
        model, options = self.model, self.options
        selected = selected_objects(model, options.groups)
        needed = set(selected)
        for obj in selected:
            needed.update(model.ancestors(obj))
        order = sorted(needed)
        node_of = {obj: i for i, obj in enumerate(order)}
        skinned = {
            obj
            for obj in selected
            if options.skin and model.objects[obj].skinned and model.skin_joints(obj) is not None
        }
        joints = sorted({j for obj in skinned for j in (model.skin_joints(obj) or ())})
        self.joint_slot = {j: i for i, j in enumerate(joints)}
        for obj in order:
            self.nodes.append(
                self._node(obj, node_of, needed, obj in selected and obj not in skinned)
            )
        roots = [node_of[o] for o in order if model.parent(o) not in needed]
        for obj in sorted(skinned):
            mesh = self._mesh(obj, skinned=True)
            if mesh is not None:
                roots.append(len(self.nodes))
                self.nodes.append({"name": f"obj{obj:02d}_skin", "mesh": mesh, "skin": 0})
        gltf: Json = {
            "asset": {"version": "2.0", "generator": GENERATOR},
            "scene": 0,
            "scenes": [{"nodes": roots}],
            "nodes": self.nodes,
        }
        if joints:
            gltf["skins"] = [self._skin(joints, node_of)]
        for attachment in options.attachments:
            self._attach(attachment, node_of)
        animations = [self._animation(clip, node_of) for clip in options.animations]
        if animations:
            gltf["animations"] = animations
        self._finish(gltf)
        return gltf

    def _attach(self, attachment: Attachment, node_of: Mapping[int, int]) -> None:
        """Anchor node, child of object `parent` (translation, rotation), with the weapon inside."""
        parent = node_of.get(attachment.parent)
        if parent is None:
            self.warnings.append(f"{attachment.name}: obj{attachment.parent} is not exported")
            return
        saved = (self.model, self.files, self.tag)
        self.model, self.files = attachment.model, attachment.texture_files
        self.tag = f"{attachment.name}_"
        try:
            model = attachment.model
            selected = selected_objects(model, attachment.groups)
            needed = set(selected)
            for obj in selected:
                needed.update(model.ancestors(obj))
            order = sorted(needed)
            anchor: Json = {
                "name": f"{attachment.name}_anchor",
                "extras": {"attachment": attachment.name, "parent_object": attachment.parent},
            }
            if any(attachment.translation):
                anchor["translation"] = list(attachment.translation)
            if attachment.rotation != (0.0, 0.0, 0.0, 1.0):
                anchor["rotation"] = list(attachment.rotation)
            anchor_index = len(self.nodes)
            self.nodes.append(anchor)
            sub_node_of = {obj: anchor_index + 1 + i for i, obj in enumerate(order)}
            for obj in order:
                node = self._node(obj, sub_node_of, needed, obj in selected)
                node["extras"]["attachment"] = attachment.name
                self.nodes.append(node)
            roots = [sub_node_of[o] for o in order if model.parent(o) not in needed]
            if roots:
                anchor["children"] = roots
            self.nodes[parent].setdefault("children", []).append(anchor_index)
        finally:
            self.model, self.files, self.tag = saved

    def _animation(self, clip: AnimationClip, node_of: Mapping[int, int]) -> Json:
        """One clip: a LINEAR sampler per channel, all sharing the same time input."""
        times = clip.times
        bounds = ([_f32(times[0])], [_f32(times[-1])]) if times else None
        source = self.buffer.add(
            _floats(times), component=FLOAT, count=len(times), kind="SCALAR", target=None,
            bounds=bounds,
        )  # fmt: skip
        samplers: list[Json] = []
        channels: list[Json] = []
        for channel in clip.channels:
            if channel.obj not in node_of or len(channel.values) != len(times):
                self.warnings.append(
                    f"{clip.name}: channel of obj{channel.obj} has no node, skipped"
                )
                continue
            kind = "VEC4" if channel.path == ROTATION else "VEC3"
            output = self.buffer.add(
                _floats(v for value in channel.values for v in value),
                component=FLOAT,
                count=len(channel.values),
                kind=kind,
                target=None,
            )
            samplers.append({"input": source, "output": output, "interpolation": "LINEAR"})
            target = {"node": node_of[channel.obj], "path": channel.path}
            channels.append({"sampler": len(samplers) - 1, "target": target})
        self.animation_count += 1
        return {
            "name": clip.name,
            "channels": channels,
            "samplers": samplers,
            "extras": dict(clip.extras),
        }

    def _node(
        self, obj: int, node_of: Mapping[int, int], needed: set[int], with_mesh: bool
    ) -> Json:
        model = self.model
        item = model.objects[obj]
        node: Json = {
            "name": f"{self.tag}obj{obj:02d}_{group_name(item.group)}",
            "extras": {"kmd_object": obj, "group": item.group, "fvf": f"{item.mesh.fvf:#05x}"},
        }
        translation = model.local_translation(obj)
        if any(translation):
            node["translation"] = list(translation)
        scale = item.info.scale
        if scale == (0.0, 0.0, 0.0):
            # Scale 0 = hidden by default (INFERRED): exported visible and annotated.
            node["extras"]["hidden_by_default"] = True
        elif scale != (1.0, 1.0, 1.0):
            node["scale"] = list(scale)
        children = [node_of[c] for c in model.children(obj) if c in needed]
        if children:
            node["children"] = children
        if with_mesh:
            mesh = self._mesh(obj, skinned=False)
            if mesh is not None:
                node["mesh"] = mesh
        return node

    def _skin(self, joints: Sequence[int], node_of: Mapping[int, int]) -> Json:
        matrices: list[float] = []
        for joint in joints:
            x, y, z = self.model.world_translation(joint)
            matrices += [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -x, -y, -z, 1]
        inverse = self.buffer.add(
            _floats(matrices), component=FLOAT, count=len(joints), kind="MAT4", target=None
        )
        return {"joints": [node_of[j] for j in joints], "inverseBindMatrices": inverse}

    def _finish(self, gltf: Json) -> None:
        if self.meshes:
            gltf["meshes"] = self.meshes
        if self.materials:
            gltf["materials"] = self.materials
        if self.textures:
            gltf["textures"] = self.textures
            gltf["samplers"] = [
                {"magFilter": LINEAR, "minFilter": LINEAR, "wrapS": REPEAT, "wrapT": REPEAT}
            ]
            gltf["images"] = [
                {"uri": texture_uri(path, self.base), "name": name}
                for path, name in zip(self.image_paths, self.image_names, strict=True)
            ]
        if self.buffer.accessors:
            gltf["accessors"] = self.buffer.accessors
            gltf["bufferViews"] = self.buffer.views

    def image_files(self) -> list[Path]:
        """PNG of each image, in the order of `images`."""
        return list(self.image_paths)

    def stats(self) -> ExportStats:
        objects = sum(1 for node in self.nodes if "mesh" in node)
        return ExportStats(
            objects=objects,
            vertices=self.vertices,
            triangles=self.triangles,
            primitives=self.primitives,
            materials=len(self.materials),
            warnings=tuple(self.warnings),
            animations=self.animation_count,
        )


def build_gltf(
    model: KmdModel,
    texture_files: Mapping[int, Path],
    base: Path,
    options: ExportOptions,
    bin_uri: str,
) -> tuple[Json, bytes, ExportStats]:
    """glTF document, `.bin` contents and counts, without writing anything."""
    writer = _GltfWriter(model, texture_files, base, options)
    gltf = writer.build()
    data = bytes(writer.buffer.data)
    gltf["buffers"] = [{"uri": quote(bin_uri), "byteLength": len(data)}]
    return gltf, data, writer.stats()


def write_gltf(
    model: KmdModel,
    out_dir: Path,
    stem: str,
    texture_files: Mapping[int, Path],
    options: ExportOptions,
) -> tuple[tuple[Path, Path], ExportStats]:
    """Write `<stem>.bin` and then `<stem>.gltf`, atomically and outside the repos."""
    out = ensure_outside_repos(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    gltf_path, bin_path = out / f"{stem}.gltf", out / f"{stem}.bin"
    gltf, data, stats = build_gltf(model, texture_files, out, options, bin_path.name)
    atomic_write_bytes(bin_path, data)
    text = json.dumps(gltf, indent=1, ensure_ascii=False) + "\n"
    # The .gltf marks the export as good: it goes last and with fsync.
    atomic_write_bytes(gltf_path, text.encode("utf-8"), fsync=True)
    return (gltf_path, bin_path), stats


GLB_MAGIC: Final = b"glTF"
GLB_VERSION: Final = 2
GLB_JSON: Final = 0x4E4F534A
GLB_BIN: Final = 0x004E4942


def pack_glb(gltf: Json, data: bytes) -> bytes:
    """GLB container: 12-byte header, JSON chunk (space-padded) and BIN chunk."""
    text = json.dumps(gltf, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
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


def build_glb(
    model: KmdModel, texture_files: Mapping[int, Path], options: ExportOptions
) -> tuple[bytes, ExportStats]:
    """Self-contained GLB: the buffer and the PNGs are embedded (bufferView + mimeType)."""
    writer = _GltfWriter(model, texture_files, Path.cwd(), options)
    gltf = writer.build()
    data = bytearray(writer.buffer.data)
    views = gltf.setdefault("bufferViews", [])
    for image, path in zip(gltf.get("images", []), writer.image_files(), strict=True):
        png = path.read_bytes()
        data += bytes(-len(data) % 4)
        views.append({"buffer": 0, "byteOffset": len(data), "byteLength": len(png)})
        data += png
        image.pop("uri", None)
        image["bufferView"] = len(views) - 1
        image["mimeType"] = "image/png"
    if not views:
        del gltf["bufferViews"]
    gltf["buffers"] = [{"byteLength": len(data)}]
    return pack_glb(gltf, bytes(data)), writer.stats()


def write_glb(
    model: KmdModel,
    out_dir: Path,
    stem: str,
    texture_files: Mapping[int, Path],
    options: ExportOptions,
) -> tuple[Path, ExportStats]:
    """Write `<stem>.glb` (atomic, with fsync) outside the repos."""
    out = ensure_outside_repos(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    glb_path = out / f"{stem}.glb"
    blob, stats = build_glb(model, texture_files, options)
    atomic_write_bytes(glb_path, blob, fsync=True)
    return glb_path, stats
