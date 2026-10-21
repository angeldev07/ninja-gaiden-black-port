"""Exportación a glTF 2.0 (`.gltf` JSON + `.bin`, o un `.glb` autocontenido) con la stdlib.

- **Nodos:** uno por objeto exportado y por cada antepasado suyo en HIERARC (aunque no se
  exporte su malla), con su traslación local de OBJINFO y su escala si no es 1. Una escala 0
  (efectos de `ryu.kmd`, la cara de `do_face.kmd`) parece "oculto por defecto" (INFERRED):
  no se escribe y el nodo lleva `extras.hidden_by_default`. Las piezas con peso de mezcla van
  en su padre sin traslación propia (ver `ngb_tools.kmd.model`).
- **Mallas:** una por objeto y una primitiva TRIANGLES por submalla: las tiras se
  convierten con `ngb_tools.kmd.topology`, que deja los triángulos CCW. Los atributos
  (POSITION, NORMAL, TEXCOORD_0, COLOR_0) se comparten entre las primitivas del objeto. Las
  UV van tal cual: glTF, como D3D, pone el origen arriba a la izquierda.
- **Ejes:** sin cambios. El modelo mira a +Z con Y arriba, como pide glTF. La mano derecha
  es INFERRED: el cull CW de las submallas solo cuadra con las normales guardadas si la
  vista es de mano derecha.
- **Materiales:** PBR sin metal, con `baseColorTexture` = la textura de la primera etapa,
  que es un PNG exportado por `ngb_tools.xpr` y se referencia con ruta relativa. `alphaMode`
  es BLEND si la submalla activa el alpha blend (+0x28, INFERRED). Si no, es OPAQUE, aunque
  la textura sea DXT5: en `ryu.kmd` el alfa de las DXT5 opacas no es transparencia. El
  blend aditivo (destino ONE) se aproxima con BLEND. `doubleSided` si la submalla no hace
  cull. El D3DMATERIAL8 y las etapas extra van en `extras`.
- **Skin (`skin=True`, opcional):** las piezas XYZB1 pasan a espacio del modelo y se
  enganchan a una única skin con JOINTS_0 = (abuelo, padre) y WEIGHTS_0 = (w, 1-w),
  semántica INFERRED. Las matrices inversas de bind son traslaciones a -mundo(joint). Sin
  skin, cada pieza va rígida en su nodo padre, igual que en el spike.

- **Animaciones (`animations`, opcional):** clips de `ngb_tools.kmd.animation` (los genera
  `ngb_tools.kmd.rig` a partir de las motions). Cada canal da un sampler LINEAR con su
  traslación o rotación por instante, y todos los samplers de un clip comparten la misma
  entrada de tiempos.

Grupos: por defecto solo detalle (1) y skin (4). LOD, sombra y efecto (`groups=None`)
salen como objetos aparte que se solapan con el cuerpo.
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
    """Qué exportar: grupos de OBJINFO (`None` = todos), si se crea la skin y los clips."""

    groups: frozenset[int] | None = DEFAULT_GROUPS
    skin: bool = False
    animations: tuple[AnimationClip, ...] = ()


@dataclass(frozen=True, slots=True)
class ExportStats:
    """Recuento de lo exportado y avisos (submallas saltadas, texturas sin PNG)."""

    objects: int
    vertices: int
    triangles: int
    primitives: int
    materials: int
    warnings: tuple[str, ...]
    animations: int = 0


@dataclass(frozen=True, slots=True)
class MaterialKey:
    """Lo que distingue un material exportado."""

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
    """Clave del material; una textura sin PNG cuenta como sin textura."""
    return MaterialKey(texture if texture in files else -1, blend, double_sided)


def texture_uri(texture: Path, base: Path) -> str:
    """Ruta relativa (URI) de `texture` vista desde la carpeta `base`, o `file:` si no hay."""
    try:
        relative = texture.resolve().relative_to(base.resolve(), walk_up=True)
    except ValueError:
        return texture.resolve().as_uri()
    return quote(relative.as_posix())


class _BufferBuilder:
    """Un único buffer binario con un bufferView por accessor, alineado a 4 bytes."""

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
    """El valor tal como queda guardado en float32 (para que min/max cuadren al byte)."""
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
        self.material_index: dict[MaterialKey, int] = {}
        self.textures: list[Json] = []
        self.texture_index: dict[int, int] = {}
        self.joint_slot: dict[int, int] = {}
        self.animation_count = 0
        self.warnings: list[str] = []
        self.vertices = self.triangles = self.primitives = 0

    def _texture(self, ordinal: int) -> int:
        if ordinal not in self.texture_index:
            self.texture_index[ordinal] = len(self.textures)
            self.textures.append({"sampler": 0, "source": len(self.textures)})
        return self.texture_index[ordinal]

    def _material(self, obj: int, position: int) -> int:
        sub = self.model.objects[obj].mesh.submeshes[position]
        texture = sub.base_texture
        if texture >= 0 and texture not in self.files:
            self.warnings.append(f"obj{obj} submalla {sub.index}: textura {texture} sin PNG")
        key = material_key(texture, sub.alpha_blend, sub.double_sided, self.files)
        if key in self.material_index:
            return self.material_index[key]
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
        material: Json = {"name": key.name, "pbrMetallicRoughness": pbr, "extras": extras}
        if key.blend:
            material["alphaMode"] = "BLEND"
        if key.double_sided:
            material["doubleSided"] = True
        self.material_index[key] = len(self.materials)
        self.materials.append(material)
        return self.material_index[key]

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
        if joints is None:  # build() solo crea skin para piezas con padre
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
                self.warnings.append(f"obj{obj} submalla {sub.index}: saltada (ver incidencias)")
                continue
            triangles = model.triangles(obj, position)
            if not triangles:
                self.warnings.append(f"obj{obj} submalla {sub.index}: sin triángulos")
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
            self.warnings.append(f"obj{obj}: FVF no admitido, sin malla")
            return None
        data = model.vertices(obj)
        offset = model.world_translation(obj) if skinned else (0.0, 0.0, 0.0)
        attributes = self._attributes(obj, data, offset, skinned)
        primitives = self._primitives(obj, attributes, len(data))
        if not primitives:
            return None
        self.vertices += len(data)
        self.primitives += len(primitives)
        self.meshes.append({"name": f"obj{obj:02d}", "primitives": primitives})
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
        animations = [self._animation(clip, node_of) for clip in options.animations]
        if animations:
            gltf["animations"] = animations
        self._finish(gltf)
        return gltf

    def _animation(self, clip: AnimationClip, node_of: Mapping[int, int]) -> Json:
        """Un clip: un sampler LINEAR por canal, todos con la misma entrada de tiempos."""
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
                self.warnings.append(f"{clip.name}: canal de obj{channel.obj} sin nodo, saltado")
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
            "name": f"obj{obj:02d}_{group_name(item.group)}",
            "extras": {"kmd_object": obj, "group": item.group, "fvf": f"{item.mesh.fvf:#05x}"},
        }
        translation = model.local_translation(obj)
        if any(translation):
            node["translation"] = list(translation)
        scale = item.info.scale
        if scale == (0.0, 0.0, 0.0):
            # Escala 0 = oculto por defecto (INFERRED): se exporta visible y se anota.
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
            by_slot = sorted(self.texture_index.items(), key=lambda item: item[1])
            gltf["images"] = [
                {"uri": texture_uri(self.files[ordinal], self.base), "name": f"tex{ordinal:02d}"}
                for ordinal, _ in by_slot
            ]
        if self.buffer.accessors:
            gltf["accessors"] = self.buffer.accessors
            gltf["bufferViews"] = self.buffer.views

    def image_files(self) -> list[Path]:
        """PNG de cada imagen, en el orden de `images`."""
        by_slot = sorted(self.texture_index.items(), key=lambda item: item[1])
        return [self.files[ordinal] for ordinal, _ in by_slot]

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
    """Documento glTF, contenido del `.bin` y recuento, sin escribir nada."""
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
    """Escribe `<stem>.bin` y después `<stem>.gltf`, atómicos y fuera de los repos."""
    out = ensure_outside_repos(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    gltf_path, bin_path = out / f"{stem}.gltf", out / f"{stem}.bin"
    gltf, data, stats = build_gltf(model, texture_files, out, options, bin_path.name)
    atomic_write_bytes(bin_path, data)
    text = json.dumps(gltf, indent=1, ensure_ascii=False) + "\n"
    # El .gltf da por buena la exportación: va el último y con fsync.
    atomic_write_bytes(gltf_path, text.encode("utf-8"), fsync=True)
    return (gltf_path, bin_path), stats


GLB_MAGIC: Final = b"glTF"
GLB_VERSION: Final = 2
GLB_JSON: Final = 0x4E4F534A
GLB_BIN: Final = 0x004E4942


def pack_glb(gltf: Json, data: bytes) -> bytes:
    """Contenedor GLB: cabecera de 12 bytes, chunk JSON (relleno con espacios) y chunk BIN."""
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
    """GLB autocontenido: el buffer y los PNG van incrustados (bufferView + mimeType)."""
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
    """Escribe `<stem>.glb` (atómico, con fsync) fuera de los repos."""
    out = ensure_outside_repos(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    glb_path = out / f"{stem}.glb"
    blob, stats = build_glb(model, texture_files, options)
    atomic_write_bytes(glb_path, blob, fsync=True)
    return glb_path, stats
