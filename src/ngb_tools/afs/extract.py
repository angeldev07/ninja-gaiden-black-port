"""Extracción por streaming de entradas AFS, siempre fuera de cualquier repo git.

Los datos del juego tienen copyright y nunca deben acabar versionados, así que la ruta de
salida pasa por `ngb_tools._paths.ensure_outside_repos` (sin excepciones: ni `build/`).
Por defecto se extrae a `$NGB_DATA/extracted/<ruta del AFS dentro de game/, sin .afs>`.

Antes de escribir nada se valida la petición entera (formato, índices y límites); cada
archivo se escribe de forma atómica (`.part` y renombrado), así que un archivo con su
nombre final siempre está completo.
"""

from __future__ import annotations

import json
import re
from collections.abc import Iterable, Mapping, Sequence
from pathlib import Path, PurePosixPath
from typing import Final, TypedDict

from ngb_tools._paths import (
    EXTRACTED_SUBDIR,
    GAME_SUBDIR,
    atomic_write_bytes,
    atomic_write_chunks,
    data_root,
    ensure_outside_repos,
)
from ngb_tools.afs.archive import (
    AfsArchive,
    AfsEntry,
    iter_entry_chunks,
    open_afs,
    read_entry_prefix,
)
from ngb_tools.afs.classify import ENTROPY_WINDOW, classify
from ngb_tools.afs.errors import AfsTruncatedError, AfsUsageError

MANIFEST_NAME: Final = "_manifest.json"
UNNAMED: Final = "sin_nombre"
_BAD_CHARS: Final = re.compile(r'[<>:"/\\|?*\x00-\x1f]')


class ManifestEntry(TypedDict):
    index: int
    file: str
    name: str | None
    offset: int
    size: int
    timestamp: str | None
    custom: int | None
    kind: str


class ExtractManifest(TypedDict):
    source: str
    count: int
    attr_location: str
    entries: list[ManifestEntry]


# --- Rutas --------------------------------------------------------------------------------


def default_extract_dir(afs_path: Path, environ: Mapping[str, str] | None = None) -> Path:
    """Carpeta de extracción por defecto de `afs_path`, dentro de `$NGB_DATA/extracted/`.

    Se conserva la ruta relativa a `game/` (o, si no, a la raíz de datos) sin la extensión,
    para que dos AFS con el mismo nombre en subcarpetas no se pisen: `game/cmn.afs` va a
    `extracted/cmn` y `game/Media/cmn.afs`, a `extracted/Media/cmn`. Un AFS fuera de la raíz
    de datos usa solo su nombre.
    """
    root = data_root(environ)
    resolved = afs_path.resolve()
    for base in ((root / GAME_SUBDIR).resolve(), root.resolve()):
        if resolved.is_relative_to(base):
            return root / EXTRACTED_SUBDIR / resolved.relative_to(base).with_suffix("")
    return root / EXTRACTED_SUBDIR / afs_path.stem


def safe_filename(name: str) -> str:
    """Nombre válido en Windows: sin caracteres reservados ni punto o espacio final."""
    return _BAD_CHARS.sub("_", name).rstrip(" .") or UNNAMED


def entry_filename(entry: AfsEntry, guessed_ext: str) -> str:
    """`NNNN_<nombre>`; si el nombre no trae extensión, se añade la deducida."""
    base = safe_filename(entry.name) if entry.name else UNNAMED
    stem = f"{entry.index:04d}_{base}"
    return stem if PurePosixPath(base).suffix else f"{stem}.{guessed_ext}"


# --- Extracción ---------------------------------------------------------------------------


def _select(archive: AfsArchive, indices: Sequence[int] | None) -> list[AfsEntry]:
    """Entradas pedidas, sin repetir y en el orden pedido; las nulas no tienen datos."""
    if indices is None:
        wanted = list(archive.entries)
    else:
        unknown = [i for i in indices if not 0 <= i < archive.count]
        if unknown:
            raise AfsUsageError(f"índices fuera de rango 0..{archive.count - 1}: {unknown}")
        wanted = [archive.entries[i] for i in dict.fromkeys(indices)]
    return [e for e in wanted if not e.is_null]


def _check_bounds(archive: AfsArchive, entries: Iterable[AfsEntry]) -> None:
    cut = [f"#{e.index}" for e in entries if e.rel_offset + e.size > archive.limit]
    if cut:
        raise AfsTruncatedError(f"entradas más allá del final del archivo: {', '.join(cut)}")


def _write_manifest(out_dir: Path, manifest: ExtractManifest) -> None:
    text = json.dumps(manifest, indent=2, ensure_ascii=False) + "\n"
    # Pequeño: el fsync es barato y el manifiesto es lo que da por buena la extracción.
    atomic_write_bytes(out_dir / MANIFEST_NAME, text.encode("utf-8"), fsync=True)


def extract_archive(
    afs_path: Path, out_dir: Path, indices: Sequence[int] | None = None
) -> ExtractManifest:
    """Extrae las entradas (todas, o `indices`) a `out_dir` y escribe el manifiesto."""
    out = ensure_outside_repos(out_dir)
    with afs_path.open("rb") as source:
        archive = open_afs(source, afs_path.name)
        entries = _select(archive, indices)
        _check_bounds(archive, entries)
        out.mkdir(parents=True, exist_ok=True)
        records: list[ManifestEntry] = []
        for entry in entries:
            found = classify(read_entry_prefix(source, entry, ENTROPY_WINDOW))
            target = out / entry_filename(entry, found.ext)
            atomic_write_chunks(target, iter_entry_chunks(source, entry))
            records.append(
                ManifestEntry(
                    index=entry.index,
                    file=target.name,
                    name=entry.name,
                    offset=entry.rel_offset,
                    size=entry.size,
                    timestamp=entry.attr.timestamp if entry.attr is not None else None,
                    custom=entry.attr.custom if entry.attr is not None else None,
                    kind=found.kind,
                )
            )
    manifest = ExtractManifest(
        source=str(afs_path),
        count=archive.count,
        attr_location=archive.attr_ptr.location.value,
        entries=records,
    )
    _write_manifest(out, manifest)
    return manifest
