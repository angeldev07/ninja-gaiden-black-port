"""Un KMD entero: objetos, jerarquía, buffers de GPU y comprobación de invariantes.

Un KMD es un chunk de Team Ninja cuyos slots son subchunks (OBJINFO, HIERARC, MORPH...) y
un XPR0. Se buscan por su etiqueta o su magia, no por número de slot. El objeto k usa:

- la entrada k de OBJINFO (transformación local, grupo);
- el nodo k de HIERARC (padre e hijos); sin HIERARC (`do_face.kmd`) todos son raíces;
- el USERDATA k+1 del XPR0 (descriptor de malla: partes, FVF, submallas, materiales);
- un par VB/IB por parte, en el orden de las cabeceras del XPR0: cada VB con el IB que lo
  sigue, si lo hay (una parte cuyas submallas leen todas de push buffer no tiene IB,
  `do_face.kmd`). Casi todos los objetos tienen una parte; con varias (`doku.kmd` obj1,
  ver `ngb_tools.kmd.mesh`), el objeto k consume tantos pares como partes.

Las submallas con origen "push buffer" consumen los push buffers del XPR0 en orden: la
k-ésima submalla de ese tipo, contando todos los objetos y en el orden del descriptor, usa
el k-ésimo push buffer (verificado con 1 en `ryu.kmd` y 2 en `r_ryu.kmd`, que cubren justo
los vértices que el IB del objeto no toca, y con 1886 en los escenarios). `push_buffer_of`
va por (objeto, parte, posición en la parte).

**Varias partes, una malla.** Los exportadores ven un objeto como una malla con un único
juego de vértices, así que un objeto de varias partes se presenta unido: `vertices(obj)`
concatena los vértices de sus partes en orden y `submesh_indices` suma a cada índice el
primer vértice de su parte. Los atributos que una parte no trae se rellenan: UV (0, 0),
color blanco (neutro al multiplicar) y normales suavizadas a partir de sus triángulos
(INFERRED: aproximación, no dato del juego). Un objeto de varias partes va rígido: los
pesos de mezcla se descartan (no hay ninguno en el juego). Con una parte todo es como
siempre, byte a byte.

**Errores e incidencias.** Lo que impide emparejar objetos y buffers (subchunks ausentes,
recuentos distintos, descriptores ilegibles) lanza `KmdFormatError`. Lo demás se anota como
`Issue` y la exportación sigue sin la parte afectada: VB o IB más cortos de lo que declaran,
rangos de submalla fuera del IB o del push buffer, índices fuera de los vértices, texturas
inexistentes, FVF no admitido, rotaciones (se ignoran) o una jerarquía incoherente.

**Colocación.** No hay rotaciones (0 en todo lo observado) ni más escala que 1 o 0, así que
la posición de un objeto en el modelo es la suma de las traslaciones de OBJINFO a lo largo
de su cadena de padres. Las piezas con peso de mezcla (FVF XYZB1, grupo skin) se colocan
en su nodo padre ignorando su propia traslación: en `ryu.kmd` los codos llevan
T = -mundo(padre), y aplicarla los manda al origen (VERIFIED visualmente).
"""

from __future__ import annotations

import math
from collections.abc import Iterable, Mapping, Sequence
from dataclasses import dataclass, field, replace
from functools import cached_property
from typing import Final

from ngb_tools.kmd.errors import KmdFormatError, UnsupportedVertexFormatError
from ngb_tools.kmd.hierarchy import HIERARC_TAG, Hierarchy, parse_hierarchy
from ngb_tools.kmd.mesh import (
    IndexSource,
    MeshDescriptor,
    ModelHeader,
    MultipartDescriptor,
    parse_model_header,
    parse_multipart_descriptor,
)
from ngb_tools.kmd.objinfo import OBJINFO_TAG, ObjectInfo, Vec3, parse_objinfo
from ngb_tools.kmd.pushbuffer import decode_push_buffer
from ngb_tools.kmd.topology import Triangle, strip_to_triangles
from ngb_tools.kmd.vertices import VertexData, VertexLayout, decode_vertices, vertex_layout
from ngb_tools.teamninja import TeamNinjaChunk, TeamNinjaError, parse_chunk
from ngb_tools.teamninja.chunk import TAG_SIZE
from ngb_tools.xpr.errors import XprError
from ngb_tools.xpr.resources import (
    XPR_MAGIC,
    IndexBuffer,
    PushBuffer,
    Texture,
    UserData,
    VertexBuffer,
    XprFile,
    parse_xpr,
)

ORIGIN: Final[Vec3] = (0.0, 0.0, 0.0)
FILL_UV: Final = (0.0, 0.0)
FILL_COLOR: Final = 0xFFFFFFFF  # D3DCOLOR blanco opaco
FALLBACK_NORMAL: Final[Vec3] = (0.0, 1.0, 0.0)

type PartRef = tuple[int, int, int]  # (objeto, parte, posición de la submalla en la parte)


@dataclass(frozen=True, slots=True)
class Issue:
    """Invariante incumplida; `obj` es el objeto afectado, si lo hay."""

    obj: int | None
    message: str

    def __str__(self) -> str:
        return self.message if self.obj is None else f"obj{self.obj}: {self.message}"


@dataclass(frozen=True, slots=True)
class KmdPart:
    """Una parte de un objeto: su descriptor, su par VB/IB y su disposición de vértices.

    `base` es su primer vértice en `KmdModel.vertices(obj)` (0 en la primera parte).
    """

    part: int
    mesh: MeshDescriptor
    vertex_buffer: VertexBuffer
    index_buffer: IndexBuffer | None
    layout: VertexLayout | None
    base: int


@dataclass(frozen=True, slots=True)
class KmdObject:
    """Un objeto del modelo y sus recursos de GPU.

    Con una parte, `mesh`, `vertex_buffer`, `index_buffer` y `layout` son los suyos. Con
    varias, `mesh` es la vista unida (submallas en el orden del descriptor, vértices e
    índices sumados), `vertex_buffer` e `index_buffer` los de la parte 0 y `layout` el de la
    parte 0 sin pesos, o None si alguna parte tiene un FVF no admitido. `order[k]` es
    (parte, posición en la parte) de la submalla k.
    """

    index: int
    info: ObjectInfo
    mesh: MeshDescriptor
    vertex_buffer: VertexBuffer | None
    index_buffer: IndexBuffer | None
    layout: VertexLayout | None
    parts: tuple[KmdPart, ...] = ()
    order: tuple[tuple[int, int], ...] = ()

    @property
    def group(self) -> int:
        return self.info.group

    @property
    def skinned(self) -> bool:
        return self.layout is not None and self.layout.weight_count > 0


@dataclass(frozen=True, slots=True)
class SlotRef:
    index: int
    offset: int
    end: int


def _slot_tag(data: bytes, offset: int) -> str | None:
    raw = data[offset : offset + TAG_SIZE].rstrip(b"\0")
    if raw and all(0x20 <= b <= 0x7E for b in raw):
        return raw.decode("ascii")
    return None


def _find_slots(data: bytes, chunk: TeamNinjaChunk) -> dict[str, SlotRef]:
    """Slots por etiqueta de subchunk (o `XPR0` por su magia); una etiqueta repetida falla."""
    found: dict[str, SlotRef] = {}
    for slot in chunk.slots:
        if data[slot.offset : slot.offset + len(XPR_MAGIC)] == XPR_MAGIC:
            tag: str | None = XPR_MAGIC.decode("ascii")
        else:
            tag = _slot_tag(data, slot.offset)
        if tag is None:
            continue
        if tag in found:
            raise KmdFormatError(f"slot '{tag}' repetido ({found[tag].index} y {slot.index})")
        found[tag] = SlotRef(slot.index, slot.offset, slot.end)
    return found


def _require(slots: dict[str, SlotRef], tag: str) -> SlotRef:
    if tag not in slots:
        raise KmdFormatError(f"el KMD no tiene el slot {tag}")
    return slots[tag]


@dataclass(frozen=True)
class KmdModel:
    """Un KMD parseado. `issues` son las invariantes incumplidas (ver el docstring del módulo)."""

    tag: str
    header: ModelHeader
    xpr: XprFile
    xpr_offset: int
    xpr_slot: int
    objects: tuple[KmdObject, ...]
    hierarchy: Hierarchy | None
    textures: tuple[Texture, ...]
    push_buffers: tuple[tuple[int, ...], ...]
    push_buffer_of: Mapping[PartRef, int]
    issues: tuple[Issue, ...] = ()
    unusable: Mapping[int, frozenset[int]] = field(default_factory=dict)

    def exportable(self, obj: int, submesh: int) -> bool:
        """La submalla se puede convertir: FVF admitido e índices dentro de rango."""
        return self.objects[obj].layout is not None and submesh not in self.unusable.get(obj, ())

    @cached_property
    def parents(self) -> dict[int, int]:
        return {} if self.hierarchy is None else self.hierarchy.parent_map()

    def parent(self, obj: int) -> int | None:
        return self.parents.get(obj)

    def children(self, obj: int) -> tuple[int, ...]:
        return tuple(sorted(k for k, p in self.parents.items() if p == obj))

    def vertices(self, obj: int) -> VertexData:
        """Vértices del objeto; con varias partes, los de todas unidos (ver el módulo)."""
        item = self.objects[obj]
        if item.layout is None:
            raise UnsupportedVertexFormatError(f"obj{obj}: FVF {item.mesh.fvf:#x} no admitido")
        if len(item.parts) > 1:
            return self._merged_vertices(item)
        if item.vertex_buffer is None:
            raise KmdFormatError(f"obj{obj}: sin VB")
        size = item.mesh.vertex_count * item.layout.stride
        raw = self.xpr.data_bytes(item.vertex_buffer.data, size)
        return decode_vertices(raw, item.layout, item.mesh.vertex_count)

    def part_vertices(self, obj: int, part: int) -> VertexData:
        """Los vértices de una parte tal cual, con sus índices locales."""
        item = self.objects[obj].parts[part]
        if item.layout is None:
            raise UnsupportedVertexFormatError(
                f"obj{obj} parte {part}: FVF {item.mesh.fvf:#x} no admitido"
            )
        size = item.mesh.vertex_count * item.layout.stride
        raw = self.xpr.data_bytes(item.vertex_buffer.data, size)
        return decode_vertices(raw, item.layout, item.mesh.vertex_count)

    def part_indices(self, obj: int, submesh: int) -> tuple[int, ...]:
        """Índices de la tira de una submalla, locales a su parte (del IB o del push buffer)."""
        item = self.objects[obj]
        part_index, position = item.order[submesh]
        part = item.parts[part_index]
        sub = part.mesh.submeshes[position]
        if sub.source is IndexSource.PUSH_BUFFER:
            source = self.push_buffers[self.push_buffer_of[(obj, part_index, position)]]
            return source[sub.first_index : sub.first_index + sub.index_count]
        if part.index_buffer is None:
            raise KmdFormatError(f"obj{obj}: submalla {submesh} lee de un IB que no existe")
        raw = self.xpr.data_bytes(part.index_buffer.data + sub.start, sub.size)
        return tuple(int.from_bytes(raw[i : i + 2], "little") for i in range(0, len(raw), 2))

    def submesh_indices(self, obj: int, submesh: int) -> tuple[int, ...]:
        """Índices de la tira de una submalla en `vertices(obj)` (del IB o de su push buffer)."""
        indices = self.part_indices(obj, submesh)
        item = self.objects[obj]
        base = item.parts[item.order[submesh][0]].base
        return tuple(i + base for i in indices) if base else indices

    def _part_normals(self, item: KmdObject, part: int, positions: Sequence[Vec3]) -> list[Vec3]:
        """Normales suavizadas (media de las caras, por área) de una parte que no las trae."""
        sums = [[0.0, 0.0, 0.0] for _ in positions]
        for k, (p, _) in enumerate(item.order):
            if p != part or not self.exportable(item.index, k):
                continue
            for a, b, c in strip_to_triangles(self.part_indices(item.index, k)):
                normal = _face_normal(positions[a], positions[b], positions[c])
                for v in (a, b, c):
                    for axis in range(3):
                        sums[v][axis] += normal[axis]
        return [_unit(n) for n in sums]

    def _merged_vertices(self, item: KmdObject) -> VertexData:
        datas = [self.part_vertices(item.index, p.part) for p in item.parts]
        positions = tuple(v for data in datas for v in data.positions)
        normals: tuple[Vec3, ...] | None = None
        if any(data.normals is not None for data in datas):
            normals = tuple(
                n
                for p, data in enumerate(datas)
                for n in (
                    data.normals
                    if data.normals is not None
                    else self._part_normals(item, p, data.positions)
                )
            )
        uvs = None
        if any(data.uvs is not None for data in datas):
            uvs = tuple(_fill(((d.uvs, len(d)) for d in datas), FILL_UV))
        colors = None
        if any(data.colors is not None for data in datas):
            colors = tuple(_fill(((d.colors, len(d)) for d in datas), FILL_COLOR))
        return VertexData(positions, normals, uvs, None, colors)

    def triangles(self, obj: int, submesh: int) -> list[Triangle]:
        return strip_to_triangles(self.submesh_indices(obj, submesh))

    def local_translation(self, obj: int) -> Vec3:
        """Traslación del nodo respecto a su padre (las piezas con skin, en el padre)."""
        item = self.objects[obj]
        return ORIGIN if item.skinned else item.info.translation

    def world_translation(self, obj: int) -> Vec3:
        x = y = z = 0.0
        node: int | None = obj
        seen: set[int] = set()
        while node is not None and node not in seen:
            seen.add(node)
            tx, ty, tz = self.local_translation(node)
            x, y, z = x + tx, y + ty, z + tz
            node = self.parent(node)
        return (x, y, z)

    def ancestors(self, obj: int) -> list[int]:
        out: list[int] = []
        node = self.parent(obj)
        while node is not None and node not in out and node != obj:
            out.append(node)
            node = self.parent(node)
        return out

    def skin_joints(self, obj: int) -> tuple[int, int] | None:
        """Nodos (peso w, peso 1-w) de una pieza XYZB1: abuelo y padre (INFERRED).

        En las 12 piezas de `ryu.kmd` los vértices de peso 1 quedan del lado del abuelo
        (pecho, brazo, cadera, espinilla) y los de peso 0 del lado del padre.
        """
        parent = self.parent(obj)
        if parent is None:
            return None
        grandparent = self.parent(parent)
        return (parent if grandparent is None else grandparent, parent)


def _fill[T](columns: Iterable[tuple[Sequence[T] | None, int]], fill: T) -> list[T]:
    """Concatena columnas de atributos; una columna ausente pone `fill` en sus vértices."""
    out: list[T] = []
    for column, count in columns:
        out += column if column is not None else [fill] * count
    return out


def _face_normal(a: Vec3, b: Vec3, c: Vec3) -> Vec3:
    """(b - a) x (c - a): perpendicular a la cara, con módulo = 2 x área."""
    u = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
    v = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
    return (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])


def _unit(vector: Sequence[float]) -> Vec3:
    length = math.sqrt(sum(x * x for x in vector))
    if length < 1e-12:
        return FALLBACK_NORMAL
    return (vector[0] / length, vector[1] / length, vector[2] / length)


def _extents(xpr: XprFile) -> dict[int, int]:
    """Bytes disponibles desde el `Data` de cada buffer hasta el siguiente dato o el final."""
    starts = sorted(
        {
            r.data
            for r in xpr.resources
            if isinstance(r, VertexBuffer | IndexBuffer | PushBuffer | Texture)
        }
    )
    ends = dict(zip(starts, [*starts[1:], xpr.data_size], strict=True))
    return {start: end - start for start, end in ends.items()}


class _Checker:
    def __init__(self) -> None:
        self.issues: list[Issue] = []

    def add(self, obj: int | None, message: str) -> None:
        self.issues.append(Issue(obj, message))


def _check_buffers(check: _Checker, obj: KmdObject, part: KmdPart, extents: dict[int, int]) -> None:
    """VB e IB de una parte; con varias partes, los mensajes dicen cuál."""
    mesh = part.mesh
    where = f"parte {part.part}: " if len(obj.parts) > 1 else ""
    if part.layout is not None:
        need = mesh.vertex_count * part.layout.stride
        have = extents.get(part.vertex_buffer.data, 0)
        if need > have:
            check.add(obj.index, f"{where}VB de {have:#x} bytes, el descriptor pide {need:#x}")
    need_ib = 2 * mesh.index_count
    ib_size = 0 if part.index_buffer is None else extents.get(part.index_buffer.data, 0)
    if need_ib > ib_size:
        check.add(obj.index, f"{where}IB de {ib_size:#x} bytes para {mesh.index_count} índices")
    in_ib = [s for s in mesh.submeshes if s.source is IndexSource.INDEX_BUFFER]
    if sum(s.size for s in in_ib) != need_ib:
        total = sum(s.size for s in in_ib)
        check.add(obj.index, f"{where}las submallas del IB suman {total} bytes")
    for sub in in_ib:
        if sub.start + sub.size > need_ib:
            check.add(obj.index, f"submalla {sub.index}: rango fuera del IB")


def _check_object(
    check: _Checker, model: KmdModel, obj: KmdObject, texture_count: int, extents: dict[int, int]
) -> set[int]:
    """Invariantes de un objeto; devuelve las submallas que no se pueden exportar."""
    bad: set[int] = set()
    info, mesh = obj.info, obj.mesh
    if info.submesh_count != len(mesh.submeshes):
        check.add(
            obj.index, f"OBJINFO dice {info.submesh_count} submallas, hay {len(mesh.submeshes)}"
        )
    if any(abs(v) > 1e-6 for v in info.rotation):
        check.add(obj.index, f"rotación {info.rotation} ignorada (orden desconocido)")
    for part in obj.parts:
        _check_buffers(check, obj, part, extents)
    for position, sub in enumerate(mesh.submeshes):
        for stage in sub.stages:
            if not -1 <= stage.texture < texture_count:
                check.add(obj.index, f"submalla {sub.index}: textura {stage.texture} inexistente")
        if obj.layout is None:
            continue
        try:
            indices = model.submesh_indices(obj.index, position)
        except (KeyError, IndexError, XprError, KmdFormatError):
            check.add(obj.index, f"submalla {sub.index}: rango de índices ilegible")
            bad.add(position)
            continue
        part = obj.parts[obj.order[position][0]]
        limit = part.base + part.mesh.vertex_count
        if len(indices) != sub.index_count:
            check.add(obj.index, f"submalla {sub.index}: rango fuera de su origen de índices")
            bad.add(position)
        elif indices and max(indices) >= limit:
            check.add(obj.index, f"submalla {sub.index}: índice {max(indices)} >= {limit}")
            bad.add(position)
    return bad


def _check_hierarchy(check: _Checker, model: KmdModel) -> None:
    hierarchy, count = model.hierarchy, len(model.objects)
    if hierarchy is None:
        check.add(None, "sin HIERARC: todos los objetos quedan en la raíz")
        return
    if hierarchy.slot_count != count:
        check.add(None, f"HIERARC tiene {hierarchy.slot_count} slots y hay {count} objetos")
    parents = model.parents
    for node in hierarchy.nodes.values():
        if node.parent != parents.get(node.index):
            check.add(
                node.index,
                f"HIERARC: padre {node.parent}, las listas dicen {parents.get(node.index)}",
            )
    roots = [k for k in range(count) if k not in parents]
    if roots != [hierarchy.root]:
        check.add(None, f"raíces {roots}, la cabecera de HIERARC declara {hierarchy.root}")
    for node in hierarchy.nodes.values():
        subtree = 1 + sum(1 for k in range(count) if node.index in model.ancestors(k))
        if subtree != node.subtree_size:
            check.add(node.index, f"HIERARC: subtree {node.subtree_size}, el árbol da {subtree}")


def _layout(
    k: int, part: int, multi: bool, mesh: MeshDescriptor, check: _Checker
) -> VertexLayout | None:
    where = f"parte {part}: " if multi else ""
    try:
        layout = vertex_layout(mesh.fvf)
    except UnsupportedVertexFormatError as error:
        check.add(k, f"{where}{error}")
        return None
    if layout.stride != mesh.stride:
        check.add(
            k, f"{where}FVF {mesh.fvf:#x} mide {layout.stride} bytes, el descriptor {mesh.stride}"
        )
        return None
    return layout


def _merged_mesh(descriptor: MultipartDescriptor, parts: Sequence[KmdPart]) -> MeshDescriptor:
    """La vista unida de un objeto de varias partes (o vacía, sin partes)."""
    first = parts[0].mesh if parts else None
    return MeshDescriptor(
        fvf=first.fvf if first is not None else 0,
        vertex_count=sum(p.mesh.vertex_count for p in parts),
        stride=first.stride if first is not None else 0,
        index_count=sum(p.mesh.index_count for p in parts),
        header=descriptor.header,
        submeshes=tuple(descriptor.submesh(k) for k in range(descriptor.submesh_count)),
    )


def _merged_layout(k: int, parts: Sequence[KmdPart], check: _Checker) -> VertexLayout | None:
    """La de la parte 0 sin pesos; None si no hay partes o alguna no se sabe leer."""
    layouts = [p.layout for p in parts]
    first = layouts[0] if layouts else None
    if first is None or any(item is None for item in layouts):
        return None
    if any(item is not None and item.weight_count for item in layouts):
        check.add(k, "objeto de varias partes con pesos de mezcla: se exporta rígido")
    return replace(first, weight_count=0)


def _object(
    k: int,
    info: ObjectInfo,
    descriptor: MultipartDescriptor,
    parts: Sequence[KmdPart],
    check: _Checker,
) -> KmdObject:
    if len(parts) == 1:
        (only,) = parts
        return KmdObject(
            k,
            info,
            only.mesh,
            only.vertex_buffer,
            only.index_buffer,
            only.layout,
            (only,),
            descriptor.order,
        )
    return KmdObject(
        k,
        info,
        _merged_mesh(descriptor, parts),
        parts[0].vertex_buffer if parts else None,
        parts[0].index_buffer if parts else None,
        _merged_layout(k, parts, check),
        tuple(parts),
        descriptor.order,
    )


def _build_objects(
    infos: Sequence[ObjectInfo],
    descriptors: Sequence[MultipartDescriptor],
    buffers: Sequence[tuple[VertexBuffer, IndexBuffer | None]],
    check: _Checker,
) -> tuple[KmdObject, ...]:
    """Reparte los pares VB/IB entre las partes de cada objeto, en orden."""
    objects: list[KmdObject] = []
    cursor = 0
    for k, (info, descriptor) in enumerate(zip(infos, descriptors, strict=True)):
        multi = len(descriptor.parts) > 1
        parts: list[KmdPart] = []
        base = 0
        for p, mesh in enumerate(descriptor.parts):
            vb, ib = buffers[cursor]
            cursor += 1
            uses_ib = any(s.source is IndexSource.INDEX_BUFFER for s in mesh.submeshes)
            if ib is None and (uses_ib or mesh.index_count):
                where = f" (parte {p})" if multi else ""
                raise KmdFormatError(
                    f"obj{k}: su VB{where} no va seguido de un IB y el descriptor lo usa"
                )
            parts.append(KmdPart(p, mesh, vb, ib, _layout(k, p, multi, mesh, check), base))
            base += mesh.vertex_count
        objects.append(_object(k, info, descriptor, parts, check))
    return tuple(objects)


def _map_push_buffers(
    objects: Sequence[KmdObject], count: int, check: _Checker
) -> dict[PartRef, int]:
    users = [
        (obj.index, part, position)
        for obj in objects
        for part, position in obj.order
        if obj.parts[part].mesh.submeshes[position].source is IndexSource.PUSH_BUFFER
    ]
    if len(users) != count:
        check.add(None, f"{len(users)} submallas leen de push buffer y hay {count} push buffers")
    return {user: k for k, user in enumerate(users) if k < count}


def _buffer_pairs(xpr: XprFile) -> list[tuple[VertexBuffer, IndexBuffer | None]]:
    """Cada VB con el IB que lo sigue en las cabeceras (antes del siguiente VB), si lo hay."""
    pairs: list[tuple[VertexBuffer, IndexBuffer | None]] = []
    for resource in xpr.resources:
        if isinstance(resource, VertexBuffer):
            pairs.append((resource, None))
        elif isinstance(resource, IndexBuffer):
            if not pairs or pairs[-1][1] is not None:
                raise KmdFormatError(f"IB res{resource.index} sin un VB delante")
            pairs[-1] = (pairs[-1][0], resource)
    return pairs


def _parse_xpr(data: bytes, slot: SlotRef) -> XprFile:
    try:
        return parse_xpr(data[slot.offset : slot.end])
    except XprError as error:
        raise KmdFormatError(f"XPR0: {error}") from error


def parse_kmd(data: bytes) -> KmdModel:
    """Parsea un KMD completo y comprueba sus invariantes."""
    try:
        chunk = parse_chunk(data)
    except TeamNinjaError as error:
        raise KmdFormatError(str(error)) from error
    slots = _find_slots(data, chunk)
    info_slot = _require(slots, OBJINFO_TAG)
    xpr_slot = _require(slots, XPR_MAGIC.decode("ascii"))
    infos = parse_objinfo(data[info_slot.offset : info_slot.end])
    hier_slot = slots.get(HIERARC_TAG)
    hierarchy = None
    if hier_slot is not None:
        hierarchy = parse_hierarchy(data[hier_slot.offset : hier_slot.end])
    xpr = _parse_xpr(data, xpr_slot)
    resources = xpr.resources
    userdata = [r for r in resources if isinstance(r, UserData)]
    buffers = _buffer_pairs(xpr)
    pbs = [r for r in resources if isinstance(r, PushBuffer)]
    if not userdata or len(infos) != len(userdata) - 1:
        raise KmdFormatError(
            f"no se pueden emparejar los objetos: {len(infos)} en OBJINFO, "
            f"{len(userdata) - 1} descriptores y {len(buffers)} VB"
        )
    header = parse_model_header(userdata[0].payload)
    descriptors: list[MultipartDescriptor] = []
    for k, ud in enumerate(userdata[1:]):
        try:
            descriptors.append(parse_multipart_descriptor(ud.payload))
        except KmdFormatError as error:
            raise KmdFormatError(f"obj{k}: {error}") from error
    parts = sum(len(d.parts) for d in descriptors)
    if parts != len(buffers):
        raise KmdFormatError(
            f"no se pueden emparejar los objetos: {len(infos)} objetos con {parts} partes "
            f"y {len(buffers)} VB"
        )
    check = _Checker()
    objects = _build_objects(infos, descriptors, buffers, check)
    push_buffers = []
    for pb in pbs:
        try:
            push_buffers.append(decode_push_buffer(xpr.data_bytes(pb.data, pb.size)))
        except (KmdFormatError, XprError) as error:
            check.add(None, f"push buffer res{pb.index}: {error}")
            push_buffers.append(())
    model = KmdModel(
        tag=chunk.tag,
        header=header,
        xpr=xpr,
        xpr_offset=xpr_slot.offset,
        xpr_slot=xpr_slot.index,
        objects=objects,
        hierarchy=hierarchy,
        textures=xpr.textures,
        push_buffers=tuple(push_buffers),
        push_buffer_of=_map_push_buffers(objects, len(pbs), check),
    )
    return _with_checks(model, check)


def _with_checks(model: KmdModel, check: _Checker) -> KmdModel:
    header = model.header
    if header.texture_count != len(model.textures):
        check.add(
            None, f"la cabecera dice {header.texture_count} texturas, hay {len(model.textures)}"
        )
    vb_count = sum(len(obj.parts) for obj in model.objects)
    if header.vertex_buffer_count != vb_count:
        check.add(None, f"la cabecera dice {header.vertex_buffer_count} VB, hay {vb_count}")
    extents = _extents(model.xpr)
    unusable = {
        obj.index: frozenset(_check_object(check, model, obj, len(model.textures), extents))
        for obj in model.objects
    }
    _check_hierarchy(check, model)
    return replace(model, issues=tuple(check.issues), unusable=unusable)
