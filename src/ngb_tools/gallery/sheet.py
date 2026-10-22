"""Contact sheet `gallery.png`: a front render per character, with its name below.

Grid of `columns` columns, with a title on top. Each cell holds the render (or a box with
FAILED), the character name and a second line: ANIM in green if the GLB carries the idle,
NO ANIM in grey if not, and the abbreviated cause in red if it was not exported.
"""

from __future__ import annotations

from collections.abc import Sequence
from typing import Final

from ngb_tools.gallery.build import CharacterResult, Status
from ngb_tools.gallery.font import GLYPH_HEIGHT, draw_text, text_width
from ngb_tools.xpr.png import encode_png

LABEL_HEIGHT: Final = 2 * GLYPH_HEIGHT * 2 + 10
TITLE_HEIGHT: Final = 40
GAP: Final = 6
BACKGROUND: Final = (16, 16, 22)
TILE_FAILED: Final = (48, 22, 26)
WHITE: Final = (235, 235, 240)
GREEN: Final = (120, 210, 120)
GREY: Final = (140, 140, 150)
RED: Final = (230, 90, 90)


def _paste(
    dst: bytearray, width: int, src: bytes, *, size: tuple[int, int], at: tuple[int, int]
) -> None:
    (w, h), (x, y) = size, at
    for row in range(h):
        d = 3 * ((y + row) * width + x)
        dst[d : d + 3 * w] = src[3 * row * w : 3 * (row + 1) * w]


def _fill(dst: bytearray, width: int, color: tuple[int, int, int], rect: tuple[int, ...]) -> None:
    x, y, w, h = rect
    line = bytes(color) * w
    for row in range(h):
        d = 3 * ((y + row) * width + x)
        dst[d : d + 3 * w] = line


def _centered(
    rgb: bytearray, width: int, box: tuple[int, int, int], text: str, color: tuple[int, int, int]
) -> None:
    """`text` centered in the box (x, width, y), at scale 2 if it fits."""
    x, w, y = box
    scale = 2 if text_width(text, 2) <= w else 1
    left = x + max(0, (w - text_width(text, scale)) // 2)
    draw_text(rgb, width, left, y, text, color=color, scale=scale)


def _second_line(result: CharacterResult) -> tuple[str, tuple[int, int, int]]:
    entry = result.entry
    if entry.status is Status.FAILED:
        reason = (entry.reason or "failed").split(":")[0]
        return reason.upper()[:24], RED
    return ("ANIM", GREEN) if entry.motion else ("NO ANIM", GREY)


def contact_sheet(
    results: Sequence[CharacterResult], tile_size: tuple[int, int], columns: int, title: str
) -> bytes:
    """RGBA PNG of the contact sheet."""
    tile_w, tile_h = tile_size
    cell_w, cell_h = tile_w + GAP, tile_h + LABEL_HEIGHT + GAP
    rows = max(1, (len(results) + columns - 1) // columns)
    width = GAP + columns * cell_w
    height = TITLE_HEIGHT + rows * cell_h + GAP
    rgb = bytearray(bytes(BACKGROUND) * (width * height))
    draw_text(rgb, width, GAP * 2, 12, title, color=WHITE, scale=2)
    for n, result in enumerate(results):
        x = GAP + (n % columns) * cell_w
        y = TITLE_HEIGHT + (n // columns) * cell_h
        if result.tile is not None:
            _paste(rgb, width, result.tile, size=(tile_w, tile_h), at=(x, y))
        else:
            _fill(rgb, width, TILE_FAILED, (x, y, tile_w, tile_h))
            _centered(rgb, width, (x, tile_w, y + tile_h // 2 - GLYPH_HEIGHT), "FAILED", RED)
        _centered(rgb, width, (x, tile_w, y + tile_h + 4), result.entry.character.upper(), WHITE)
        text, color = _second_line(result)
        _centered(rgb, width, (x, tile_w, y + tile_h + 8 + 2 * GLYPH_HEIGHT), text, color)
    rgba = bytearray(4 * width * height)
    rgba[0::4], rgba[1::4], rgba[2::4] = rgb[0::3], rgb[1::3], rgb[2::3]
    rgba[3::4] = bytes([255]) * (width * height)
    return encode_png(width, height, bytes(rgba))
