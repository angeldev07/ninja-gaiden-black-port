"""Gallery: each `chr_*.afs` to a GLB, with its idle if there is a motion bank and a humanoid rig.

Per character (`chr_<name>.afs`):

1. Main KMD (`ngb_tools.gallery.sources.main_kmd`), parsed with `ngb_tools.kmd`.
2. Textures to PNG in `<out>/textures/<name>` (`ngb_tools.kmd.textures`).
3. Animation: bank `<name>.mtn` (or `.mot`) from the AFS itself or from the common ones,
   humanoid rig (`ngb_tools.kmd.rig`) and idle motion: id 1 if it is not the filler one (in
   `ryu.mtn` it is the idle, VERIFIED visually) or, failing that, the first id with a motion
   different from id 0 (INFERRED). If any of this fails, the GLB comes out without animation
   and the reason is noted.
4. `<out>/<name>.glb` with skin, and a front render for the contact sheet.

A character that cannot be exported (no KMD, unreadable KMD, like Doku's multipart body) is
recorded with its reason and the gallery goes on. The known errors (AFS, KMD, XPR, I/O) are
described as they are. Any other exception is noted as an "internal error": it is a bug, but
it does not bring down the rest.
"""

from __future__ import annotations

import struct
from collections.abc import Callable, Sequence
from dataclasses import dataclass, field
from enum import StrEnum
from pathlib import Path
from typing import Final

from ngb_tools.afs.errors import AfsError
from ngb_tools.gallery.raster import Color, model_triangles, render_front
from ngb_tools.gallery.sources import (
    EntryRef,
    MotionIndex,
    character_name,
    list_entries,
    main_kmd,
    motion_candidates,
)
from ngb_tools.kmd.animation import AnimationClip
from ngb_tools.kmd.errors import KmdError
from ngb_tools.kmd.gltf import ExportOptions, write_glb
from ngb_tools.kmd.model import KmdModel, parse_kmd
from ngb_tools.kmd.motion import MotionBank, parse_motion_bank
from ngb_tools.kmd.rig import find_rig, motion_clip
from ngb_tools.kmd.textures import resolve_texture_files
from ngb_tools.xpr.errors import XprError
from ngb_tools.xpr.texture import RgbaImage, decode_texture

IDLE_ID: Final = 1
TEXTURES_SUBDIR: Final = "textures"
TILE_BACKGROUND: Final[Color] = (28, 28, 38)
KNOWN_ERRORS: Final = (AfsError, KmdError, XprError, OSError, struct.error, ValueError)


class Status(StrEnum):
    OK = "ok"
    FAILED = "failed"


@dataclass(frozen=True, slots=True)
class GalleryEntry:
    """A row of the gallery."""

    character: str
    archive: str
    status: Status
    kmd: str | None = None
    objects: int = 0
    triangles: int = 0
    motion: str | None = None
    motion_note: str | None = None
    issues: int = 0
    glb: str | None = None
    reason: str | None = None


@dataclass(frozen=True, slots=True)
class CharacterResult:
    entry: GalleryEntry
    tile: bytes | None = field(default=None, repr=False)


def idle_id(bank: MotionBank) -> int:
    """Id 1 if it does not point to the filler motion of id 0; otherwise the first different one."""
    filler = bank.table[0]
    if len(bank.table) > IDLE_ID and bank.table[IDLE_ID] != filler:
        return IDLE_ID
    return next((i for i, offset in enumerate(bank.table) if offset != filler), 0)


def _idle_clip(
    model: KmdModel, bank_ref: EntryRef | None
) -> tuple[AnimationClip | None, str | None, str | None]:
    """(clip, motion description, reason if there is no animation)."""
    if bank_ref is None:
        return None, None, "no motion bank"
    try:
        bank = parse_motion_bank(bank_ref.read())
        rig = find_rig(model)
        motion_id = idle_id(bank)
        clip = motion_clip(rig, bank.motion(motion_id), f"{bank_ref.stem}#{motion_id}")
    except (KmdError, OSError) as error:
        return None, str(bank_ref), f"no animation: {error}"
    return clip, f"{bank_ref} id {motion_id}", None


def _textures_rgba(model: KmdModel) -> dict[int, RgbaImage]:
    images: dict[int, RgbaImage] = {}
    for ordinal, texture in enumerate(model.textures):
        try:
            images[ordinal] = decode_texture(model.xpr, texture)
        except XprError:
            continue
    return images


def _export(
    path: Path,
    entries: Sequence[EntryRef],
    kmd_ref: EntryRef,
    *,
    motions: MotionIndex,
    out_dir: Path,
    tile_size: tuple[int, int],
) -> CharacterResult:
    name = character_name(path)
    model = parse_kmd(kmd_ref.read())
    source = path.parent / f"{path.name}#{kmd_ref.index}_{kmd_ref.name}"
    files, _ = resolve_texture_files(model, source, out_dir / TEXTURES_SUBDIR / name)
    bank_ref = motions.find(entries, motion_candidates(name, kmd_ref.stem))
    clip, motion, note = _idle_clip(model, bank_ref)
    options = ExportOptions(skin=True, animations=(clip,) if clip is not None else ())
    glb, stats = write_glb(model, out_dir, name, files, options)
    tile = render_front(model_triangles(model), _textures_rgba(model), *tile_size, TILE_BACKGROUND)
    entry = GalleryEntry(
        character=name,
        archive=path.name,
        status=Status.OK,
        kmd=f"{kmd_ref.name} (#{kmd_ref.index})",
        objects=stats.objects,
        triangles=stats.triangles,
        motion=motion if clip is not None else None,
        motion_note=note,
        issues=len(model.issues),
        glb=glb.name,
    )
    return CharacterResult(entry, tile)


def build_character(
    path: Path, motions: MotionIndex, out_dir: Path, tile_size: tuple[int, int]
) -> CharacterResult:
    """Export one character; never raises: failures come back as an entry with a reason."""
    name = character_name(path)

    def failed(reason: str, kmd: str | None = None) -> CharacterResult:
        return CharacterResult(GalleryEntry(name, path.name, Status.FAILED, kmd=kmd, reason=reason))

    try:
        entries = list_entries(path)
    except (AfsError, OSError) as error:
        return failed(f"unreadable AFS: {error}")
    kmd_ref = main_kmd(entries, name)
    if kmd_ref is None:
        return failed("no KMD in the AFS")
    kmd = f"{kmd_ref.name} (#{kmd_ref.index})"
    try:
        return _export(
            path, entries, kmd_ref, motions=motions, out_dir=out_dir, tile_size=tile_size
        )
    except KNOWN_ERRORS as error:
        return failed(f"{type(error).__name__}: {error}", kmd)
    except Exception as error:
        return failed(f"internal error {type(error).__name__}: {error}", kmd)


def build_gallery(
    archives: Sequence[Path],
    motions: MotionIndex,
    out_dir: Path,
    tile_size: tuple[int, int],
    progress: Callable[[GalleryEntry], None] | None = None,
) -> list[CharacterResult]:
    """One result per AFS, in order. `out_dir` must already be checked."""
    out_dir.mkdir(parents=True, exist_ok=True)
    results: list[CharacterResult] = []
    for path in archives:
        result = build_character(path, motions, out_dir, tile_size)
        results.append(result)
        if progress is not None:
            progress(result.entry)
    return results
