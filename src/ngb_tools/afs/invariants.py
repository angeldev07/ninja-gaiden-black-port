"""Auditoría de un AFS ya parseado: lo que un AFS sano cumple y este archivo no.

Las reglas salen de cómo CRI empaqueta los datos para el DVD: cada entrada empieza en
un sector (0x800), después de la cabecera, dentro del archivo y sin pisar a otra; la
tabla de atributos ocupa exactamente N x 0x30 bytes y también va alineada.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import StrEnum
from typing import NamedTuple

from ngb_tools.afs.archive import (
    ATTR_ENTRY_SIZE,
    MAGIC_VARIANT,
    SECTOR,
    AfsArchive,
    ByteSource,
    read_at,
)


class IssueCode(StrEnum):
    """Tipo de incidencia; el valor es el código estable que aparece en los informes."""

    MAGIC_VARIANT = "magic_variant"
    MISALIGNED = "misaligned"
    INSIDE_HEADER = "inside_header"
    OUT_OF_BOUNDS = "out_of_bounds"
    ATTR_SIZE = "attr_size"
    ATTR_MISALIGNED = "attr_misaligned"
    BAD_DATE = "bad_date"
    OVERLAP = "overlap"
    UNREADABLE = "unreadable"
    NESTED_REVISIT = "nested_revisit"  # un AFS anidado ya inventariado en el mismo archivo
    ENTRY_BUDGET = "entry_budget"  # se agotó el tope de fichas por archivo
    METADATA_BUDGET = "metadata_budget"  # se agotó el tope de bytes de metadatos de los hijos


@dataclass(frozen=True, slots=True)
class Issue:
    """Una incidencia; `index` es la entrada afectada, o None si es del archivo entero."""

    code: IssueCode
    detail: str
    index: int | None = None

    def __str__(self) -> str:
        where = "" if self.index is None else f" #{self.index}"
        return f"{self.code}{where}: {self.detail}"


class _Span(NamedTuple):
    """Rango [start, end) relativo al AFS; owner None = la tabla de atributos."""

    start: int
    end: int
    owner: int | None

    @property
    def label(self) -> str:
        return "attr" if self.owner is None else str(self.owner)


def _entry_issues(archive: AfsArchive) -> list[Issue]:
    issues: list[Issue] = []
    for e in archive.data_entries():
        if e.rel_offset % SECTOR:
            detail = f"offset {e.rel_offset:#x} no múltiplo de {SECTOR:#x}"
            issues.append(Issue(IssueCode.MISALIGNED, detail, e.index))
        if e.rel_offset < archive.header_end:
            detail = f"offset {e.rel_offset:#x} < fin de cabecera {archive.header_end:#x}"
            issues.append(Issue(IssueCode.INSIDE_HEADER, detail, e.index))
        if e.rel_offset + e.size > archive.limit:
            detail = f"{e.rel_offset:#x}+{e.size:#x} > {archive.limit:#x}"
            issues.append(Issue(IssueCode.OUT_OF_BOUNDS, detail, e.index))
    return issues


def _attr_issues(archive: AfsArchive) -> list[Issue]:
    if not archive.has_names:
        return []
    ptr, expected = archive.attr_ptr, archive.count * ATTR_ENTRY_SIZE
    issues: list[Issue] = []
    if ptr.size != expected:
        detail = f"tabla {ptr.size:#x} != N*0x30 = {expected:#x}"
        issues.append(Issue(IssueCode.ATTR_SIZE, detail))
    if ptr.offset % SECTOR:
        issues.append(Issue(IssueCode.ATTR_MISALIGNED, f"tabla en {ptr.offset:#x}"))
    for e in archive.data_entries():
        # Una fecha toda a cero es "sin fecha", no una fecha corrupta.
        if e.attr is not None and e.attr.timestamp is None and any(e.attr.date):
            issues.append(Issue(IssueCode.BAD_DATE, f"fecha inválida {e.attr.date}", e.index))
    return issues


def _overlap_issues(archive: AfsArchive) -> list[Issue]:
    spans = [_Span(e.rel_offset, e.rel_offset + e.size, e.index) for e in archive.data_entries()]
    if archive.has_names:
        ptr = archive.attr_ptr
        spans.append(_Span(ptr.offset, ptr.offset + ptr.size, None))
    spans.sort(key=lambda s: (s.start, s.end))
    # Barrido: cada rango se compara con el que más lejos llega de los anteriores, no solo
    # con su vecino. Así una entrada que envuelve a varias solapa con todas ellas.
    issues: list[Issue] = []
    reach: _Span | None = None
    for span in spans:
        if reach is not None and span.start < reach.end:
            detail = (
                f"{reach.label} [{reach.start:#x},{reach.end:#x}) solapa con {span.label} "
                f"en {span.start:#x}"
            )
            issues.append(Issue(IssueCode.OVERLAP, detail, span.owner))
        if reach is None or span.end > reach.end:
            reach = span
    return issues


def validate_archive(archive: AfsArchive) -> tuple[Issue, ...]:
    """Todas las incidencias del AFS, en un orden estable (cabecera, entradas, tabla, solapes)."""
    head = (
        [Issue(IssueCode.MAGIC_VARIANT, "magia 'AFS ' (0x20)")]
        if archive.magic == MAGIC_VARIANT
        else []
    )
    return (*head, *_entry_issues(archive), *_attr_issues(archive), *_overlap_issues(archive))


@dataclass(frozen=True, slots=True)
class CustomFieldStats:
    """Recuento de hipótesis sobre el u32 `custom` (no son excluyentes entre sí)."""

    equals_header_u32: int
    equals_size: int
    neither: int


def custom_field_semantics(source: ByteSource, archive: AfsArchive) -> CustomFieldStats:
    """Compara el `custom` de cada entrada con su tamaño y con el u32 del offset 4 + 4*i.

    La cabecera completa (8 + 8*N bytes) siempre cubre esos 4 + 4*N bytes, así que la
    lectura no puede quedarse corta en un AFS que se haya podido abrir.
    """
    if not archive.has_names:
        return CustomFieldStats(0, 0, 0)
    raw = read_at(source, archive.base, 4 + 4 * archive.count)
    header_hits = size_hits = neither = 0
    for e in archive.entries:
        if e.attr is None:
            continue
        pos = 4 + 4 * e.index
        hit_header = e.attr.custom == int.from_bytes(raw[pos : pos + 4], "little")
        hit_size = e.attr.custom == e.size
        header_hits += hit_header
        size_hits += hit_size
        neither += not (hit_header or hit_size)
    return CustomFieldStats(header_hits, size_hits, neither)
