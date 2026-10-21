"""Los PNG de las texturas de cada parte: los de `ngb_tools.xpr` si cuadran y, si no, se exportan.

Cada parte va a `$NGB_DATA/extracted/textures/<parte>` (`textures/boff_01`), con el mismo
`_manifest.json` que `python -m ngb_tools.xpr export --kmd`. El manifiesto solo se reutiliza
si describe este mismo XPR0: mismo número de texturas, mismo índice de recurso y mismo
sha256 de cada una, y su PNG existe. Si no, se vuelve a exportar con `export_textures`
(que también garantiza que la salida queda fuera de los repos).

TODO(kmd): es la lógica de `ngb_tools.kmd.textures.resolve_texture_files`, que pide un
`KmdModel`; con una versión que reciba el XPR0 y su posición, este módulo sobra.
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
    """PNG por número de textura y si se exportaron ahora."""

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
    """PNG de cada textura exportable de la parte; `origin` solo va al manifiesto."""
    found = _from_manifest(model, _read_manifest(directory), directory)
    if found is not None:
        return found
    source = XprSource(origin, Container.KMD, model.xpr_offset, model.xpr_slot, model.xpr)
    export_textures(source, directory)
    found = _from_manifest(model, _read_manifest(directory), directory)
    if found is None:
        raise XprError(f"el manifiesto recién exportado en {directory} no cuadra con el KMD")
    return TextureSet(found.files, exported=True)
