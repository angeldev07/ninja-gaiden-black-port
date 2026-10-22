"""HIERARC: the object tree of the model (the nodes are the objects themselves).

The subchunk carries the Team Ninja chunk header with the table at 0x30 and, in between,
16 bytes of its own (verified on `ryu.kmd` and `r_ryu.kmd`):

    0x20  u32 0xFFFFFFFF
    0x24  u32 0x00010001
    0x28  u32 root        root object (2 in ryu.kmd, 0 in r_ryu.kmd)
    0x2C  u32 0

Each slot k describes object k, as little-endian u16, padded to 16 bytes:

    self, parent (0xFFFF = root), n_children, subtree_size (includes the node itself),
    children[n_children]

A slot can be empty even though its object appears as a child of another one (in `ryu.kmd`,
effect objects 45 and 46): an object's parent comes from the children lists, and the `parent`
field of its entry, if it has one, is cross-checked against them (`ngb_tools.kmd.model`).
"""

from __future__ import annotations

import struct
from collections.abc import Mapping
from dataclasses import dataclass
from types import MappingProxyType
from typing import Final

from ngb_tools.kmd.errors import KmdFormatError
from ngb_tools.teamninja import TeamNinjaError, parse_chunk

HIERARC_TAG: Final = "HIERARC"
ROOT_FIELD: Final = 0x28
NO_PARENT: Final = 0xFFFF
_NODE: Final = struct.Struct("<4H")


@dataclass(frozen=True, slots=True)
class HierarchyNode:
    """One HIERARC entry."""

    index: int
    parent: int | None
    subtree_size: int
    children: tuple[int, ...]


@dataclass(frozen=True, slots=True)
class Hierarchy:
    """The tree: declared root, slot count and the entries present, by index."""

    root: int
    slot_count: int
    nodes: Mapping[int, HierarchyNode]

    def parent_map(self) -> dict[int, int]:
        """Parent of each object according to the children lists (covers empty slots)."""
        parents: dict[int, int] = {}
        for node in self.nodes.values():
            for child in node.children:
                parents.setdefault(child, node.index)
        return parents


def _parse_node(data: bytes, index: int, offset: int, end: int, slot_count: int) -> HierarchyNode:
    if offset + _NODE.size > end:
        raise KmdFormatError(f"HIERARC {index}: truncated entry")
    self_index, parent, n_children, subtree = _NODE.unpack_from(data, offset)
    if self_index != index:
        raise KmdFormatError(f"HIERARC {index}: the self field is {self_index}")
    children_end = offset + _NODE.size + 2 * n_children
    if children_end > end:
        raise KmdFormatError(f"HIERARC {index}: {n_children} children do not fit in the entry")
    children = struct.unpack_from(f"<{n_children}H", data, offset + _NODE.size)
    for child in children:
        if child >= slot_count or child == index:
            raise KmdFormatError(f"HIERARC {index}: child {child} out of range")
    if parent != NO_PARENT and parent >= slot_count:
        raise KmdFormatError(f"HIERARC {index}: parent {parent} out of range")
    return HierarchyNode(
        index=index,
        parent=None if parent == NO_PARENT else parent,
        subtree_size=subtree,
        children=tuple(children),
    )


def parse_hierarchy(data: bytes) -> Hierarchy:
    """Parse the HIERARC subchunk that starts at `data[0]`."""
    try:
        chunk = parse_chunk(data)
    except TeamNinjaError as error:
        raise KmdFormatError(f"HIERARC: {error}") from error
    if chunk.tag != HIERARC_TAG:
        raise KmdFormatError(f"expected subchunk {HIERARC_TAG}, found '{chunk.tag}'")
    if chunk.table_pos < ROOT_FIELD + 4:
        raise KmdFormatError(
            f"HIERARC: table at {chunk.table_pos:#x}, the root at 0x28 does not fit"
        )
    (root,) = struct.unpack_from("<I", data, ROOT_FIELD)
    if root >= chunk.n_slots:
        raise KmdFormatError(f"HIERARC: root {root} outside the {chunk.n_slots} slots")
    nodes = {
        slot.index: _parse_node(data, slot.index, slot.offset, slot.end, chunk.n_slots)
        for slot in chunk.slots
    }
    return Hierarchy(root=root, slot_count=chunk.n_slots, nodes=MappingProxyType(nodes))
