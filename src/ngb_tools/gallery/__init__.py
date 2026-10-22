"""Character gallery: every `chr_*.afs` to GLB, contact sheet and report.

Uses `ngb_tools.kmd` as a library (parsing, textures, rig and GLB) without touching its CLI.
Modules:

- `sources`: character AFS files, main KMD and motion bank lookup.
- `build`: per-character export with failure logging (never aborts the gallery).
- `raster`, `font`, `sheet`: front render, own 5x7 font and `gallery.png`.
- `report`: `gallery.md` and `gallery.json`.
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
