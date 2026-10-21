"""Estructuras y parseo de contenedores AFS (CRI Middleware) sin cargar sus datos.

Formato (little endian; contrastado con AFSLib de MaikelChan y AfsLib de Sewer56)::

    0x00  char[4]  magia "AFS\\0" (algunos juegos usan "AFS " = 0x20)
    0x04  u32      N = número de entradas
    0x08  N x {u32 offset, u32 size}   TOC; (0, 0) = entrada nula
    ...   {u32 offset, u32 size}       puntero a la tabla de atributos, en una de dos
                                       posiciones: justo tras la TOC (0x08 + N*8) o en
                                       los 8 bytes previos al primer dato. Puede faltar.

    Atributos: N x 0x30 bytes = name[32] (ASCII, relleno NUL), 6 x u16 de fecha
               (año, mes, día, hora, minuto, segundo) y un u32 "custom".
    Datos alineados a 0x800 (el sector del DVD).

Los offsets de la TOC son relativos al inicio del AFS. `AfsEntry.offset` es absoluto en
el archivo físico, así un AFS anidado se lee en su sitio, sin copiarlo.
"""

from __future__ import annotations

import datetime as dt
import os
import struct
from collections.abc import Iterator, Sequence
from dataclasses import dataclass
from enum import StrEnum
from typing import Final, Protocol

from ngb_tools.afs.errors import AfsFormatError, AfsTruncatedError, AfsUsageError

MAGIC: Final = b"AFS\x00"
MAGIC_VARIANT: Final = b"AFS\x20"
HEADER_SIZE: Final = 8  # magia + N
TOC_ENTRY_SIZE: Final = 8  # offset + size
ATTR_PTR_SIZE: Final = 8  # offset + size de la tabla de atributos
ATTR_ENTRY_SIZE: Final = 0x30  # name[32] + 6 x u16 + u32
ATTR_NAME_SIZE: Final = 32
SECTOR: Final = 0x800
CHUNK_SIZE: Final = 1 << 20

_HEADER: Final = struct.Struct("<4sI")
_PAIR: Final = struct.Struct("<II")  # (offset, size): entrada de TOC y puntero de atributos
_ATTR: Final = struct.Struct(f"<{ATTR_NAME_SIZE}s6HI")

Date = tuple[int, int, int, int, int, int]


class ByteSource(Protocol):
    """Lo único que el parser pide a un archivo: posicionarse y leer bytes."""

    def seek(self, offset: int, whence: int = 0, /) -> int: ...

    def read(self, size: int = -1, /) -> bytes: ...


class AttrLocation(StrEnum):
    """Dónde apareció el puntero a la tabla de atributos."""

    AFTER_TOC = "after_toc"
    BEFORE_DATA = "before_data"
    NONE = "none"


@dataclass(frozen=True, slots=True)
class AttrPointer:
    """Puntero (offset, size) a la tabla de atributos, relativo al inicio del AFS."""

    location: AttrLocation
    offset: int = 0
    size: int = 0


NO_ATTR_POINTER: Final = AttrPointer(AttrLocation.NONE)


@dataclass(frozen=True, slots=True)
class AttrEntry:
    """Entrada de 0x30 bytes de la tabla de atributos.

    En Ninja Gaiden Black `custom` no es el tamaño, como suponen otras herramientas: es
    la copia del u32 en el offset 4 + 4*i del propio AFS (la cabecera leída como u32[]).
    """

    name: str
    date: Date
    custom: int

    @property
    def timestamp(self) -> str | None:
        """Fecha en ISO 8601, o None si los seis campos no forman una fecha real."""
        try:
            return dt.datetime(*self.date).isoformat(sep=" ")
        except ValueError:
            return None


@dataclass(frozen=True, slots=True)
class AfsEntry:
    """Entrada de la TOC. `offset` es absoluto; `rel_offset`, el valor tal cual de la TOC."""

    index: int
    offset: int
    size: int
    rel_offset: int
    attr: AttrEntry | None = None

    @property
    def is_null(self) -> bool:
        """Una entrada (0, 0) reserva el índice sin ocupar datos."""
        return self.rel_offset == 0 and self.size == 0

    @property
    def name(self) -> str | None:
        return self.attr.name if self.attr is not None and self.attr.name else None


@dataclass(frozen=True, slots=True)
class AfsArchive:
    """AFS parseado (solo metadatos). `base` > 0 si vive dentro de otro archivo."""

    label: str
    base: int
    limit: int
    magic: bytes
    count: int
    attr_ptr: AttrPointer
    entries: tuple[AfsEntry, ...]

    @property
    def toc_end(self) -> int:
        """Fin relativo de la TOC: primera posición candidata del puntero de atributos."""
        return HEADER_SIZE + self.count * TOC_ENTRY_SIZE

    @property
    def header_end(self) -> int:
        """Fin de cabecera + TOC + puntero: ningún dato puede empezar antes."""
        return self.toc_end + ATTR_PTR_SIZE

    @property
    def has_names(self) -> bool:
        return self.attr_ptr.location is not AttrLocation.NONE

    def data_entries(self) -> tuple[AfsEntry, ...]:
        """Las entradas que ocupan bytes (sin las nulas)."""
        return tuple(e for e in self.entries if not e.is_null)


def read_at(source: ByteSource, pos: int, size: int) -> bytes:
    """Hasta `size` bytes desde la posición absoluta `pos` (menos si el archivo acaba)."""
    source.seek(pos)
    return source.read(size)


def parse_header(raw: bytes) -> tuple[bytes, int]:
    """Cabecera de 8 bytes → (magia, N)."""
    if len(raw) < HEADER_SIZE:
        raise AfsTruncatedError("cabecera truncada")
    header: tuple[bytes, int] = _HEADER.unpack_from(raw)
    magic, count = header
    if magic not in (MAGIC, MAGIC_VARIANT):
        raise AfsFormatError(f"magia inválida {magic!r}")
    return magic, count


def _pair_at(raw: bytes, offset: int) -> tuple[int, int]:
    pair: tuple[int, int] = _PAIR.unpack_from(raw, offset)
    return pair


def parse_toc(raw: bytes, count: int) -> tuple[tuple[int, int], ...]:
    """TOC de N x (offset, size)."""
    if len(raw) < count * TOC_ENTRY_SIZE:
        raise AfsTruncatedError("TOC truncada")
    return tuple(_pair_at(raw, i * TOC_ENTRY_SIZE) for i in range(count))


def parse_attr_entry(raw: bytes, offset: int = 0) -> AttrEntry:
    """Entrada de atributos en `raw[offset:]`: name[32], 6 x u16 de fecha, u32 custom."""
    fields: tuple[bytes, int, int, int, int, int, int, int] = _ATTR.unpack_from(raw, offset)
    name_raw, year, month, day, hour, minute, second, custom = fields
    name = name_raw.split(b"\x00", 1)[0].decode("ascii", "backslashreplace")
    return AttrEntry(name=name, date=(year, month, day, hour, minute, second), custom=custom)


def attr_pointer_is_valid(offset: int, size: int, count: int, min_offset: int, limit: int) -> bool:
    """Criterio de AFSLib: no nulo, tras la cabecera, dentro del AFS y con sitio para N entradas."""
    return (
        offset > 0
        and size > 0
        and offset >= min_offset
        and offset + size <= limit
        and size >= count * ATTR_ENTRY_SIZE
    )


def locate_attr_pointer(
    source: ByteSource, base: int, count: int, toc: Sequence[tuple[int, int]], limit: int
) -> AttrPointer:
    """Prueba el puntero tras la TOC y, si no vale, el de los 8 bytes previos al primer dato."""
    toc_end = HEADER_SIZE + count * TOC_ENTRY_SIZE
    candidates = [(AttrLocation.AFTER_TOC, toc_end)]
    first_data = min((offset for offset, _ in toc if offset), default=0)
    if first_data >= toc_end + ATTR_PTR_SIZE:
        candidates.append((AttrLocation.BEFORE_DATA, first_data - ATTR_PTR_SIZE))
    for location, pos in candidates:
        if pos + ATTR_PTR_SIZE > limit:
            continue
        raw = read_at(source, base + pos, ATTR_PTR_SIZE)
        if len(raw) < ATTR_PTR_SIZE:
            continue
        offset, size = _pair_at(raw, 0)
        if attr_pointer_is_valid(offset, size, count, toc_end + ATTR_PTR_SIZE, limit):
            return AttrPointer(location, offset, size)
    return NO_ATTR_POINTER


def read_attr_table(
    source: ByteSource, base: int, ptr: AttrPointer, count: int
) -> tuple[AttrEntry, ...]:
    """Las N entradas de atributos a las que apunta `ptr`."""
    size = count * ATTR_ENTRY_SIZE
    raw = read_at(source, base + ptr.offset, size)
    if len(raw) < size:
        raise AfsTruncatedError(f"tabla de atributos truncada en {ptr.offset:#x}")
    return tuple(parse_attr_entry(raw, i * ATTR_ENTRY_SIZE) for i in range(count))


def open_afs(
    source: ByteSource, label: str, *, base: int = 0, limit: int | None = None
) -> AfsArchive:
    """Parsea cabecera, TOC y atributos del AFS que empieza en `base`, sin leer los datos.

    `limit` es el tamaño del AFS; por defecto, desde `base` hasta el final del archivo.
    """
    if limit is None:
        limit = source.seek(0, os.SEEK_END) - base
    magic, count = parse_header(read_at(source, base, HEADER_SIZE))
    toc_size = count * TOC_ENTRY_SIZE
    if HEADER_SIZE + toc_size > limit:
        raise AfsTruncatedError(f"TOC de {count} entradas no cabe en {limit} bytes")
    toc = parse_toc(read_at(source, base + HEADER_SIZE, toc_size), count)
    ptr = locate_attr_pointer(source, base, count, toc, limit)
    attrs: Sequence[AttrEntry | None] = (
        read_attr_table(source, base, ptr, count)
        if ptr.location is not AttrLocation.NONE
        else (None,) * count
    )
    entries = tuple(
        AfsEntry(index, base + offset, size, offset, attr)
        for index, ((offset, size), attr) in enumerate(zip(toc, attrs, strict=True))
    )
    return AfsArchive(label, base, limit, magic, count, ptr, entries)


def read_entry_prefix(source: ByteSource, entry: AfsEntry, size: int) -> bytes:
    """Los primeros `size` bytes de la entrada (o menos, si la entrada es más corta)."""
    return read_at(source, entry.offset, min(size, entry.size))


def iter_entry_chunks(
    source: ByteSource, entry: AfsEntry, chunk_size: int = CHUNK_SIZE
) -> Iterator[bytes]:
    """Recorre el contenido de la entrada en bloques, sin cargarla entera en memoria.

    Cada bloque se lee con su propio `seek`, así que intercalar otras lecturas del mismo
    `source` mientras se consume el iterador no desordena los datos.
    """
    if chunk_size <= 0:
        raise AfsUsageError(f"chunk_size debe ser positivo, no {chunk_size}")
    pos, remaining = entry.offset, entry.size
    while remaining > 0:
        block = read_at(source, pos, min(chunk_size, remaining))
        if not block:
            raise AfsTruncatedError(f"entrada {entry.index} truncada: faltan {remaining} bytes")
        pos += len(block)
        remaining -= len(block)
        yield block
