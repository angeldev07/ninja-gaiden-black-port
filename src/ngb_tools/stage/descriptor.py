"""Descriptor de malla con varias partes (varios pares VB/IB por objeto).

El USERDATA de un objeto empieza por `u32 n_parts, u32 n_sub`. `ngb_tools.kmd.mesh` lee el
primer u32 como "versión" y solo admite 1. En los escenarios vale 1, 2, 3 o 4 (VERIFIED en
los 111 KMD de escenario `<zona>_NN`: 2684 objetos con 1 parte, 430 con 2, 31 con 3 y 1 con
4). La cabecera tiene un bloque de 8 u32 por parte:

    0x00  n_parts
    0x04  n_sub              submallas de todas las partes
    0x08 + 0x20 * p          fvf, ?, ?, vertex_count, stride, ?, ?, index_count

Con `n_parts = 1` es justo la cabecera de 0x28 bytes de `ngb_tools.kmd.mesh`. Detrás van
las `n_sub` submallas con el mismo registro de siempre (0x60 fijos, material, etapas); el
u32 `+0x04` del registro fijo, que en los personajes vale 0, es **la parte** de la que leen
la submalla sus vértices y sus índices (VERIFIED: en las 111 escenas, el VB de la parte p
es el p-ésimo VB del objeto y las submallas de cada parte suman justo su `index_count`).
Ejemplo, `boff_05.kmd` obj1: parte 0 FVF 0x152 con 8459 vértices y 4883 índices (11
submallas), parte 1 FVF 0x142 con 709 vértices y 1347 índices (1 submalla, `og01`).

Cada parte se convierte en un descriptor de una parte (`MeshDescriptor`) y se parsea con
`ngb_tools.kmd.mesh.parse_mesh_descriptor`, que valida todo lo demás.

TODO(kmd): cuando `ngb_tools.kmd.mesh` admita varias partes, este módulo sobra. Ese cambio
arregla además el cuerpo de Doku (`chr_doku#0`: 88 VB para 87 objetos, "origen de submalla
24" es el desfase de leer como submalla la segunda parte de la cabecera).
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from typing import Final

from ngb_tools.kmd.mesh import (
    MATERIAL_SIZE,
    MAX_STAGES,
    MAX_SUBMESHES,
    STAGE_SIZE,
    SUBMESH_FIXED_SIZE,
    MeshDescriptor,
    parse_mesh_descriptor,
)
from ngb_tools.stage.errors import StageFormatError

PREFIX_SIZE: Final = 8
PART_HEADER_SIZE: Final = 0x20
# Tope de partes por objeto: el máximo observado es 4.
MAX_PARTS: Final = 16
FIELD_PART: Final = 1
FIELD_STAGES: Final = 2
FIELD_MATERIAL: Final = 8
_PREFIX: Final = struct.Struct("<2I")
_FIXED: Final = struct.Struct("<24I")


@dataclass(frozen=True, slots=True)
class StageDescriptor:
    """Las partes de un objeto. `order[k]` = (parte, posición en la parte) de la submalla k."""

    parts: tuple[MeshDescriptor, ...]
    order: tuple[tuple[int, int], ...]

    @property
    def submesh_count(self) -> int:
        return len(self.order)


def _record_end(payload: bytes, ordinal: int, pos: int) -> tuple[int, int]:
    """Parte y fin del registro de la submalla que empieza en `pos`.

    Solo lee lo imprescindible para trocear (parte, etapas y material); el resto lo valida
    `parse_mesh_descriptor`.
    """
    if pos + SUBMESH_FIXED_SIZE > len(payload):
        raise StageFormatError(f"submalla {ordinal}: registro fijo cortado en {pos:#x}")
    fixed = _FIXED.unpack_from(payload, pos)
    part, n_tex, has_material = fixed[FIELD_PART], fixed[FIELD_STAGES], fixed[FIELD_MATERIAL]
    if has_material not in (0, 1) or n_tex > MAX_STAGES:
        raise StageFormatError(
            f"submalla {ordinal}: material {has_material} o {n_tex} etapas inválidos"
        )
    end = pos + SUBMESH_FIXED_SIZE + MATERIAL_SIZE * has_material + STAGE_SIZE * n_tex
    if end > len(payload):
        raise StageFormatError(f"submalla {ordinal}: material y etapas no caben en el descriptor")
    return part, end


def parse_stage_descriptor(payload: bytes) -> StageDescriptor:
    """Parsea un descriptor de una o varias partes; tiene que ocupar todo el USERDATA."""
    if len(payload) < PREFIX_SIZE:
        raise StageFormatError(f"descriptor de {len(payload)} bytes, mínimo {PREFIX_SIZE}")
    n_parts, n_sub = _PREFIX.unpack_from(payload)
    # 0 partes: objeto vacío sin VB (`rail_cmn` `ra_door1.kmd` obj1, USERDATA de 8 bytes).
    if n_parts > MAX_PARTS:
        raise StageFormatError(f"descriptor con {n_parts} partes supera el tope de {MAX_PARTS}")
    if n_sub > MAX_SUBMESHES:
        raise StageFormatError(
            f"descriptor con {n_sub} submallas supera el tope de {MAX_SUBMESHES}"
        )
    pos = PREFIX_SIZE + PART_HEADER_SIZE * n_parts
    if pos > len(payload):
        raise StageFormatError(f"cabecera de {n_parts} partes cortada ({len(payload):#x} bytes)")
    records: list[list[bytes]] = [[] for _ in range(n_parts)]
    order: list[tuple[int, int]] = []
    for ordinal in range(n_sub):
        part, end = _record_end(payload, ordinal, pos)
        if part >= n_parts:
            raise StageFormatError(f"submalla {ordinal}: parte {part}, hay {n_parts}")
        order.append((part, len(records[part])))
        records[part].append(payload[pos:end])
        pos = end
    if pos != len(payload):
        raise StageFormatError(
            f"descriptor: las submallas acaban en {pos:#x} pero el USERDATA mide {len(payload):#x}"
        )
    parts = []
    for p, chunks in enumerate(records):
        start = PREFIX_SIZE + PART_HEADER_SIZE * p
        single = (
            _PREFIX.pack(1, len(chunks))
            + payload[start : start + PART_HEADER_SIZE]
            + b"".join(chunks)
        )
        parts.append(parse_mesh_descriptor(single))
    return StageDescriptor(tuple(parts), tuple(order))
