"""The texture PNGs of each part: the `ngb_tools.xpr` ones if they match, else exported.

Each part goes to `$NGB_DATA/extracted/textures/<part>` (`textures/boff_01`), with the same
`_manifest.json` as `python -m ngb_tools.xpr export --kmd`. The manifest is only reused if it
describes this same XPR0: same number of textures, same resource index and same sha256 for
each one, and its PNG exists. Otherwise it is exported again with `export_textures` (which
also guarantees the output stays outside the repos).

TODO(kmd): this is the logic of `ngb_tools.kmd.textures.resolve_texture_files`, which takes
a `KmdModel`; with a version that takes the XPR0 and its position, this module becomes
redundant.
"""

from __future__ import annotations

import hashlib
import json
from collections.abc import Mapping
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Final

from ngb_tools._paths import EXTRACTED_SUBDIR, data_root
from ngb_tools.stage.model import StageModel
from ngb_tools.xpr.errors import XprError
from ngb_tools.xpr.export import MANIFEST_NAME, export_textures
from ngb_tools.xpr.source import Container, XprSource
from ngb_tools.xpr.texture import texture_layout

TEXTURES_SUBDIR: Final = "textures"


@dataclass(frozen=True, slots=True)
class TextureSet:
    """PNG per texture number and whether they were exported just now."""

    files: Mapping[int, Path]
    exported: bool


def texture_dir(part_name: str, environ: Mapping[str, str] | None = None) -> Path:
    return data_root(environ) / EXTRACTED_SUBDIR / TEXTURES_SUBDIR / part_name


def _digest(model: StageModel, ordinal: int) -> str | None:
    texture = model.textures[ordinal]
    try:
        size = texture_layout(texture).byte_size
        return hashlib.sha256(model.xpr.data_bytes(texture.data, size)).hexdigest()
    except XprError:
        return None


def _record_matches(model: StageModel, ordinal: int, record: Any, directory: Path) -> bool:
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
        and record.get("sha256") == _digest(model, ordinal)
        and (directory / name).is_file()
    )


def _from_manifest(model: StageModel, manifest: Any, directory: Path) -> TextureSet | None:
    records = manifest.get("textures") if isinstance(manifest, dict) else None
    if not isinstance(records, list) or len(records) != len(model.textures):
        return None
    if not all(_record_matches(model, k, r, directory) for k, r in enumerate(records)):
        return None
    files = {k: directory / r["file"] for k, r in enumerate(records) if r.get("file")}
    return TextureSet(files, exported=False)


def _read_manifest(directory: Path) -> Any:
    try:
        return json.loads((directory / MANIFEST_NAME).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None


def resolve_textures(model: StageModel, origin: Path, directory: Path) -> TextureSet:
    """PNG of each exportable texture of the part; `origin` only goes into the manifest."""
    found = _from_manifest(model, _read_manifest(directory), directory)
    if found is not None:
        return found
    source = XprSource(origin, Container.KMD, model.xpr_offset, model.xpr_slot, model.xpr)
    export_textures(source, directory)
    found = _from_manifest(model, _read_manifest(directory), directory)
    if found is None:
        raise XprError(f"the manifest just exported to {directory} does not match the KMD")
    return TextureSet(found.files, exported=True)
