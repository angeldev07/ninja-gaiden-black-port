"""Cabecera de chunk de Team Ninja y búsqueda del slot XPR0 de un KMD.

Muchos archivos de los AFS (KMD, itm_dat2, chr_dat...) empiezan con la misma cabecera de
0x20 bytes, todos los campos en u32 little endian (verificado en `ryu.kmd`):

    0x00  tag[12]    nombre ASCII rellenado con NUL ("KMD", "OBJINFO"...)
    0x0C  size       en ryu.kmd, bytes del chunk; en r_ryu.kmd no cubre los dos últimos
                     slots, así que su significado está sin confirmar y no se usa de límite
    0x10  n_slots    entradas de la tabla de offsets
    0x14  n_used     entradas no vacías
    0x18  table_pos  posición de la tabla, relativa al chunk
    0x1C  0x20       tamaño de esta cabecera

En `table_pos` hay `u32 off[n_slots]` relativos al inicio del chunk; 0 marca un slot vacío.
Un slot no declara su tamaño: se extiende hasta el siguiente offset ocupado y el último,
hasta el final de los datos (que pueden traer el relleno de sector del AFS).

En `ryu.kmd` la tabla está en 0x80 y el slot 1 es un XPR0 (texturas y buffers de la GPU) en
0x3000. El resto del KMD (huesos, materiales, mallas) queda para el hito H3.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from typing import Final

HEADER_SIZE: Final = 0x20
TAG_SIZE: Final = 12
XPR0_MAGIC: Final = b"XPR0"
# Tope de entradas de la tabla. Un KMD tiene 10 (ryu.kmd, r_ryu.kmd); 4096 deja
# margen para otros chunks y evita que un `n_slots` hostil haga desempaquetar
# millones de enteros antes de descubrir que la tabla no cabe.
MAX_SLOTS: Final = 4096

_HEADER: Final = struct.Struct("<12s5I")


class TeamNinjaError(Exception):
    """Raíz de los errores de `ngb_tools.teamninja`."""


class ChunkFormatError(TeamNinjaError):
    """Los bytes no forman un chunk de Team Ninja coherente."""


class SlotNotFoundError(TeamNinjaError):
    """El chunk no tiene el slot pedido, o lo tiene más de una vez."""


@dataclass(frozen=True, slots=True)
class ChunkSlot:
    """Un slot ocupado: índice en la tabla y extensión `[offset, end)` dentro del chunk."""

    index: int
    offset: int
    end: int

    @property
    def size(self) -> int:
        return self.end - self.offset


@dataclass(frozen=True, slots=True)
class TeamNinjaChunk:
    """Cabecera ya validada y slots ocupados, en el orden de la tabla."""

    tag: str
    size: int
    n_slots: int
    n_used: int
    table_pos: int
    slots: tuple[ChunkSlot, ...]


def _decode_tag(raw: bytes) -> str:
    tag = raw.rstrip(b"\0")
    if not tag or not all(0x20 <= byte <= 0x7E for byte in tag):
        raise ChunkFormatError(f"tag no imprimible: {raw!r}")
    return tag.decode("ascii")


def _read_offsets(data: bytes, table_pos: int, n_slots: int) -> list[int]:
    if n_slots > MAX_SLOTS:
        raise ChunkFormatError(f"n_slots = {n_slots} supera el tope de {MAX_SLOTS}")
    limit = len(data)
    table_end = table_pos + 4 * n_slots
    if table_pos < HEADER_SIZE or table_end > limit:
        raise ChunkFormatError(
            f"tabla de slots [{table_pos:#x}, {table_end:#x}) fuera de los {limit:#x} bytes"
        )
    offsets = list(struct.unpack_from(f"<{n_slots}I", data, table_pos))
    for index, offset in enumerate(offsets):
        if offset and not table_end <= offset < limit:
            raise ChunkFormatError(
                f"slot {index}: offset {offset:#x} fuera de [{table_end:#x}, {limit:#x})"
            )
    return offsets


def _slots_from_offsets(offsets: list[int], limit: int) -> tuple[ChunkSlot, ...]:
    """Cada slot ocupado llega hasta el siguiente offset mayor; el último, hasta `limit`."""
    used = sorted({offset for offset in offsets if offset})
    if not used:  # chunk sin slots ocupados (HITMDL de un escenario sin colisión)
        return ()
    ends = dict(zip(used, [*used[1:], limit], strict=True))
    return tuple(
        ChunkSlot(index, offset, ends[offset]) for index, offset in enumerate(offsets) if offset
    )


def parse_chunk(data: bytes) -> TeamNinjaChunk:
    """Parsea y valida la cabecera de chunk que empieza en `data[0]`."""
    if len(data) < HEADER_SIZE:
        raise ChunkFormatError(f"{len(data)} bytes: no cabe la cabecera de {HEADER_SIZE:#x}")
    raw_tag, size, n_slots, n_used, table_pos, header_size = _HEADER.unpack_from(data)
    tag = _decode_tag(raw_tag)
    if header_size != HEADER_SIZE:
        raise ChunkFormatError(f"campo 0x1C = {header_size:#x}, se esperaba {HEADER_SIZE:#x}")
    offsets = _read_offsets(data, table_pos, n_slots)
    if (found := sum(1 for offset in offsets if offset)) != n_used:
        raise ChunkFormatError(f"n_used = {n_used} pero la tabla tiene {found} slots ocupados")
    if len(set(filter(None, offsets))) != n_used:
        raise ChunkFormatError("dos slots comparten offset")
    slots = _slots_from_offsets(offsets, len(data))
    return TeamNinjaChunk(tag, size, n_slots, n_used, table_pos, slots)


def find_xpr0_slot(data: bytes, chunk: TeamNinjaChunk | None = None) -> ChunkSlot:
    """El único slot del chunk cuya carga útil empieza por la magia `XPR0`."""
    parsed = parse_chunk(data) if chunk is None else chunk
    matches = [
        slot
        for slot in parsed.slots
        if data[slot.offset : slot.offset + len(XPR0_MAGIC)] == XPR0_MAGIC
    ]
    if not matches:
        raise SlotNotFoundError(f"el chunk '{parsed.tag}' no tiene ningún slot XPR0")
    if len(matches) > 1:
        indices = ", ".join(str(slot.index) for slot in matches)
        raise SlotNotFoundError(f"XPR0 ambiguo: {len(matches)} slots ({indices})")
    return matches[0]
