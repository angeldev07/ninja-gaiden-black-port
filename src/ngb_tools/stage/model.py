"""A stage KMD: multi-part objects, push buffers, collision and invariants.

A stage KMD (`boff_01.kmd`) is a KMD like a character's (`ngb_tools.kmd`) with these
differences (VERIFIED on the 111 `<zone>_NN` KMDs of the game and on `stage00`/`stage02` of
`debug.afs`):

- **Parts.** An object can have up to 4 VB/IB pairs, one per FVF (`ngb_tools.kmd.mesh`). The
  XPR0 headers go object by object: the VBs (each followed by its IB, if any) of its parts
  and then its push buffers. `boff_01.kmd`: 36 objects, 43 VBs and 42 IBs.
- **USERDATA 0** says (63, textures, VB, IB, push buffers): its third field counts VBs, not
  objects (in characters they coincide). `boff_01.kmd`: (63, 98, 43, 42, 22).
- **No hierarchy and no transforms.** No stage carries HIERARC. In OBJINFO, T and R are 0
  and S = (0, 0, 0) in every object: the vertices are already in stage space (the OBJINFO
  box at `+0x60` matches them). Here S = 0 does not mean "hidden" as in the effects of
  `ryu.kmd`: it is ignored.
- **Groups 0 and 6** instead of 1-5: 2299 group-0 objects (geometry) and 847 group-6 objects
  (INFERRED: billboards and effects; many measure 1 unit and sit at the origin, the game
  places them at runtime).
- **FVF:** 0x142 (XYZ|DIFFUSE|TEX1, the most common: light baked into the vertex color),
  0x152 (with normal), 0x112, 0x012 (no UV), 0x052 and 0x042. There is no TEX2 (no
  lightmaps).
- **HITMDL** with the collision (`collision`), TEXANM and sometimes MISCDATA or MORPH.

Submeshes are identified by `SubmeshRef` = (object, part, position within the part). Those
whose source is "push buffer" consume the XPR0 push buffers in object and submesh order, as
in `ngb_tools.kmd.model` (VERIFIED: in all 111 scenes the count matches and each push buffer
has exactly the indices its submesh declares).

Errors: what prevents pairing objects, parts and buffers raises `StageFormatError` (or the
`KmdFormatError` of the `ngb_tools.kmd` parsers). The rest is recorded as an `Issue` and the
export continues without the affected part.
"""

from __future__ import annotations

from collections.abc import Iterator, Mapping, Sequence
from dataclasses import dataclass, field, replace
from functools import cached_property
from typing import Final

from ngb_tools.kmd.errors import KmdError, UnsupportedVertexFormatError
from ngb_tools.kmd.hierarchy import HIERARC_TAG, Hierarchy, parse_hierarchy
from ngb_tools.kmd.mesh import (
    IndexSource,
    MeshDescriptor,
    ModelHeader,
    MultipartDescriptor,
    Submesh,
    parse_model_header,
    parse_multipart_descriptor,
)
from ngb_tools.kmd.model import Issue
from ngb_tools.kmd.objinfo import OBJINFO_TAG, ObjectInfo, Vec3, parse_objinfo
from ngb_tools.kmd.pushbuffer import decode_push_buffer
from ngb_tools.kmd.topology import Triangle, strip_to_triangles
from ngb_tools.kmd.vertices import VertexData, VertexLayout, decode_vertices, vertex_layout
from ngb_tools.stage.collision import HITMDL_TAG, CollisionMesh, parse_hitmdl
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
    """A part of an object: its single-part descriptor and its VB/IB pair."""

    obj: int
    part: int
    mesh: MeshDescriptor
    vertex_buffer: VertexBuffer
    index_buffer: IndexBuffer | None
    layout: VertexLayout | None


@dataclass(frozen=True, slots=True)
class StageObject:
    """A stage object: its OBJINFO entry and its parts."""

    index: int
    info: ObjectInfo
    descriptor: MultipartDescriptor
    parts: tuple[StagePart, ...]

    @property
    def group(self) -> int:
        return self.info.group

    def submeshes(self) -> Iterator[tuple[SubmeshRef, Submesh]]:
        """The submeshes in descriptor order."""
        for part, position in self.descriptor.order:
            yield (self.index, part, position), self.parts[part].mesh.submeshes[position]


@dataclass(frozen=True)
class StageModel:
    """A parsed stage KMD; `issues` are the violated invariants."""

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
        """Supported FVF and indices within range."""
        return self.part(ref[0], ref[1]).layout is not None and ref not in self.unusable

    def vertices(self, obj: int, part: int) -> VertexData:
        item = self.part(obj, part)
        if item.layout is None:
            raise UnsupportedVertexFormatError(
                f"obj{obj} part {part}: FVF {item.mesh.fvf:#x} not supported"
            )
        size = item.mesh.vertex_count * item.layout.stride
        raw = self.xpr.data_bytes(item.vertex_buffer.data, size)
        return decode_vertices(raw, item.layout, item.mesh.vertex_count)

    def submesh_indices(self, ref: SubmeshRef) -> tuple[int, ...]:
        """Strip indices of a submesh (from the IB of its part or from its push buffer)."""
        item = self.part(ref[0], ref[1])
        sub = self.submesh(ref)
        if sub.source is IndexSource.PUSH_BUFFER:
            source = self.push_buffers[self.push_buffer_of[ref]]
            return source[sub.first_index : sub.first_index + sub.index_count]
        if item.index_buffer is None:
            raise StageFormatError(
                f"obj{ref[0]} part {ref[1]}: submesh reads from an IB that does not exist"
            )
        raw = self.xpr.data_bytes(item.index_buffer.data + sub.start, sub.size)
        return tuple(int.from_bytes(raw[i : i + 2], "little") for i in range(0, len(raw), 2))

    def triangles(self, ref: SubmeshRef) -> list[Triangle]:
        return strip_to_triangles(self.submesh_indices(ref))

    @cached_property
    def parents(self) -> dict[int, int]:
        return {} if self.hierarchy is None else self.hierarchy.parent_map()

    def world_translation(self, obj: int) -> Vec3:
        """Sum of the OBJINFO Ts along the parent chain (0 in every stage)."""
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
    """Slots by subchunk tag (or `XPR0` by its magic); a repeated tag fails.

    TODO(kmd): this is `ngb_tools.kmd.model._find_slots`, which is private.
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
            raise StageFormatError(f"slot '{tag}' repeated ({found[tag].index} and {slot.index})")
        found[tag] = _Slot(slot.index, slot.offset, slot.end)
    return found


def _buffer_pairs(xpr: XprFile) -> list[tuple[VertexBuffer, IndexBuffer | None]]:
    """Each VB with the IB that follows it in the headers (before the next VB), if any."""
    pairs: list[tuple[VertexBuffer, IndexBuffer | None]] = []
    for resource in xpr.resources:
        if isinstance(resource, VertexBuffer):
            pairs.append((resource, None))
        elif isinstance(resource, IndexBuffer):
            if not pairs or pairs[-1][1] is not None:
                raise StageFormatError(f"IB res{resource.index} without a VB before it")
            pairs[-1] = (pairs[-1][0], resource)
    return pairs


def _extents(xpr: XprFile) -> dict[int, int]:
    """Bytes available from each buffer's `Data` up to the next data or the end."""
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
        issues.append(Issue(obj, f"part {part}: {error}"))
        return None
    if layout.stride != mesh.stride:
        issues.append(
            Issue(obj, f"part {part}: FVF {mesh.fvf:#x} is {layout.stride}, stride {mesh.stride}")
        )
        return None
    return layout


def _build_objects(
    infos: Sequence[ObjectInfo],
    descriptors: Sequence[MultipartDescriptor],
    pairs: Sequence[tuple[VertexBuffer, IndexBuffer | None]],
    issues: list[Issue],
) -> tuple[StageObject, ...]:
    total = sum(len(d.parts) for d in descriptors)
    if total != len(pairs):
        raise StageFormatError(f"the descriptors have {total} parts and there are {len(pairs)} VB")
    objects: list[StageObject] = []
    cursor = 0
    for k, (info, descriptor) in enumerate(zip(infos, descriptors, strict=True)):
        parts: list[StagePart] = []
        for p, mesh in enumerate(descriptor.parts):
            vb, ib = pairs[cursor]
            cursor += 1
            uses_ib = any(s.source is IndexSource.INDEX_BUFFER for s in mesh.submeshes)
            if ib is None and (uses_ib or mesh.index_count):
                raise StageFormatError(f"obj{k} part {p}: its VB is not followed by an IB")
            parts.append(StagePart(k, p, mesh, vb, ib, _layout(k, p, mesh, issues)))
        objects.append(StageObject(k, info, descriptor, tuple(parts)))
    return tuple(objects)


def _decode_push_buffers(xpr: XprFile, issues: list[Issue]) -> tuple[tuple[int, ...], ...]:
    decoded: list[tuple[int, ...]] = []
    for pb in (r for r in xpr.resources if isinstance(r, PushBuffer)):
        try:
            decoded.append(decode_push_buffer(xpr.data_bytes(pb.data, pb.size)))
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
        issues.append(
            Issue(None, f"{len(users)} submeshes read from a push buffer and there are {count}")
        )
    return {ref: k for k, ref in enumerate(users) if k < count}


def _check_part(item: StagePart, extents: Mapping[int, int], issues: list[Issue]) -> None:
    mesh, obj, p = item.mesh, item.obj, item.part
    if item.layout is not None:
        need = mesh.vertex_count * item.layout.stride
        have = extents.get(item.vertex_buffer.data, 0)
        if need > have:
            issues.append(Issue(obj, f"part {p}: VB of {have:#x} bytes, {need:#x} requested"))
    need_ib = 2 * mesh.index_count
    have_ib = 0 if item.index_buffer is None else extents.get(item.index_buffer.data, 0)
    if need_ib > have_ib:
        issues.append(Issue(obj, f"part {p}: IB of {have_ib:#x} bytes for {need_ib:#x}"))
    in_ib = sum(s.size for s in mesh.submeshes if s.source is IndexSource.INDEX_BUFFER)
    if in_ib != need_ib:
        issues.append(Issue(obj, f"part {p}: the IB submeshes add up to {in_ib} bytes"))


def _check_submesh(model: StageModel, ref: SubmeshRef, sub: Submesh, issues: list[Issue]) -> bool:
    """Invariants of one submesh; False if it cannot be exported."""
    obj = ref[0]
    for stage in sub.stages:
        if not -1 <= stage.texture < len(model.textures):
            issues.append(
                Issue(obj, f"submesh {sub.index}: texture {stage.texture} does not exist")
            )
    item = model.part(obj, ref[1])
    if item.layout is None:
        return False
    try:
        indices = model.submesh_indices(ref)
    except (KeyError, IndexError, XprError, StageFormatError):
        issues.append(Issue(obj, f"submesh {sub.index}: unreadable index range"))
        return False
    if len(indices) != sub.index_count:
        issues.append(Issue(obj, f"submesh {sub.index}: range outside its index source"))
        return False
    if indices and max(indices) >= item.mesh.vertex_count:
        issues.append(
            Issue(obj, f"submesh {sub.index}: index {max(indices)} >= {item.mesh.vertex_count}")
        )
        return False
    return True


def _with_checks(model: StageModel, issues: list[Issue]) -> StageModel:
    header, vb_count = model.header, model.part_count
    if header.texture_count != len(model.textures):
        issues.append(Issue(None, f"the header says {header.texture_count} textures"))
    if header.vertex_buffer_count != vb_count:
        issues.append(
            Issue(None, f"the header says {header.vertex_buffer_count} VB, found {vb_count}")
        )
    extents = _extents(model.xpr)
    unusable: set[SubmeshRef] = set()
    for obj in model.objects:
        info = obj.info
        if info.submesh_count != obj.descriptor.submesh_count:
            issues.append(
                Issue(
                    obj.index,
                    f"OBJINFO says {info.submesh_count} submeshes, found a different number",
                )
            )
        if any(abs(v) > 1e-6 for v in info.rotation):
            issues.append(Issue(obj.index, f"rotation {info.rotation} ignored"))
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
        issues.append(Issue(None, f"collision discarded: {error}"))
        return {}


def parse_stage_kmd(data: bytes, name: str = "") -> StageModel:
    """Parse a stage KMD (or any KMD) and check its invariants."""
    try:
        chunk = parse_chunk(data)
    except TeamNinjaError as error:
        raise StageFormatError(f"{name}: {error}") from error
    slots = _find_slots(data, chunk)
    for tag in (OBJINFO_TAG, XPR_TAG):
        if tag not in slots:
            raise StageFormatError(f"{name}: the KMD has no {tag} slot")
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
            f"{name}: {len(infos)} objects in OBJINFO and {len(userdata) - 1} descriptors"
        )
    descriptors = []
    for k, ud in enumerate(userdata[1:]):
        try:
            descriptors.append(parse_multipart_descriptor(ud.payload))
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
