"""Lector, extractor e inventariador de contenedores AFS (CRI Middleware).

Módulos, de abajo arriba:

- `archive`: estructuras y parseo de cabecera, TOC y atributos; lectura por streaming.
- `invariants`: auditoría de un AFS ya parseado (alineación, solapes, campo custom).
- `classify`: qué hay dentro de cada entrada (magias, chunks de Team Ninja, entropía).
- `records`: forma exacta (TypedDict) de los documentos JSON del inventario.
- `inventory`: fichas y agregados de todos los AFS de un directorio.
- `report`: presentación en texto (listado, Markdown y JSON).
- `extract`: extracción por streaming, siempre fuera de cualquier repo git.
- `cli`: `python -I -m ngb_tools.afs {list,extract,inventory}`.

Las rutas (raíz de datos, guarda de repos y escritura atómica) vienen de
`ngb_tools._paths`, compartido con las demás herramientas.

Aquí solo se reexporta la API de lectura; el resto se importa de su módulo.
"""

from __future__ import annotations

from ngb_tools.afs.archive import (
    MAGIC,
    MAGIC_VARIANT,
    AfsArchive,
    AfsEntry,
    AttrEntry,
    AttrLocation,
    AttrPointer,
    ByteSource,
    iter_entry_chunks,
    open_afs,
    read_entry_prefix,
)
from ngb_tools.afs.errors import (
    AfsError,
    AfsFormatError,
    AfsTruncatedError,
    AfsUsageError,
    UnsafeOutputError,
)

__all__ = [
    "MAGIC",
    "MAGIC_VARIANT",
    "AfsArchive",
    "AfsEntry",
    "AfsError",
    "AfsFormatError",
    "AfsTruncatedError",
    "AfsUsageError",
    "AttrEntry",
    "AttrLocation",
    "AttrPointer",
    "ByteSource",
    "UnsafeOutputError",
    "iter_entry_chunks",
    "open_afs",
    "read_entry_prefix",
]
