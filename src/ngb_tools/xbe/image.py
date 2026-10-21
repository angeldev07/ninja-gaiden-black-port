"""Imagen XBE cargada: secciones, mapa VA -> archivo, claves XOR y digests.

Al arrancar un XBE, el kernel copia la cabecera a `base_address` y cada sección
de `raw_addr` a `virtual_addr`; lo que la sección ocupa en memoria por encima de
sus datos crudos (`virtual_size > raw_size`) es BSS y se rellena con ceros.
`AddressMap` reproduce esa traducción al revés: dada una VA, qué offset del
archivo la respalda (si alguno).

Un XBE puede ser hostil o estar corrupto, así que el layout se valida antes de
recorrerlo: lo imposible (tabla de secciones fuera de la cabecera, rangos que
desbordan los 32 bits o se salen de la imagen, contadores desorbitados) es un
`ImageLayoutError`; lo sospechoso pero analizable (secciones solapadas, claves
XOR ambiguas, digests que no cuadran) son problemas de validación.
"""

from __future__ import annotations

import hashlib
from dataclasses import dataclass, field
from enum import StrEnum
from itertools import combinations
from pathlib import Path
from typing import Final

from ngb_tools.xbe.binary import (
    DEFAULT_C_STRING_MAX,
    decode_ascii,
    decode_utf16z,
    read_c_bytes,
)
from ngb_tools.xbe.errors import ImageLayoutError, TruncatedDataError, UnmappedAddressError
from ngb_tools.xbe.structs import SECTION_HEADER_SIZE, ImageHeader, SectionHeader

HEADERS_REGION = "<headers>"
SECTION_NAME_MAX = 64
UNKNOWN_SECTION_NAME = "?"
DEFAULT_UTF16_MAX_CHARS = 512
ADDRESS_SPACE: Final = 1 << 32  # el Xbox es x86 de 32 bits
# Cota del número de secciones. Los XBE reales tienen unas decenas (default.xbe de NGB, 16;
# los de la actualización del dashboard, 27); con más de 256 la tabla no es real, y cada
# entrada cuesta un SHA-1 de sus datos.
MAX_SECTIONS: Final = 256


class BuildKind(StrEnum):
    """Tipo de build según qué juego de claves XOR des-ofusca la cabecera."""

    RETAIL = "retail"
    DEBUG = "debug"
    BETA = "beta"
    CHIHIRO = "chihiro"  # placa arcade de Sega basada en Xbox
    UNKNOWN = "unknown"


@dataclass(frozen=True, slots=True)
class XorKeys:
    """Claves con que se ofuscan el entry point y el kernel thunk de un tipo de build."""

    kind: BuildKind
    entry_point: int
    kernel_thunk: int


# retail/debug/beta: xboxdevwiki; chihiro: Cxbx-Reloaded.
XOR_KEYS: tuple[XorKeys, ...] = (
    XorKeys(BuildKind.RETAIL, 0xA8FC57AB, 0x5B6D40B6),
    XorKeys(BuildKind.DEBUG, 0x94859D4B, 0xEFB1F152),
    XorKeys(BuildKind.BETA, 0xE682F45B, 0x46437DCD),
    XorKeys(BuildKind.CHIHIRO, 0x40B5C16E, 0x2290059D),
)


@dataclass(frozen=True, slots=True)
class XorCandidate:
    """Resultado de des-ofuscar con un juego de claves."""

    kind: BuildKind
    entry_point: int
    kernel_thunk_addr: int
    valid: bool  # entry en código respaldado y thunk respaldado por el archivo


@dataclass(frozen=True, slots=True)
class XorDecoding:
    """Todas las candidatas; solo se elige una si es la única válida."""

    candidates: tuple[XorCandidate, ...]

    @property
    def valid_candidates(self) -> tuple[XorCandidate, ...]:
        return tuple(candidate for candidate in self.candidates if candidate.valid)

    @property
    def chosen(self) -> XorCandidate | None:
        valid = self.valid_candidates
        return valid[0] if len(valid) == 1 else None

    @property
    def build_kind(self) -> BuildKind:
        chosen = self.chosen
        return chosen.kind if chosen else BuildKind.UNKNOWN

    @property
    def entry_point(self) -> int | None:
        chosen = self.chosen
        return chosen.entry_point if chosen else None

    @property
    def kernel_thunk_addr(self) -> int | None:
        chosen = self.chosen
        return chosen.kernel_thunk_addr if chosen else None

    @property
    def ambiguous(self) -> bool:
        """Más de un juego de claves da direcciones plausibles: no se elige ninguno."""
        return len(self.valid_candidates) > 1


def section_digest_ok(data: bytes, header: SectionHeader) -> bool | None:
    """¿Coincide el SHA-1 de la sección? Se calcula sobre `u32 raw_size || datos crudos`.

    None si los datos crudos se salen del archivo (no se puede verificar).
    """
    end = header.raw_addr + header.raw_size
    if end > len(data):
        return None
    message = header.raw_size.to_bytes(4, "little") + data[header.raw_addr : end]
    return hashlib.sha1(message, usedforsecurity=False).digest() == header.digest


@dataclass(frozen=True, slots=True)
class Section:
    """Sección resuelta: su cabecera, su nombre y el estado de su digest."""

    index: int
    name: str
    header: SectionHeader
    digest_ok: bool | None


@dataclass(frozen=True, slots=True)
class _Backing:
    """Respaldo de una VA: región que la contiene y su tramo de archivo `[offset, end)`."""

    section: Section | None  # None: región de cabeceras
    offset: int
    end: int


@dataclass(frozen=True, slots=True)
class AddressMap:
    """Traduce VA de la imagen cargada a offsets del archivo.

    La cabecera está respaldada en `[base, base + size_of_headers)` con
    offset = VA - base. Cada sección respalda solo sus `backed_size` primeros
    bytes; el resto de su rango virtual es BSS y no existe en el archivo. Todo
    tramo se recorta al final del archivo (`file_size`): un XBE truncado no
    respalda lo que le falta.
    """

    base_address: int
    size_of_headers: int
    file_size: int
    sections: tuple[Section, ...] = ()

    @property
    def headers_end(self) -> int:
        return self.base_address + self.size_of_headers

    def _region_backing(self, va: int) -> _Backing | None:
        """Tramo declarado por la región que contiene `va`, sin mirar el tamaño del archivo."""
        if self.base_address <= va < self.headers_end:
            return _Backing(None, va - self.base_address, self.size_of_headers)
        for section in self.sections:
            start = section.header.virtual_addr
            backed = section.header.backed_size
            if start <= va < start + backed:
                raw = section.header.raw_addr
                return _Backing(section, raw + (va - start), raw + backed)
        return None

    def _backing(self, va: int) -> _Backing | None:
        """Como `_region_backing`, recortado al final del archivo."""
        backing = self._region_backing(va)
        if backing is None:
            return None
        end = min(backing.end, self.file_size)
        return _Backing(backing.section, backing.offset, end) if backing.offset < end else None

    def backed_span(self, va: int) -> tuple[int, int] | None:
        """`(offset, fin)` del archivo que respalda `va` hasta el final de su región."""
        backing = self._backing(va)
        return (backing.offset, backing.end) if backing else None

    def to_offset(self, va: int) -> int | None:
        """Offset del archivo que respalda `va`, o None (BSS, fuera de la imagen o del archivo)."""
        backing = self._backing(va)
        return backing.offset if backing else None

    def require_offset(self, va: int, what: str) -> int:
        """Como `to_offset`, pero `UnmappedAddressError` si no hay respaldo."""
        offset = self.to_offset(va)
        if offset is None:
            raise UnmappedAddressError(va, what)
        return offset

    def require_header_range(self, va: int, size: int, what: str) -> int:
        """Offset de `[va, va + size)`, que debe caber entero en la región de cabeceras.

        Las estructuras que el enlazador pone en la cabecera (tabla de secciones,
        certificado, library versions) no pueden vivir en otro sitio: si el puntero
        sale de ahí, el XBE es incoherente.
        """
        if not (self.base_address <= va and va + size <= self.headers_end):
            raise ImageLayoutError(
                f"{what}: [0x{va:08X}, +0x{size:X}) fuera de la región de cabeceras "
                f"[0x{self.base_address:08X}, 0x{self.headers_end:08X})"
            )
        offset = va - self.base_address
        if offset + size > self.file_size:
            raise TruncatedDataError(offset, size)
        return offset

    def is_backed_code(self, va: int) -> bool:
        """¿Cae `va` en bytes del archivo de una sección EXECUTABLE?"""
        backing = self._backing(va)
        return bool(backing and backing.section and backing.section.header.is_executable)

    def section_name(self, va: int) -> str | None:
        """Región que contiene `va` en memoria, incluido su BSS."""
        if self.base_address <= va < self.headers_end:
            return HEADERS_REGION
        for section in self.sections:
            start = section.header.virtual_addr
            if start <= va < start + section.header.virtual_size:
                return section.name
        return None


def decode_xor(header: ImageHeader, address_map: AddressMap) -> XorDecoding:
    """Des-ofusca entry point y kernel thunk con cada juego de claves.

    No hay un campo que diga si el XBE es retail o debug: se deduce porque solo
    la clave correcta deja el entry point en código de una sección EXECUTABLE y
    el thunk en bytes que el archivo respalda. Si ninguna o varias cumplen, el
    tipo queda UNKNOWN.
    """
    candidates = []
    for keys in XOR_KEYS:
        entry = header.entry_point_encoded ^ keys.entry_point
        thunk = header.kernel_thunk_encoded ^ keys.kernel_thunk
        valid = address_map.is_backed_code(entry) and address_map.to_offset(thunk) is not None
        candidates.append(XorCandidate(keys.kind, entry, thunk, valid))
    return XorDecoding(tuple(candidates))


def _check_image_range(header: ImageHeader) -> None:
    if header.base_address + header.size_of_image > ADDRESS_SPACE:
        raise ImageLayoutError(
            f"la imagen [0x{header.base_address:08X}, +0x{header.size_of_image:X}) "
            "desborda los 32 bits"
        )


def _check_section_range(section: SectionHeader, name: str, header: ImageHeader) -> None:
    """El rango virtual de la sección debe caber en 32 bits y dentro de la imagen."""
    start, end = section.virtual_addr, section.virtual_addr + section.virtual_size
    if end > ADDRESS_SPACE:
        raise ImageLayoutError(
            f"sección {name!r}: [0x{start:08X}, +0x{section.virtual_size:X}) desborda los 32 bits"
        )
    image_end = header.base_address + header.size_of_image
    if start < header.base_address or end > image_end:
        raise ImageLayoutError(
            f"sección {name!r}: [0x{start:08X}, 0x{end:08X}) fuera de la imagen "
            f"[0x{header.base_address:08X}, 0x{image_end:08X})"
        )


def _read_sections(data: bytes, header: ImageHeader) -> tuple[Section, ...]:
    """Tabla de secciones, validada antes de recorrerla; vive en la región de cabeceras."""
    count = header.section_count
    if count > MAX_SECTIONS:
        raise ImageLayoutError(f"section_count {count} supera la cota de {MAX_SECTIONS}")
    headers_only = AddressMap(header.base_address, header.size_of_headers, len(data))
    table = headers_only.require_header_range(
        header.section_headers_addr, count * SECTION_HEADER_SIZE, "tabla de secciones"
    )
    sections: list[Section] = []
    for index in range(count):
        section_header = SectionHeader.parse(data, table + index * SECTION_HEADER_SIZE)
        name_offset = headers_only.to_offset(section_header.name_addr)
        name = (
            decode_ascii(read_c_bytes(data, name_offset, SECTION_NAME_MAX))
            if name_offset is not None
            else UNKNOWN_SECTION_NAME
        )
        _check_section_range(section_header, name, header)
        digest_ok = section_digest_ok(data, section_header)
        sections.append(Section(index, name, section_header, digest_ok))
    return tuple(sections)


@dataclass(frozen=True, slots=True)
class XbeImage:
    """Un XBE en memoria: bytes del archivo + cabecera, secciones y claves XOR resueltas."""

    name: str
    data: bytes = field(repr=False)
    header: ImageHeader
    address_map: AddressMap
    xor: XorDecoding

    @classmethod
    def from_bytes(cls, data: bytes, name: str = "<memoria>") -> XbeImage:
        """Parsea y valida cabecera y tabla de secciones; el resto se lee bajo demanda."""
        header = ImageHeader.parse(data)
        _check_image_range(header)
        sections = _read_sections(data, header)
        address_map = AddressMap(header.base_address, header.size_of_headers, len(data), sections)
        return cls(
            name=name,
            data=data,
            header=header,
            address_map=address_map,
            xor=decode_xor(header, address_map),
        )

    @classmethod
    def load(cls, path: Path) -> XbeImage:
        """Lee el archivo completo (los XBE caben de sobra en memoria)."""
        return cls.from_bytes(path.read_bytes(), path.name)

    @property
    def sections(self) -> tuple[Section, ...]:
        return self.address_map.sections

    def c_bytes_at(self, va: int, max_len: int = DEFAULT_C_STRING_MAX) -> bytes | None:
        """Cadena C en `va`, cortada al final de su región (allí, en memoria, hay ceros)."""
        span = self.address_map.backed_span(va)
        if span is None:
            return None
        offset, end = span
        return read_c_bytes(self.data, offset, min(max_len, end - offset))

    def utf16_at(self, va: int, max_chars: int = DEFAULT_UTF16_MAX_CHARS) -> str | None:
        """Cadena UTF-16LE terminada en 0x0000 en `va`, cortada al final de su región."""
        span = self.address_map.backed_span(va)
        if span is None:
            return None
        offset, end = span
        return decode_utf16z(self.data[offset : min(offset + 2 * max_chars, end)])

    def validation_issues(self) -> tuple[str, ...]:
        """Problemas que hacen dudar de la integridad del XBE (vacío si está bien)."""
        issues = [
            _digest_issue(section) for section in self.sections if section.digest_ok is not True
        ]
        issues += self._overlap_issues()
        xor = self.xor
        if xor.ambiguous:
            kinds = ", ".join(candidate.kind for candidate in xor.valid_candidates)
            issues.append(f"claves XOR ambiguas ({kinds}): no se elige tipo de build")
        elif xor.chosen is None:
            issues.append(
                "ninguna clave XOR conocida da un entry point en código y un kernel thunk "
                "respaldado por el archivo"
            )
        return tuple(issues)

    def _overlap_issues(self) -> list[str]:
        """Regiones de memoria que se pisan: el mapa VA -> archivo dejaría de ser único."""
        regions = [
            ("la región de cabeceras", self.header.base_address, self.address_map.headers_end)
        ]
        regions += [
            (
                f"sección {s.name!r}",
                s.header.virtual_addr,
                s.header.virtual_addr + s.header.virtual_size,
            )
            for s in self.sections
            if s.header.virtual_size
        ]
        return [
            f"{first} y {second} se solapan en memoria"
            for (first, start1, end1), (second, start2, end2) in combinations(regions, 2)
            if start1 < end2 and start2 < end1
        ]


def _digest_issue(section: Section) -> str:
    if section.digest_ok is None:
        return f"sección {section.name!r}: datos fuera del archivo, SHA-1 no verificable"
    return f"sección {section.name!r}: el SHA-1 no coincide con el de su cabecera"
