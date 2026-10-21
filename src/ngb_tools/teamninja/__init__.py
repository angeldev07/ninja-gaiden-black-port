"""Formatos propios de Team Ninja que no son de un middleware conocido.

De momento solo la cabecera de chunk común a los archivos de los AFS (`chunk`) y la
búsqueda del XPR0 que lleva dentro un KMD. El resto del KMD es del hito H3.
"""

from __future__ import annotations

from ngb_tools.teamninja.chunk import (
    MAX_SLOTS,
    ChunkFormatError,
    ChunkSlot,
    SlotNotFoundError,
    TeamNinjaChunk,
    TeamNinjaError,
    find_xpr0_slot,
    parse_chunk,
)

__all__ = [
    "MAX_SLOTS",
    "ChunkFormatError",
    "ChunkSlot",
    "SlotNotFoundError",
    "TeamNinjaChunk",
    "TeamNinjaError",
    "find_xpr0_slot",
    "parse_chunk",
]
