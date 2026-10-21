"""Los PNG de las texturas del modelo: los del hito H2 si cuadran y, si no, se exportan.

`python -m ngb_tools.xpr export --kmd` deja un PNG por textura y un `_manifest.json` en
`$NGB_DATA/extracted/textures/<archivo sin extensión>`. Ese manifiesto solo se reutiliza
si describe este mismo XPR0: mismo número de texturas, mismo índice de recurso y mismo
sha256 de los bytes de cada una. Si falta o no cuadra, se vuelve a exportar con
`ngb_tools.xpr.export` (que también garantiza que la salida queda fuera de los repos).
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
    """La carpeta donde `ngb_tools.xpr export` deja las texturas de este KMD."""
    return default_output_dir(kmd_path, environ)


def _texture_digest(model: KmdModel, ordinal: int) -> str | None:
    texture = model.textures[ordinal]
    try:
        size = texture_layout(texture).byte_size
        return hashlib.sha256(model.xpr.data_bytes(texture.data, size)).hexdigest()
    except XprError:
        return None


def _record_matches(model: KmdModel, ordinal: int, record: Any, directory: Path) -> bool:
    """El registro es de esta textura y, si se exportó, su PNG existe y el sha256 cuadra."""
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
    """PNG por número de textura si el manifiesto describe este XPR0; si no, None."""
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
    """PNG de cada textura exportable y si hubo que exportarlas ahora."""
    files = _manifest_files(model, _read_manifest(directory), directory)
    if files is not None:
        return files, False
    source = XprSource(kmd_path, Container.KMD, model.xpr_offset, model.xpr_slot, model.xpr)
    export_textures(source, directory)
    files = _manifest_files(model, _read_manifest(directory), directory)
    if files is None:
        raise XprError(f"el manifiesto recién exportado en {directory} no cuadra con el KMD")
    return files, True
