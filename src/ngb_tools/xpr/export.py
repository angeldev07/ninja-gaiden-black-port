"""Export of the textures of an XPR0 to PNG, with a `_manifest.json` to trace them.

Level 0 of each exportable texture is exported, one PNG per texture. The manifest lists
all of them, including the skipped ones and the reason, with what is needed to get back to
the source bytes: offset of the `Data` field, absolute offset in the file, mip levels and the
sha256 of the texture bytes (all its mips) and of its palette.

Textures are game data (copyrighted): the output goes through
`ngb_tools._paths.ensure_outside_repos` with no exceptions (not even `build/`) and by default
goes to `$NGB_DATA/extracted/textures/<file without extension>`.

Order of work: the "outside the repo" guard goes first and then the whole catalog is
validated without decoding. Then each texture is decoded and written atomically, one at a
time (only one is in memory), and the manifest goes last, with fsync: it is what makes
the export count as good. A mid-way failure leaves neither truncated PNGs nor a manifest
describing files that do not exist.

Each exported texture carries `encoding`: `color`, or `signed-bump` if the PNG is a signed
bump map (V8U8, L6V5U5) converted to RGBA with the mapping of `ngb_tools.xpr.bump`. In
L6V5U5, `alpha` is `luminance`: the A channel is luminance, not transparency.

Several headers can point to the same data, so a small file could request thousands of
4096x4096 textures. `pixel_budget` (by default `MAX_EXPORT_PIXELS`) bounds the pixels
decoded per export; textures that do not fit are skipped with their reason.

PNG names come only from the sequence number, the format table and the dimensions
(`0004_DXT1_256x256.png`): no byte of the file reaches a path.
"""

from __future__ import annotations

import hashlib
import json
from collections.abc import Mapping
from dataclasses import replace
from pathlib import Path
from typing import Final, Literal, TypedDict

from ngb_tools._paths import EXTRACTED_SUBDIR, atomic_write_bytes, data_root, ensure_outside_repos
from ngb_tools.xpr.catalog import CatalogEntry, catalog
from ngb_tools.xpr.errors import UnsupportedTextureError, XprFormatError
from ngb_tools.xpr.formats import TextureFormat, lookup_format
from ngb_tools.xpr.png import encode_png
from ngb_tools.xpr.source import XprSource
from ngb_tools.xpr.texture import AlphaMode, alpha_mode, decode_texture

MANIFEST_NAME: Final = "_manifest.json"
TEXTURES_SUBDIR: Final = "textures"
TOOL: Final = "ngb_tools.xpr"
UNNAMED: Final = "unnamed"
COLOR: Final = "color"
SIGNED_BUMP: Final = "signed-bump"
# Level-0 pixels decoded per export: one 4096x4096 texture. Measured cost
# (Python 3.13, one core): DXT1 2.8 Mpx/s, DXT5 1.6, ARGB 6.5, P8 and A8 9; level-9 PNG
# costs 0.15 to 4 s per Mpx depending on the data. 16 Mpx is 10 s of DXT5 and, with the
# worst PNG, a little over a minute (64 Mpx would be about 4 min). Real data: 873,728 px in
# ryu.kmd, 1.57 Mpx in dlcustom.xpr and 5.9 Mpx in the game's largest XPR0 (suiro_21.afs #0).
MAX_EXPORT_PIXELS: Final = 4096 * 4096


class LevelRecord(TypedDict):
    level: int
    width: int
    height: int
    offset: int
    size: int
    present: bool


class PaletteRecord(TypedDict):
    resource_index: int
    entries: int
    data_offset: int
    file_offset: int
    sha256: str
    source: str


class TextureRecord(TypedDict):
    index: int
    resource_index: int
    status: Literal["exported", "skipped"]
    file: str | None
    issue: str | None
    format_code: int
    format: str
    storage: str | None
    width: int
    height: int
    depth: int
    mips: int
    cubemap: bool
    data_offset: int
    file_offset: int
    byte_length: int | None
    sha256: str | None
    levels: list[LevelRecord]
    palette: PaletteRecord | None
    alpha: str | None
    encoding: str | None
    warnings: list[str]


class ExportManifest(TypedDict):
    tool: str
    source: str
    container: str
    kmd_slot: int | None
    xpr_offset: int
    xpr_total_size: int
    xpr_header_size: int
    exported: int
    skipped: int
    textures: list[TextureRecord]


def default_output_dir(source: Path, environ: Mapping[str, str] | None = None) -> Path:
    """`<data>/extracted/textures/<source name without extension>`.

    A name made only of dots or spaces (`..`, `..kmd`) is replaced: with it the
    folder would go up a level or collapse into `textures/`.
    """
    stem = source.stem if source.stem.strip(" .") else UNNAMED
    return data_root(environ) / EXTRACTED_SUBDIR / TEXTURES_SUBDIR / stem


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _file_offset(source: XprSource, data_offset: int) -> int:
    """From a `Data` field (relative to header_size) to an absolute offset in the file."""
    return source.offset + source.xpr.header_size + data_offset


def _palette_record(source: XprSource, entry: CatalogEntry) -> PaletteRecord | None:
    link = entry.check.palette if entry.check is not None else None
    if link is None:
        return None
    palette = link.palette
    return PaletteRecord(
        resource_index=palette.index,
        entries=palette.entries,
        data_offset=palette.data,
        file_offset=_file_offset(source, palette.data),
        sha256=_sha256(source.xpr.data_bytes(palette.data, palette.byte_size)),
        source=link.source.value,
    )


def _levels(entry: CatalogEntry) -> list[LevelRecord]:
    if entry.layout is None:
        return []
    present = entry.check.present_levels if entry.check is not None else 0
    return [
        LevelRecord(
            level=m.level,
            width=m.width,
            height=m.height,
            offset=m.offset,
            size=m.size,
            present=m.level < present,
        )
        for m in entry.layout.levels
    ]


def _encoding(fmt: TextureFormat | None) -> str:
    """What the PNG means: color, or a signed bump map (`ngb_tools.xpr.bump`)."""
    return SIGNED_BUMP if fmt is not None and fmt.is_signed_bump else COLOR


def _record(
    source: XprSource, entry: CatalogEntry, issue: str | None, alpha: AlphaMode | None = None
) -> TextureRecord:
    """Record of one texture; `issue` is the reason it is not exported, if any."""
    texture, check = entry.texture, entry.check
    fmt = lookup_format(texture.format.code)
    exported = issue is None and check is not None
    byte_length = check.present_bytes if exported and check is not None else None
    digest = None
    if byte_length is not None:
        digest = _sha256(source.xpr.data_bytes(texture.data, byte_length))
    return TextureRecord(
        index=entry.ordinal,
        resource_index=texture.index,
        status="exported" if exported else "skipped",
        file=entry.filename if exported else None,
        issue=issue,
        format_code=texture.format.code,
        format=entry.format_name,
        storage=fmt.storage.value if fmt is not None else None,
        width=entry.width,
        height=entry.height,
        depth=texture.format.depth if texture.format.dimensions == 3 else 1,
        mips=texture.format.levels,
        cubemap=texture.format.cubemap,
        data_offset=texture.data,
        file_offset=_file_offset(source, texture.data),
        byte_length=byte_length,
        sha256=digest,
        levels=_levels(entry),
        palette=_palette_record(source, entry),
        alpha=alpha.value if alpha is not None and exported else None,
        encoding=_encoding(fmt) if exported else None,
        warnings=entry.warnings if exported else [],
    )


def _render(source: XprSource, entry: CatalogEntry) -> tuple[TextureRecord, bytes | None]:
    """Manifest record and PNG (if exported) for a catalog entry.

    A failure while decoding or encoding the PNG skips only that texture, with its reason.
    """
    if entry.issue is not None or entry.layout is None:
        return _record(source, entry, entry.issue), None
    try:
        image = decode_texture(source.xpr, entry.texture)
        png = encode_png(image.width, image.height, image.pixels)
    except (UnsupportedTextureError, XprFormatError) as error:
        return _record(source, entry, str(error)), None
    except ValueError as error:  # encode_png: should not happen with a validated image
        return _record(source, entry, f"PNG: {error}"), None
    mode = alpha_mode(entry.layout.format, image.pixels)
    return _record(source, entry, None, mode), png


def export_textures(
    source: XprSource, out_dir: Path, *, pixel_budget: int = MAX_EXPORT_PIXELS
) -> ExportManifest:
    """Write one PNG per exportable texture and the manifest in `out_dir`.

    Raises `UnsafeOutputError` (from `_paths`) before creating anything if `out_dir` falls
    inside a git repo.
    """
    out = ensure_outside_repos(out_dir)
    entries = catalog(source.xpr)
    out.mkdir(parents=True, exist_ok=True)
    records: list[TextureRecord] = []
    remaining = pixel_budget
    for listed in entries:
        entry = listed
        pixels = entry.width * entry.height
        if entry.issue is None and pixels > remaining:
            issue = f"budget of {pixel_budget} pixels per export exhausted"
            entry = replace(entry, issue=issue)
        record, png = _render(source, entry)
        records.append(record)
        if png is not None and record["file"] is not None:
            atomic_write_bytes(out / record["file"], png)
            remaining -= pixels
    exported = sum(1 for record in records if record["status"] == "exported")
    manifest = ExportManifest(
        tool=TOOL,
        source=str(source.path.resolve()),
        container=source.container.value,
        kmd_slot=source.slot,
        xpr_offset=source.offset,
        xpr_total_size=source.xpr.total_size,
        xpr_header_size=source.xpr.header_size,
        exported=exported,
        skipped=len(records) - exported,
        textures=records,
    )
    text = json.dumps(manifest, indent=2, ensure_ascii=False) + "\n"
    # The manifest makes the export count as good: it goes last and with fsync.
    atomic_write_bytes(out / MANIFEST_NAME, text.encode("utf-8"), fsync=True)
    return manifest
