"""HIERARC: el árbol de objetos del modelo (los nodos son los propios objetos).

El subchunk lleva la cabecera de chunk de Team Ninja con la tabla en 0x30 y, entre medias,
16 bytes propios (verificado en `ryu.kmd` y `r_ryu.kmd`):

    0x20  u32 0xFFFFFFFF
    0x24  u32 0x00010001
    0x28  u32 root        objeto raíz (2 en ryu.kmd, 0 en r_ryu.kmd)
    0x2C  u32 0

Cada slot k describe el objeto k, en u16 little endian, rellenado a 16 bytes:

    self, parent (0xFFFF = raíz), n_children, subtree_size (incluye el propio nodo),
    children[n_children]

Un slot puede estar vacío aunque su objeto aparezca como hijo de otro (en `ryu.kmd`, los
objetos de efecto 45 y 46): el padre de un objeto sale de las listas de hijos, y el campo
`parent` de su entrada, si la tiene, se contrasta con ellas (`ngb_tools.kmd.model`).
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
    """Una entrada de HIERARC."""

    index: int
    parent: int | None
    subtree_size: int
    children: tuple[int, ...]


@dataclass(frozen=True, slots=True)
class Hierarchy:
    """El árbol: raíz declarada, número de slots y entradas presentes por índice."""

    root: int
    slot_count: int
    nodes: Mapping[int, HierarchyNode]

    def parent_map(self) -> dict[int, int]:
        """Padre de cada objeto según las listas de hijos (cubre los slots vacíos)."""
        parents: dict[int, int] = {}
        for node in self.nodes.values():
            for child in node.children:
                parents.setdefault(child, node.index)
        return parents


def _parse_node(data: bytes, index: int, offset: int, end: int, slot_count: int) -> HierarchyNode:
    if offset + _NODE.size > end:
        raise KmdFormatError(f"HIERARC {index}: entrada cortada")
    self_index, parent, n_children, subtree = _NODE.unpack_from(data, offset)
    if self_index != index:
        raise KmdFormatError(f"HIERARC {index}: el campo self vale {self_index}")
    children_end = offset + _NODE.size + 2 * n_children
    if children_end > end:
        raise KmdFormatError(f"HIERARC {index}: {n_children} hijos no caben en la entrada")
    children = struct.unpack_from(f"<{n_children}H", data, offset + _NODE.size)
    for child in children:
        if child >= slot_count or child == index:
            raise KmdFormatError(f"HIERARC {index}: hijo {child} fuera de rango")
    if parent != NO_PARENT and parent >= slot_count:
        raise KmdFormatError(f"HIERARC {index}: padre {parent} fuera de rango")
    return HierarchyNode(
        index=index,
        parent=None if parent == NO_PARENT else parent,
        subtree_size=subtree,
        children=tuple(children),
    )


def parse_hierarchy(data: bytes) -> Hierarchy:
    """Parsea el subchunk HIERARC que empieza en `data[0]`."""
    try:
        chunk = parse_chunk(data)
    except TeamNinjaError as error:
        raise KmdFormatError(f"HIERARC: {error}") from error
    if chunk.tag != HIERARC_TAG:
        raise KmdFormatError(f"se esperaba el subchunk {HIERARC_TAG}, hay '{chunk.tag}'")
    if chunk.table_pos < ROOT_FIELD + 4:
        raise KmdFormatError(f"HIERARC: tabla en {chunk.table_pos:#x}, no cabe la raíz en 0x28")
    (root,) = struct.unpack_from("<I", data, ROOT_FIELD)
    if root >= chunk.n_slots:
        raise KmdFormatError(f"HIERARC: raíz {root} fuera de los {chunk.n_slots} slots")
    nodes = {
        slot.index: _parse_node(data, slot.index, slot.offset, slot.end, chunk.n_slots)
        for slot in chunk.slots
    }
    return Hierarchy(root=root, slot_count=chunk.n_slots, nodes=MappingProxyType(nodes))
