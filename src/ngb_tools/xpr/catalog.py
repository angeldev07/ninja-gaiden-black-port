"""Catalog of the textures in an XPR0: what is there, its layout and whether it can be exported.

Shared by `list` (which only reports) and `export` (which also decodes). Each texture is
validated with `check_texture`, which does not decode, so listing is cheap.
"""

from __future__ import annotations

from dataclasses import dataclass

from ngb_tools.xpr.errors import UnsupportedTextureError, XprTruncatedError
from ngb_tools.xpr.formats import lookup_format
from ngb_tools.xpr.resources import Texture, XprFile
from ngb_tools.xpr.texture import TextureCheck, TextureLayout, check_texture, texture_layout


@dataclass(frozen=True, slots=True)
class CatalogEntry:
    """One texture: its sequence number, its header and its layout or the reason for rejection."""

    ordinal: int
    texture: Texture
    layout: TextureLayout | None
    issue: str | None
    check: TextureCheck | None = None

    @property
    def warnings(self) -> list[str]:
        """Warnings for a texture that is exported (e.g. missing mips)."""
        if self.check is None or not self.check.missing_levels:
            return []
        total = len(self.check.layout.levels)
        return [
            f"{self.check.missing_levels} of {total} mip levels are missing outside the XPR0: "
            "level 0 is exported"
        ]

    @property
    def format_name(self) -> str:
        fmt = lookup_format(self.texture.format.code)
        return fmt.name if fmt is not None else f"{self.texture.format.code:#04x}"

    @property
    def width(self) -> int:
        return self.layout.width if self.layout is not None else self.texture.format.width

    @property
    def height(self) -> int:
        return self.layout.height if self.layout is not None else self.texture.format.height

    @property
    def filename(self) -> str | None:
        """`NNNN_<format>_<width>x<height>.png`, or `None` if it is not exported."""
        if self.issue is not None:
            return None
        return f"{self.ordinal:04d}_{self.format_name}_{self.width}x{self.height}.png"


def catalog(xpr: XprFile) -> tuple[CatalogEntry, ...]:
    """All the textures, in header order."""
    entries: list[CatalogEntry] = []
    for ordinal, texture in enumerate(xpr.textures):
        try:
            layout = texture_layout(texture)
        except UnsupportedTextureError as error:
            entries.append(CatalogEntry(ordinal, texture, None, str(error)))
            continue
        # With a known layout, a later rejection (palette, truncated data) keeps it.
        try:
            check = check_texture(xpr, texture)
        except (UnsupportedTextureError, XprTruncatedError) as error:
            entries.append(CatalogEntry(ordinal, texture, layout, str(error)))
        else:
            entries.append(CatalogEntry(ordinal, texture, layout, None, check))
    return tuple(entries)
