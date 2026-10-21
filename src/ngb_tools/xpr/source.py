"""Apertura de un XPR0: suelto (`.xpr`, `.spr`) o dentro del chunk de un KMD (`--kmd`).

En un KMD el XPR0 es uno de los slots del chunk de Team Ninja (ver `ngb_tools.teamninja`).
Lo que importa para el manifiesto es dónde empieza dentro del archivo (`offset`): los
campos `Data` son relativos a la región de datos del XPR0, no al archivo.

El archivo se lee una vez; el XPR0 es una vista (`memoryview`) sobre esos bytes, sin copia.
Si el archivo no es lo pedido (sin magia XPR0, o un chunk sin slot XPR0), el error es de
entrada (`XprInputError`); si es un XPR0 pero está roto, de formato (`XprFormatError`).
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import StrEnum
from pathlib import Path

from ngb_tools.teamninja import TeamNinjaError, find_xpr0_slot, parse_chunk
from ngb_tools.xpr.errors import XprInputError
from ngb_tools.xpr.resources import XPR_MAGIC, XprFile, parse_xpr


class Container(StrEnum):
    """Dónde venía el XPR0."""

    XPR = "xpr"
    KMD = "kmd"


@dataclass(frozen=True, slots=True)
class XprSource:
    """Un XPR0 ya parseado y su posición en el archivo de origen."""

    path: Path
    container: Container
    offset: int
    slot: int | None
    xpr: XprFile


def _chunk_hint(data: bytes) -> str:
    """Si los bytes parecen un chunk de Team Ninja, sugiere `--kmd`."""
    try:
        chunk = parse_chunk(data)
    except TeamNinjaError:
        return ""
    return f"; parece un chunk de Team Ninja '{chunk.tag}': prueba con --kmd"


def _open_kmd(path: Path, data: bytes) -> XprSource:
    try:
        slot = find_xpr0_slot(data)
    except TeamNinjaError as error:
        raise XprInputError(f"{path.name}: {error}") from error
    xpr = parse_xpr(memoryview(data)[slot.offset : slot.end])
    return XprSource(path, Container.KMD, slot.offset, slot.index, xpr)


def open_source(path: Path, *, kmd: bool) -> XprSource:
    """Lee `path` y parsea su XPR0; con `kmd`, lo busca entre los slots del chunk."""
    data = path.read_bytes()
    if kmd:
        return _open_kmd(path, data)
    if not data.startswith(XPR_MAGIC):
        raise XprInputError(f"{path.name}: no empieza por {XPR_MAGIC!r}{_chunk_hint(data)}")
    return XprSource(path, Container.XPR, 0, None, parse_xpr(data))
