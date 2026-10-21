"""Un KMD de escenario: objetos con varias partes, push buffers, colisión e invariantes.

Un KMD de escenario (`boff_01.kmd`) es un KMD como el de un personaje (`ngb_tools.kmd`) con
estas diferencias (VERIFIED en los 111 KMD `<zona>_NN` del juego y en `stage00`/`stage02`
de `debug.afs`):

- **Partes.** Un objeto puede tener hasta 4 pares VB/IB, uno por FVF (`descriptor`). Las
  cabeceras del XPR0 van objeto a objeto: los VB (cada uno seguido de su IB, si lo hay) de
  sus partes y después sus push buffers. `boff_01.kmd`: 36 objetos, 43 VB y 42 IB.
- **El USERDATA 0** dice (63, texturas, VB, IB, push buffers): su tercer campo cuenta VB,
  no objetos (en los personajes coinciden). `boff_01.kmd`: (63, 98, 43, 42, 22).
- **Sin jerarquía y sin transformaciones.** Ningún escenario trae HIERARC. En OBJINFO, T y
  R valen 0 y S = (0, 0, 0) en todos los objetos: los vértices ya están en el espacio del
  escenario (la caja de OBJINFO `+0x60` cuadra con ellos). Aquí S = 0 no significa "oculto"
  como en los efectos de `ryu.kmd`: se ignora.
- **Grupos 0 y 6** en vez de 1-5: 2299 objetos de grupo 0 (geometría) y 847 de grupo 6
  (INFERRED: billboards y efectos; muchos miden 1 unidad y están en el origen, el juego los
  coloca en ejecución).
- **FVF:** 0x142 (XYZ|DIFFUSE|TEX1, el más común: luz precalculada en el color de vértice),
  0x152 (con normal), 0x112, 0x012 (sin UV), 0x052 y 0x042. No hay TEX2 (sin lightmaps).
- **HITMDL** con la colisión (`collision`), TEXANM y a veces MISCDATA o MORPH.

Las submallas se identifican con `SubmeshRef` = (objeto, parte, posición en la parte). Las
de origen "push buffer" consumen los push buffers del XPR0 en orden de objeto y de
submalla, igual que en `ngb_tools.kmd.model` (VERIFIED: en las 111 escenas el recuento
coincide y cada push buffer tiene justo los índices que declara su submalla).

Errores: lo que impide emparejar objetos, partes y buffers lanza `StageFormatError` (o el
`KmdFormatError` de los parsers de `ngb_tools.kmd`). El resto se anota como `Issue` y la
exportación sigue sin la parte afectada.
"""

from __future__ import annotations

from collections.abc import Iterator, Mapping, Sequence
from dataclasses import dataclass, field, replace
from functools import cached_property
from typing import Final

from ngb_tools.kmd.errors import KmdError, PushBufferError, UnsupportedVertexFormatError
from ngb_tools.kmd.hierarchy import HIERARC_TAG, Hierarchy, parse_hierarchy
from ngb_tools.kmd.mesh import (
    IndexSource,
    MeshDescriptor,
    ModelHeader,
    Submesh,
    parse_model_header,
)
from ngb_tools.kmd.model import Issue
from ngb_tools.kmd.objinfo import OBJINFO_TAG, ObjectInfo, Vec3, parse_objinfo
from ngb_tools.kmd.pushbuffer import decode_push_buffer
from ngb_tools.kmd.topology import Triangle, strip_to_triangles
from ngb_tools.kmd.vertices import VertexData, VertexLayout, decode_vertices, vertex_layout
from ngb_tools.stage.collision import HITMDL_TAG, CollisionMesh, parse_hitmdl
from ngb_tools.stage.descriptor import StageDescriptor, parse_stage_descriptor
from ngb_tools.stage.errors import StageFormatError
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

XPR_TAG: Final = XPR_MAGIC.decode("ascii")

type SubmeshRef = tuple[int, int, int]


@dataclass(frozen=True, slots=True)
class StagePart:
    """Una parte de un objeto: su descriptor de una parte y su par VB/IB."""

    obj: int
    part: int
    mesh: MeshDescriptor
    vertex_buffer: VertexBuffer
    index_buffer: IndexBuffer | None
    layout: VertexLayout | None


@dataclass(frozen=True, slots=True)
class StageObject:
    """Un objeto del escenario: su entrada de OBJINFO y sus partes."""

    index: int
    info: ObjectInfo
    descriptor: StageDescriptor
    parts: tuple[StagePart, ...]

    @property
    def group(self) -> int:
        return self.info.group

    def submeshes(self) -> Iterator[tuple[SubmeshRef, Submesh]]:
        """Las submallas en el orden del descriptor."""
        for part, position in self.descriptor.order:
            yield (self.index, part, position), self.parts[part].mesh.submeshes[position]


@dataclass(frozen=True)
class StageModel:
    """Un KMD de escenario parseado; `issues` son las invariantes incumplidas."""

    name: str
    header: ModelHeader
    xpr: XprFile
    xpr_offset: int
    xpr_slot: int
    objects: tuple[StageObject, ...]
    hierarchy: Hierarchy | None
    push_buffers: tuple[tuple[int, ...], ...]
    push_buffer_of: Mapping[SubmeshRef, int]
    collision: Mapping[int, CollisionMesh]
    issues: tuple[Issue, ...] = ()
    unusable: frozenset[SubmeshRef] = field(default_factory=frozenset)

    @property
    def textures(self) -> tuple[Texture, ...]:
        return self.xpr.textures

    @property
    def part_count(self) -> int:
        return sum(len(o.parts) for o in self.objects)

    def part(self, obj: int, part: int) -> StagePart:
        return self.objects[obj].parts[part]

    def submesh(self, ref: SubmeshRef) -> Submesh:
        obj, part, position = ref
        return self.part(obj, part).mesh.submeshes[position]

    def exportable(self, ref: SubmeshRef) -> bool:
        """FVF admitido e índices dentro de rango."""
        return self.part(ref[0], ref[1]).layout is not None and ref not in self.unusable

    def vertices(self, obj: int, part: int) -> VertexData:
        item = self.part(obj, part)
        if item.layout is None:
            raise UnsupportedVertexFormatError(
                f"obj{obj} parte {part}: FVF {item.mesh.fvf:#x} no admitido"
            )
        size = item.mesh.vertex_count * item.layout.stride
        raw = self.xpr.data_bytes(item.vertex_buffer.data, size)
        return decode_vertices(raw, item.layout, item.mesh.vertex_count)

    def submesh_indices(self, ref: SubmeshRef) -> tuple[int, ...]:
        """Índices de la tira de una submalla (del IB de su parte o de su push buffer)."""
        item = self.part(ref[0], ref[1])
        sub = self.submesh(ref)
        if sub.source is IndexSource.PUSH_BUFFER:
            source = self.push_buffers[self.push_buffer_of[ref]]
            return source[sub.first_index : sub.first_index + sub.index_count]
        if item.index_buffer is None:
            raise StageFormatError(f"obj{ref[0]} parte {ref[1]}: submalla lee de un IB que no hay")
        raw = self.xpr.data_bytes(item.index_buffer.data + sub.start, sub.size)
        return tuple(int.from_bytes(raw[i : i + 2], "little") for i in range(0, len(raw), 2))

    def triangles(self, ref: SubmeshRef) -> list[Triangle]:
        return strip_to_triangles(self.submesh_indices(ref))

    @cached_property
    def parents(self) -> dict[int, int]:
        return {} if self.hierarchy is None else self.hierarchy.parent_map()

    def world_translation(self, obj: int) -> Vec3:
        """Suma de las T de OBJINFO por la cadena de padres (0 en todos los escenarios)."""
        x = y = z = 0.0
        node: int | None = obj
        seen: set[int] = set()
        while node is not None and node not in seen:
            seen.add(node)
            tx, ty, tz = self.objects[node].info.translation
            x, y, z = x + tx, y + ty, z + tz
            node = self.parents.get(node)
        return (x, y, z)


@dataclass(frozen=True, slots=True)
class _Slot:
    index: int
    offset: int
    end: int


def _find_slots(data: bytes, chunk: TeamNinjaChunk) -> dict[str, _Slot]:
    """Slots por etiqueta de subchunk (o `XPR0` por su magia); una etiqueta repetida falla.

    TODO(kmd): es `ngb_tools.kmd.model._find_slots`, que es privada.
    """
    found: dict[str, _Slot] = {}
    for slot in chunk.slots:
        head = data[slot.offset : slot.offset + TAG_SIZE]
        if head.startswith(XPR_MAGIC):
            tag = XPR_TAG
        else:
            raw = head.rstrip(b"\0")
            if not raw or not all(0x20 <= b <= 0x7E for b in raw):
                continue
            tag = raw.decode("ascii")
        if tag in found:
            raise StageFormatError(f"slot '{tag}' repetido ({found[tag].index} y {slot.index})")
        found[tag] = _Slot(slot.index, slot.offset, slot.end)
    return found


def _buffer_pairs(xpr: XprFile) -> list[tuple[VertexBuffer, IndexBuffer | None]]:
    """Cada VB con el IB que lo sigue en las cabeceras (antes del siguiente VB), si lo hay."""
    pairs: list[tuple[VertexBuffer, IndexBuffer | None]] = []
    for resource in xpr.resources:
        if isinstance(resource, VertexBuffer):
            pairs.append((resource, None))
        elif isinstance(resource, IndexBuffer):
            if not pairs or pairs[-1][1] is not None:
                raise StageFormatError(f"IB res{resource.index} sin un VB delante")
            pairs[-1] = (pairs[-1][0], resource)
    return pairs


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


def _layout(obj: int, part: int, mesh: MeshDescriptor, issues: list[Issue]) -> VertexLayout | None:
    try:
        layout = vertex_layout(mesh.fvf)
    except UnsupportedVertexFormatError as error:
        issues.append(Issue(obj, f"parte {part}: {error}"))
        return None
    if layout.stride != mesh.stride:
        issues.append(
            Issue(
                obj, f"parte {part}: FVF {mesh.fvf:#x} mide {layout.stride}, stride {mesh.stride}"
            )
        )
        return None
    return layout


def _build_objects(
    infos: Sequence[ObjectInfo],
    descriptors: Sequence[StageDescriptor],
    pairs: Sequence[tuple[VertexBuffer, IndexBuffer | None]],
    issues: list[Issue],
) -> tuple[StageObject, ...]:
    total = sum(len(d.parts) for d in descriptors)
    if total != len(pairs):
        raise StageFormatError(f"los descriptores tienen {total} partes y hay {len(pairs)} VB")
    objects: list[StageObject] = []
    cursor = 0
    for k, (info, descriptor) in enumerate(zip(infos, descriptors, strict=True)):
        parts: list[StagePart] = []
        for p, mesh in enumerate(descriptor.parts):
            vb, ib = pairs[cursor]
            cursor += 1
            uses_ib = any(s.source is IndexSource.INDEX_BUFFER for s in mesh.submeshes)
            if ib is None and (uses_ib or mesh.index_count):
                raise StageFormatError(f"obj{k} parte {p}: su VB no va seguido de un IB")
            parts.append(StagePart(k, p, mesh, vb, ib, _layout(k, p, mesh, issues)))
        objects.append(StageObject(k, info, descriptor, tuple(parts)))
    return tuple(objects)


def decode_stage_push_buffer(raw: bytes) -> tuple[int, ...]:
    """`decode_push_buffer` tolerando basura en el u32 de relleno que sigue a END.

    En los personajes ese último u32 vale 0; en 64 push buffers de 8 escenarios
    (`ssato_02`, `suiro_02`, `zppln_01`...) trae un valor cualquiera (`0xc2f916e6` en
    `ssato_02.kmd` res236). Si la lectura estricta falla, se repite con ese u32 a 0.

    TODO(kmd): que `ngb_tools.kmd.pushbuffer` ignore el relleno tras END.
    """
    try:
        return decode_push_buffer(raw)
    except PushBufferError:
        if len(raw) < 4 or not any(raw[-4:]):
            raise
        return decode_push_buffer(raw[:-4] + bytes(4))


def _decode_push_buffers(xpr: XprFile, issues: list[Issue]) -> tuple[tuple[int, ...], ...]:
    decoded: list[tuple[int, ...]] = []
    for pb in (r for r in xpr.resources if isinstance(r, PushBuffer)):
        try:
            decoded.append(decode_stage_push_buffer(xpr.data_bytes(pb.data, pb.size)))
        except (KmdError, XprError) as error:
            issues.append(Issue(None, f"push buffer res{pb.index}: {error}"))
            decoded.append(())
    return tuple(decoded)


def _map_push_buffers(
    objects: Sequence[StageObject], count: int, issues: list[Issue]
) -> dict[SubmeshRef, int]:
    users = [
        ref
        for obj in objects
        for ref, sub in obj.submeshes()
        if sub.source is IndexSource.PUSH_BUFFER
    ]
    if len(users) != count:
        issues.append(Issue(None, f"{len(users)} submallas leen de push buffer y hay {count}"))
    return {ref: k for k, ref in enumerate(users) if k < count}


def _check_part(item: StagePart, extents: Mapping[int, int], issues: list[Issue]) -> None:
    mesh, obj, p = item.mesh, item.obj, item.part
    if item.layout is not None:
        need = mesh.vertex_count * item.layout.stride
        have = extents.get(item.vertex_buffer.data, 0)
        if need > have:
            issues.append(Issue(obj, f"parte {p}: VB de {have:#x} bytes, se piden {need:#x}"))
    need_ib = 2 * mesh.index_count
    have_ib = 0 if item.index_buffer is None else extents.get(item.index_buffer.data, 0)
    if need_ib > have_ib:
        issues.append(Issue(obj, f"parte {p}: IB de {have_ib:#x} bytes para {need_ib:#x}"))
    in_ib = sum(s.size for s in mesh.submeshes if s.source is IndexSource.INDEX_BUFFER)
    if in_ib != need_ib:
        issues.append(Issue(obj, f"parte {p}: las submallas del IB suman {in_ib} bytes"))


def _check_submesh(model: StageModel, ref: SubmeshRef, sub: Submesh, issues: list[Issue]) -> bool:
    """Invariantes de una submalla; False si no se puede exportar."""
    obj = ref[0]
    for stage in sub.stages:
        if not -1 <= stage.texture < len(model.textures):
            issues.append(Issue(obj, f"submalla {sub.index}: textura {stage.texture} inexistente"))
    item = model.part(obj, ref[1])
    if item.layout is None:
        return False
    try:
        indices = model.submesh_indices(ref)
    except (KeyError, IndexError, XprError, StageFormatError):
        issues.append(Issue(obj, f"submalla {sub.index}: rango de índices ilegible"))
        return False
    if len(indices) != sub.index_count:
        issues.append(Issue(obj, f"submalla {sub.index}: rango fuera de su origen de índices"))
        return False
    if indices and max(indices) >= item.mesh.vertex_count:
        issues.append(
            Issue(obj, f"submalla {sub.index}: índice {max(indices)} >= {item.mesh.vertex_count}")
        )
        return False
    return True


def _with_checks(model: StageModel, issues: list[Issue]) -> StageModel:
    header, vb_count = model.header, model.part_count
    if header.texture_count != len(model.textures):
        issues.append(Issue(None, f"la cabecera dice {header.texture_count} texturas"))
    if header.object_count != vb_count:
        issues.append(Issue(None, f"la cabecera dice {header.object_count} VB, hay {vb_count}"))
    extents = _extents(model.xpr)
    unusable: set[SubmeshRef] = set()
    for obj in model.objects:
        info = obj.info
        if info.submesh_count != obj.descriptor.submesh_count:
            issues.append(
                Issue(obj.index, f"OBJINFO dice {info.submesh_count} submallas, hay otras")
            )
        if any(abs(v) > 1e-6 for v in info.rotation):
            issues.append(Issue(obj.index, f"rotación {info.rotation} ignorada"))
        for item in obj.parts:
            _check_part(item, extents, issues)
        for ref, sub in obj.submeshes():
            if not _check_submesh(model, ref, sub, issues):
                unusable.add(ref)
    return replace(model, issues=tuple(issues), unusable=frozenset(unusable))


def _collision(
    data: bytes, slots: Mapping[str, _Slot], count: int, issues: list[Issue]
) -> dict[int, CollisionMesh]:
    slot = slots.get(HITMDL_TAG)
    if slot is None:
        return {}
    try:
        return parse_hitmdl(data[slot.offset : slot.end], count)
    except StageFormatError as error:
        issues.append(Issue(None, f"colisión descartada: {error}"))
        return {}


def parse_stage_kmd(data: bytes, name: str = "") -> StageModel:
    """Parsea un KMD de escenario (o cualquier KMD) y comprueba sus invariantes."""
    try:
        chunk = parse_chunk(data)
    except TeamNinjaError as error:
        raise StageFormatError(f"{name}: {error}") from error
    slots = _find_slots(data, chunk)
    for tag in (OBJINFO_TAG, XPR_TAG):
        if tag not in slots:
            raise StageFormatError(f"{name}: el KMD no tiene el slot {tag}")
    info_slot, xpr_slot = slots[OBJINFO_TAG], slots[XPR_TAG]
    infos = parse_objinfo(data[info_slot.offset : info_slot.end])
    hier = slots.get(HIERARC_TAG)
    hierarchy = None if hier is None else parse_hierarchy(data[hier.offset : hier.end])
    try:
        xpr = parse_xpr(data[xpr_slot.offset : xpr_slot.end])
    except XprError as error:
        raise StageFormatError(f"{name}: XPR0: {error}") from error
    userdata = [r for r in xpr.resources if isinstance(r, UserData)]
    if len(userdata) != len(infos) + 1:
        raise StageFormatError(
            f"{name}: {len(infos)} objetos en OBJINFO y {len(userdata) - 1} descriptores"
        )
    descriptors = []
    for k, ud in enumerate(userdata[1:]):
        try:
            descriptors.append(parse_stage_descriptor(ud.payload))
        except (StageFormatError, KmdError) as error:
            raise StageFormatError(f"{name}: obj{k}: {error}") from error
    issues: list[Issue] = []
    objects = _build_objects(infos, descriptors, _buffer_pairs(xpr), issues)
    push_buffers = _decode_push_buffers(xpr, issues)
    model = StageModel(
        name=name,
        header=parse_model_header(userdata[0].payload),
        xpr=xpr,
        xpr_offset=xpr_slot.offset,
        xpr_slot=xpr_slot.index,
        objects=objects,
        hierarchy=hierarchy,
        push_buffers=push_buffers,
        push_buffer_of=_map_push_buffers(objects, len(push_buffers), issues),
        collision=_collision(data, slots, len(infos), issues),
    )
    return _with_checks(model, issues)
