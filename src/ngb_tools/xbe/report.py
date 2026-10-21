"""Informe JSON del análisis, con su esquema declarado como TypedDict.

Convención: las direcciones, punteros, máscaras de flags y valores ofuscados
van como cadenas `0x........` (así se buscan tal cual en un desensamblador);
los tamaños, contadores y versiones van como enteros.
"""

from __future__ import annotations

import json
from typing import NotRequired, TypedDict

from ngb_tools.xbe.analysis import DebugStrings, XbeAnalysis
from ngb_tools.xbe.binary import hex32
from ngb_tools.xbe.image import Section, XorCandidate, XorDecoding
from ngb_tools.xbe.kernel import KernelImport, KernelImportTable
from ngb_tools.xbe.strings import CategorizedString, StringSummary
from ngb_tools.xbe.structs import (
    XBE_MAGIC,
    Certificate,
    CodeViewRecord,
    ImageHeader,
    LibraryVersion,
    TlsDirectory,
    format_title_id,
)


class FileJson(TypedDict):
    size: int
    sha256: str
    name: str


class ImageHeaderJson(TypedDict):
    magic: str
    base_address: str
    size_of_headers: int
    size_of_image: int
    size_of_image_header: int
    timedate: int
    certificate_addr: str
    section_count: int
    section_headers_addr: str
    init_flags: str
    entry_point_encoded: str
    tls_addr: str
    stack_size: int
    pe_heap_reserve: int
    pe_heap_commit: int
    pe_base_address: str
    pe_size_of_image: int
    pe_checksum: str
    pe_timedate: int
    debug_pathname_addr: str
    debug_filename_addr: str
    debug_unicode_filename_addr: str
    kernel_thunk_encoded: str
    non_kernel_import_dir_addr: str
    library_version_count: int
    library_versions_addr: str
    kernel_library_version_addr: str
    xapi_library_version_addr: str
    logo_bitmap_addr: str
    logo_bitmap_size: int
    timedate_iso: str
    pe_timedate_iso: str
    init_flags_decoded: list[str]
    unknown1_hex: NotRequired[str]
    debug_info_addr: NotRequired[str]


class DebugStringsJson(TypedDict):
    debug_pathname: str | None
    debug_pathname_encoding: str | None
    debug_filename: str | None
    debug_unicode_filename: str | None


class XorCandidateJson(TypedDict):
    kind: str
    entry_point: str
    kernel_thunk_addr: str
    valid: bool


class XorJson(TypedDict):
    build_kind: str
    entry_point: str | None
    kernel_thunk_addr: str | None
    ambiguous: bool
    candidates: list[XorCandidateJson]


class AlternateTitleJson(TypedDict):
    title_id: str
    title_id_str: str


class CertificateJson(TypedDict):
    size: int
    timedate: int
    timedate_iso: str
    title_id: str
    title_id_str: str
    title_name: str
    alternate_title_ids: list[AlternateTitleJson]
    allowed_media: str
    allowed_media_decoded: list[str]
    game_region: str
    game_region_decoded: list[str]
    game_ratings: int
    disk_number: int
    version: int
    original_certificate_size: NotRequired[int]
    online_service: NotRequired[str]
    online_service_text: NotRequired[str]
    security_flags: NotRequired[str]


class SectionJson(TypedDict):
    flags: str
    flags_decoded: list[str]
    virtual_addr: str
    virtual_size: int
    raw_addr: str
    raw_size: int
    name_addr: str
    reference_count: int
    head_shared_ref_count_addr: str
    tail_shared_ref_count_addr: str
    digest: str
    index: int
    name: str
    digest_ok: bool | None


class LibraryJson(TypedDict):
    name: str
    major: int
    minor: int
    build: int
    version: str
    flags: str
    qfe: int
    approved: int
    approved_text: str
    debug_build: bool


class TlsJson(TypedDict):
    raw_data_start_addr: str
    raw_data_end_addr: str
    index_addr: str
    callbacks_addr: str
    size_of_zero_fill: int
    characteristics: int


class KernelImportJson(TypedDict):
    slot_va: str
    raw_entry: str
    ordinal: int | None
    name: str
    kind: str | None
    callconv: str | None
    arg_bytes: int | None


class CodeViewJson(TypedDict):
    signature: str
    guid: str
    age: int
    pdb_id: str
    pdb_path: str


class KernelThunkJson(TypedDict):
    va: str
    file_offset: str
    section: str | None


class StringItemJson(TypedDict):
    section: str
    va: str
    text: str
    demangled: NotRequired[str]


class StringsJson(TypedDict):
    total: int
    unique: int
    nul_terminated: int
    by_section: dict[str, int]
    category_counts: dict[str, int]
    identifier_prefixes: dict[str, int]
    categories: dict[str, list[StringItemJson]]


class XbeReport(TypedDict):
    file: FileJson
    image_header: ImageHeaderJson
    debug_strings: DebugStringsJson
    xor: XorJson
    certificate: CertificateJson
    sections: list[SectionJson]
    libraries: list[LibraryJson]
    xdk_build_guess: int | None
    kernel_library_version: LibraryJson | None
    xapi_library_version: LibraryJson | None
    tls: TlsJson | None
    kernel_imports: list[KernelImportJson]
    codeview: CodeViewJson | None
    kernel_thunk: KernelThunkJson | None
    entry_point_section: str | None
    strings: NotRequired[StringsJson]


def _hex_or_none(value: int | None) -> str | None:
    return hex32(value) if value is not None else None


def image_header_json(header: ImageHeader) -> ImageHeaderJson:
    result: ImageHeaderJson = {
        "magic": XBE_MAGIC.decode("ascii"),
        "base_address": hex32(header.base_address),
        "size_of_headers": header.size_of_headers,
        "size_of_image": header.size_of_image,
        "size_of_image_header": header.size_of_image_header,
        "timedate": header.timedate,
        "certificate_addr": hex32(header.certificate_addr),
        "section_count": header.section_count,
        "section_headers_addr": hex32(header.section_headers_addr),
        "init_flags": hex32(header.init_flags),
        "entry_point_encoded": hex32(header.entry_point_encoded),
        "tls_addr": hex32(header.tls_addr),
        "stack_size": header.stack_size,
        "pe_heap_reserve": header.pe_heap_reserve,
        "pe_heap_commit": header.pe_heap_commit,
        "pe_base_address": hex32(header.pe_base_address),
        "pe_size_of_image": header.pe_size_of_image,
        "pe_checksum": hex32(header.pe_checksum),
        "pe_timedate": header.pe_timedate,
        "debug_pathname_addr": hex32(header.debug_pathname_addr),
        "debug_filename_addr": hex32(header.debug_filename_addr),
        "debug_unicode_filename_addr": hex32(header.debug_unicode_filename_addr),
        "kernel_thunk_encoded": hex32(header.kernel_thunk_encoded),
        "non_kernel_import_dir_addr": hex32(header.non_kernel_import_dir_addr),
        "library_version_count": header.library_version_count,
        "library_versions_addr": hex32(header.library_versions_addr),
        "kernel_library_version_addr": hex32(header.kernel_library_version_addr),
        "xapi_library_version_addr": hex32(header.xapi_library_version_addr),
        "logo_bitmap_addr": hex32(header.logo_bitmap_addr),
        "logo_bitmap_size": header.logo_bitmap_size,
        "timedate_iso": header.timedate_iso,
        "pe_timedate_iso": header.pe_timedate_iso,
        "init_flags_decoded": header.init_flags_decoded,
    }
    if header.unknown1 is not None:
        result["unknown1_hex"] = header.unknown1.hex()
    if header.debug_info_addr is not None:
        result["debug_info_addr"] = hex32(header.debug_info_addr)
    return result


def debug_strings_json(debug: DebugStrings) -> DebugStringsJson:
    return {
        "debug_pathname": debug.pathname,
        "debug_pathname_encoding": debug.pathname_encoding,
        "debug_filename": debug.filename,
        "debug_unicode_filename": debug.unicode_filename,
    }


def _xor_candidate_json(candidate: XorCandidate) -> XorCandidateJson:
    return {
        "kind": candidate.kind.value,
        "entry_point": hex32(candidate.entry_point),
        "kernel_thunk_addr": hex32(candidate.kernel_thunk_addr),
        "valid": candidate.valid,
    }


def xor_json(xor: XorDecoding) -> XorJson:
    return {
        "build_kind": xor.build_kind.value,
        "entry_point": _hex_or_none(xor.entry_point),
        "kernel_thunk_addr": _hex_or_none(xor.kernel_thunk_addr),
        "ambiguous": xor.ambiguous,
        "candidates": [_xor_candidate_json(candidate) for candidate in xor.candidates],
    }


def certificate_json(cert: Certificate) -> CertificateJson:
    result: CertificateJson = {
        "size": cert.size,
        "timedate": cert.timedate,
        "timedate_iso": cert.timedate_iso,
        "title_id": hex32(cert.title_id),
        "title_id_str": cert.title_id_str,
        "title_name": cert.title_name,
        "alternate_title_ids": [
            {"title_id": hex32(title_id), "title_id_str": format_title_id(title_id)}
            for title_id in cert.alternate_title_ids
        ],
        "allowed_media": hex32(cert.allowed_media),
        "allowed_media_decoded": cert.allowed_media_decoded,
        "game_region": hex32(cert.game_region),
        "game_region_decoded": cert.game_region_decoded,
        "game_ratings": cert.game_ratings,
        "disk_number": cert.disk_number,
        "version": cert.version,
    }
    if cert.original_certificate_size is not None:
        result["original_certificate_size"] = cert.original_certificate_size
    if cert.online_service is not None and cert.online_service_text is not None:
        result["online_service"] = hex32(cert.online_service)
        result["online_service_text"] = cert.online_service_text
    if cert.security_flags is not None:
        result["security_flags"] = hex32(cert.security_flags)
    return result


def section_json(section: Section) -> SectionJson:
    header = section.header
    return {
        "flags": hex32(header.flags),
        "flags_decoded": header.flags_decoded,
        "virtual_addr": hex32(header.virtual_addr),
        "virtual_size": header.virtual_size,
        "raw_addr": hex32(header.raw_addr),
        "raw_size": header.raw_size,
        "name_addr": hex32(header.name_addr),
        "reference_count": header.reference_count,
        "head_shared_ref_count_addr": hex32(header.head_shared_ref_count_addr),
        "tail_shared_ref_count_addr": hex32(header.tail_shared_ref_count_addr),
        "digest": header.digest.hex(),
        "index": section.index,
        "name": section.name,
        "digest_ok": section.digest_ok,
    }


def library_json(library: LibraryVersion) -> LibraryJson:
    return {
        "name": library.name,
        "major": library.major,
        "minor": library.minor,
        "build": library.build,
        "version": library.version,
        "flags": hex32(library.flags),
        "qfe": library.qfe,
        "approved": library.approved,
        "approved_text": library.approved_text,
        "debug_build": library.debug_build,
    }


def tls_json(tls: TlsDirectory) -> TlsJson:
    return {
        "raw_data_start_addr": hex32(tls.raw_data_start_addr),
        "raw_data_end_addr": hex32(tls.raw_data_end_addr),
        "index_addr": hex32(tls.index_addr),
        "callbacks_addr": hex32(tls.callbacks_addr),
        "size_of_zero_fill": tls.size_of_zero_fill,
        "characteristics": tls.characteristics,
    }


def kernel_import_json(item: KernelImport) -> KernelImportJson:
    return {
        "slot_va": hex32(item.slot_va),
        "raw_entry": hex32(item.raw_entry),
        "ordinal": item.ordinal,
        "name": item.name,
        "kind": item.kind.value if item.kind else None,
        "callconv": item.callconv.value if item.callconv else None,
        "arg_bytes": item.arg_bytes,
    }


def codeview_json(record: CodeViewRecord) -> CodeViewJson:
    return {
        "signature": record.signature,
        "guid": record.guid,
        "age": record.age,
        "pdb_id": record.pdb_id,
        "pdb_path": record.pdb_path,
    }


def kernel_thunk_json(table: KernelImportTable) -> KernelThunkJson:
    return {
        "va": hex32(table.va),
        "file_offset": hex32(table.file_offset),
        "section": table.section,
    }


def _string_item_json(item: CategorizedString) -> StringItemJson:
    result: StringItemJson = {"section": item.section, "va": hex32(item.va), "text": item.text}
    if item.demangled is not None:
        result["demangled"] = item.demangled
    return result


def strings_json(summary: StringSummary) -> StringsJson:
    return {
        "total": summary.total,
        "unique": summary.unique,
        "nul_terminated": summary.nul_terminated,
        "by_section": dict(summary.by_section),
        "category_counts": {str(name): count for name, count in summary.category_counts.items()},
        "identifier_prefixes": summary.identifier_prefixes,
        "categories": {
            str(name): [_string_item_json(item) for item in items]
            for name, items in summary.categories.items()
        },
    }


def _optional_library(library: LibraryVersion | None) -> LibraryJson | None:
    return library_json(library) if library is not None else None


def build_report(analysis: XbeAnalysis) -> XbeReport:
    """El análisis completo como estructura JSON (sin serializar)."""
    image = analysis.image
    kernel = analysis.kernel
    report: XbeReport = {
        "file": {"size": len(image.data), "sha256": analysis.sha256, "name": image.name},
        "image_header": image_header_json(image.header),
        "debug_strings": debug_strings_json(analysis.debug_strings),
        "xor": xor_json(image.xor),
        "certificate": certificate_json(analysis.certificate),
        "sections": [section_json(section) for section in image.sections],
        "libraries": [library_json(library) for library in analysis.libraries],
        "xdk_build_guess": analysis.xdk_build_guess,
        "kernel_library_version": _optional_library(analysis.kernel_library_version),
        "xapi_library_version": _optional_library(analysis.xapi_library_version),
        "tls": tls_json(analysis.tls) if analysis.tls is not None else None,
        "kernel_imports": [kernel_import_json(item) for item in kernel.imports] if kernel else [],
        "codeview": codeview_json(analysis.codeview) if analysis.codeview is not None else None,
        "kernel_thunk": kernel_thunk_json(kernel) if kernel else None,
        "entry_point_section": analysis.entry_point_section,
    }
    if analysis.strings is not None:
        report["strings"] = strings_json(analysis.strings)
    return report


def render_json(analysis: XbeAnalysis) -> str:
    """Informe JSON serializado: UTF-8 legible (sin escapes ASCII) y salto final."""
    return json.dumps(build_report(analysis), indent=2, ensure_ascii=False) + "\n"
