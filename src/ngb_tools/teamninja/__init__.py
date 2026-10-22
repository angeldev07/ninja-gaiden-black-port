"""Team Ninja's own formats that do not belong to a known middleware.

For now only the chunk header common to the AFS files (`chunk`) and the
search for the XPR0 carried inside a KMD. The rest of the KMD belongs to milestone H3.
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
