"""Catálogo de las texturas de un XPR0: qué hay, cómo está dispuesto y si se puede exportar.

Lo comparten `list` (que solo informa) y `export` (que además decodifica). Cada textura se
valida con `check_texture`, que no decodifica, así que listar es barato.
"""

from __future__ import annotations

from dataclasses import dataclass

from ngb_tools.xpr.errors import UnsupportedTextureError, XprTruncatedError
from ngb_tools.xpr.formats import lookup_format
from ngb_tools.xpr.resources import Texture, XprFile
from ngb_tools.xpr.texture import TextureCheck, TextureLayout, check_texture, texture_layout


@dataclass(frozen=True, slots=True)
class CatalogEntry:
    """Una textura: su número de orden, su cabecera y su disposición o el motivo de rechazo."""

    ordinal: int
    texture: Texture
    layout: TextureLayout | None
    issue: str | None
    check: TextureCheck | None = None

    @property
    def warnings(self) -> list[str]:
        """Avisos de una textura que sí se exporta (p. ej. mips que faltan)."""
        if self.check is None or not self.check.missing_levels:
            return []
        total = len(self.check.layout.levels)
        return [
            f"faltan {self.check.missing_levels} de {total} niveles de mip fuera del XPR0: "
            "se exporta el nivel 0"
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
        """`NNNN_<formato>_<ancho>x<alto>.png`, o `None` si no se exporta."""
        if self.issue is not None:
            return None
        return f"{self.ordinal:04d}_{self.format_name}_{self.width}x{self.height}.png"


def catalog(xpr: XprFile) -> tuple[CatalogEntry, ...]:
    """Todas las texturas, en el orden de las cabeceras."""
    entries: list[CatalogEntry] = []
    for ordinal, texture in enumerate(xpr.textures):
        try:
            layout = texture_layout(texture)
        except UnsupportedTextureError as error:
            entries.append(CatalogEntry(ordinal, texture, None, str(error)))
            continue
        # Con disposición conocida, un rechazo posterior (paleta, datos cortados) la conserva.
        try:
            check = check_texture(xpr, texture)
        except (UnsupportedTextureError, XprTruncatedError) as error:
            entries.append(CatalogEntry(ordinal, texture, layout, str(error)))
        else:
            entries.append(CatalogEntry(ordinal, texture, layout, None, check))
    return tuple(entries)
