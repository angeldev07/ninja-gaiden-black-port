"""The PNGs of the model textures: the ones from milestone H2 if they match, else exported.

`python -m ngb_tools.xpr export --kmd` leaves one PNG per texture and a `_manifest.json` in
`$NGB_DATA/extracted/textures/<file name without extension>`. That manifest is only reused
if it describes this same XPR0: same number of textures, same resource index and same
sha256 of the bytes of each one. If it is missing or does not match, the textures are
exported again with `ngb_tools.xpr.export` (which also guarantees the output stays outside
the repos).
"""

from __future__ import annotations

import hashlib
import json
from collections.abc import Mapping
from pathlib import Path
from typing import Any

from ngb_tools.kmd.model import KmdModel
from ngb_tools.xpr.errors import XprError
from ngb_tools.xpr.export import MANIFEST_NAME, default_output_dir, export_textures
from ngb_tools.xpr.source import Container, XprSource
from ngb_tools.xpr.texture import texture_layout


def default_texture_dir(kmd_path: Path, environ: Mapping[str, str] | None = None) -> Path:
    """The folder where `ngb_tools.xpr export` leaves this KMD's textures."""
    return default_output_dir(kmd_path, environ)


def _texture_digest(model: KmdModel, ordinal: int) -> str | None:
    texture = model.textures[ordinal]
    try:
        size = texture_layout(texture).byte_size
        return hashlib.sha256(model.xpr.data_bytes(texture.data, size)).hexdigest()
    except XprError:
        return None


def _record_matches(model: KmdModel, ordinal: int, record: Any, directory: Path) -> bool:
    """The record belongs to this texture and, if exported, its PNG exists and sha256 matches."""
    if not isinstance(record, dict) or record.get("index") != ordinal:
        return False
    if record.get("resource_index") != model.textures[ordinal].index:
        return False
    name = record.get("file")
    if name is None:
        return True
    return (
        isinstance(name, str)
        and Path(name).name == name
        and record.get("sha256") == _texture_digest(model, ordinal)
        and (directory / name).is_file()
    )


def _manifest_files(model: KmdModel, manifest: Any, directory: Path) -> dict[int, Path] | None:
    """PNG per texture number if the manifest describes this XPR0; otherwise None."""
    records = manifest.get("textures") if isinstance(manifest, dict) else None
    if not isinstance(records, list) or len(records) != len(model.textures):
        return None
    if not all(_record_matches(model, k, record, directory) for k, record in enumerate(records)):
        return None
    return {k: directory / r["file"] for k, r in enumerate(records) if r.get("file") is not None}


def _read_manifest(directory: Path) -> Any:
    try:
        return json.loads((directory / MANIFEST_NAME).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None


def resolve_texture_files(
    model: KmdModel, kmd_path: Path, directory: Path
) -> tuple[dict[int, Path], bool]:
    """PNG of each exportable texture, and whether they had to be exported just now."""
    files = _manifest_files(model, _read_manifest(directory), directory)
    if files is not None:
        return files, False
    source = XprSource(kmd_path, Container.KMD, model.xpr_offset, model.xpr_slot, model.xpr)
    export_textures(source, directory)
    files = _manifest_files(model, _read_manifest(directory), directory)
    if files is None:
        raise XprError(f"the manifest just exported to {directory} does not match the KMD")
    return files, True
