"""Galería de personajes: todos los `chr_*.afs` a GLB, hoja de contactos e informe.

Usa `ngb_tools.kmd` como biblioteca (parseo, texturas, rig y GLB) sin tocar su CLI.
Módulos:

- `sources`: AFS de personajes, KMD principal y búsqueda del banco de motions.
- `build`: exportación por personaje con registro de fallos (nunca aborta la galería).
- `raster`, `font`, `sheet`: render de frente, fuente 5x7 propia y `gallery.png`.
- `report`: `gallery.md` y `gallery.json`.
- `cli`: `python -m ngb_tools.gallery build`.
"""

from __future__ import annotations

from ngb_tools.gallery.build import CharacterResult, GalleryEntry, Status, build_gallery
from ngb_tools.gallery.sources import MotionIndex, main_kmd, motion_candidates

__all__ = [
    "CharacterResult",
    "GalleryEntry",
    "MotionIndex",
    "Status",
    "build_gallery",
    "main_kmd",
    "motion_candidates",
]
