"""Análisis completo de un XBE: sigue los punteros de la cabecera a cada estructura.

La cabecera de imagen es el índice del archivo: guarda la VA del certificado,
de las library versions, del directorio TLS, de las rutas de depuración, del
registro CodeView y (ofuscada) de la tabla de thunks del kernel. `analyze`
recorre esos punteros y devuelve un `XbeAnalysis` inmutable, que es lo que
consumen los informes JSON y Markdown.
"""

from __future__ import annotations

import hashlib
from collections import Counter
from collections.abc import Sequence
from dataclasses import dataclass

from ngb_tools.xbe.binary import decode_build_text
from ngb_tools.xbe.image import XbeImage
from ngb_tools.xbe.kernel import (
    KernelExports,
    KernelImportTable,
    load_kernel_exports,
    read_kernel_imports,
)
from ngb_tools.xbe.strings import DEFAULT_MIN_LEN, StringSummary, extract_strings, summarize_strings
from ngb_tools.xbe.structs import (
    CERT_BASE_SIZE,
    CODEVIEW_HEADER_SIZE,
    LIBRARY_VERSION_SIZE,
    TLS_DIRECTORY_SIZE,
    Certificate,
    CodeViewRecord,
    LibraryVersion,
    TlsDirectory,
)

XAPI_LIBRARY = "XAPILIB"


@dataclass(frozen=True, slots=True)
class DebugStrings:
    """Rutas del build original que el enlazador deja en la cabecera."""

    pathname: str | None
    pathname_encoding: str | None  # 'ascii', 'cp932' (Shift-JIS) o 'ascii+escapes'
    filename: str | None
    unicode_filename: str | None


@dataclass(frozen=True, slots=True)
class XbeAnalysis:
    """Todo lo que el inspector sabe de un XBE."""

    image: XbeImage
    sha256: str
    debug_strings: DebugStrings
    certificate: Certificate
    libraries: tuple[LibraryVersion, ...]
    kernel_library_version: LibraryVersion | None
    xapi_library_version: LibraryVersion | None
    tls: TlsDirectory | None
    codeview: CodeViewRecord | None
    kernel: KernelImportTable | None  # None si ninguna clave XOR dio el thunk
    entry_point_section: str | None
    strings: StringSummary | None  # solo si se pidieron

    @property
    def xdk_build_guess(self) -> int | None:
        return guess_xdk_build(self.libraries)


def guess_xdk_build(libraries: Sequence[LibraryVersion]) -> int | None:
    """Build del XDK: el de XAPILIB si está; si no, el más frecuente."""
    for library in libraries:
        if library.name == XAPI_LIBRARY:
            return library.build
    builds = Counter(library.build for library in libraries)
    return builds.most_common(1)[0][0] if builds else None


def read_debug_strings(image: XbeImage) -> DebugStrings:
    """Rutas de depuración de la cabecera; un puntero nulo o sin respaldo da None."""
    header = image.header

    def build_text(va: int) -> tuple[str | None, str | None]:
        raw = image.c_bytes_at(va) if va else None
        return decode_build_text(raw) if raw is not None else (None, None)

    pathname, pathname_encoding = build_text(header.debug_pathname_addr)
    filename, _ = build_text(header.debug_filename_addr)
    unicode_va = header.debug_unicode_filename_addr
    return DebugStrings(
        pathname=pathname,
        pathname_encoding=pathname_encoding,
        filename=filename,
        unicode_filename=image.utf16_at(unicode_va) if unicode_va else None,
    )


def _read_libraries(image: XbeImage) -> tuple[LibraryVersion, ...]:
    """Array de library versions: identifica el XDK con que se enlazó. Vive en la cabecera."""
    count = image.header.library_version_count
    if not count:
        return ()
    offset = image.address_map.require_header_range(
        image.header.library_versions_addr, count * LIBRARY_VERSION_SIZE, "library versions"
    )
    return tuple(
        LibraryVersion.parse(image.data, offset + index * LIBRARY_VERSION_SIZE)
        for index in range(count)
    )


def _read_certificate(image: XbeImage) -> Certificate:
    """Certificado: en la cabecera, entero (parte fija y la ampliada que declare `size`)."""
    va = image.header.certificate_addr
    offset = image.address_map.require_header_range(va, CERT_BASE_SIZE, "certificado")
    certificate = Certificate.parse(image.data, offset)
    if certificate.size > CERT_BASE_SIZE:
        image.address_map.require_header_range(va, certificate.size, "certificado declarado")
    return certificate


def _optional_region(image: XbeImage, va: int | None, min_size: int) -> bytes | None:
    """Bytes de un puntero opcional hasta el final de su región.

    Un puntero nulo, sin respaldo o que no deja `min_size` bytes antes del final de
    la región (o del archivo) cuenta como ausente: es un dato accesorio, no un fallo.
    """
    span = image.address_map.backed_span(va) if va else None
    if span is None or span[1] - span[0] < min_size:
        return None
    offset, end = span
    return image.data[offset:end]


def _library_at(image: XbeImage, va: int) -> LibraryVersion | None:
    region = _optional_region(image, va, LIBRARY_VERSION_SIZE)
    return LibraryVersion.parse(region, 0) if region is not None else None


def analyze(
    image: XbeImage,
    exports: KernelExports | None = None,
    *,
    with_strings: bool = False,
    min_len: int = DEFAULT_MIN_LEN,
) -> XbeAnalysis:
    """Lee todas las estructuras de `image`.

    `exports` nombra los ordinales del kernel (por defecto, la tabla incluida).
    Lanza `XbeFormatError` si una estructura obligatoria (certificado, library
    versions, tabla de thunks) está fuera de su región o truncada. Los punteros
    opcionales (TLS, CodeView, library versions del kernel y de XAPI) que no se
    pueden leer cuentan como ausentes.
    """
    header = image.header
    tls_region = _optional_region(image, header.tls_addr, TLS_DIRECTORY_SIZE)
    codeview_region = _optional_region(image, header.debug_info_addr, CODEVIEW_HEADER_SIZE)
    entry_point = image.xor.entry_point
    kernel = read_kernel_imports(image, exports if exports is not None else load_kernel_exports())
    return XbeAnalysis(
        image=image,
        sha256=hashlib.sha256(image.data).hexdigest(),
        debug_strings=read_debug_strings(image),
        certificate=_read_certificate(image),
        libraries=_read_libraries(image),
        kernel_library_version=_library_at(image, header.kernel_library_version_addr),
        xapi_library_version=_library_at(image, header.xapi_library_version_addr),
        tls=TlsDirectory.parse(tls_region, 0) if tls_region is not None else None,
        codeview=CodeViewRecord.parse(codeview_region, 0) if codeview_region is not None else None,
        kernel=kernel,
        entry_point_section=(
            image.address_map.section_name(entry_point) if entry_point is not None else None
        ),
        strings=summarize_strings(extract_strings(image, min_len)) if with_strings else None,
    )
