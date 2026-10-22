"""Full analysis of an XBE: follows the header pointers to every structure.

The image header is the index of the file: it holds the VA of the certificate,
the library versions, the TLS directory, the debug paths, the CodeView
record and (obfuscated) the kernel thunk table. `analyze` walks those
pointers and returns an immutable `XbeAnalysis`, which is what the JSON and
Markdown reports consume.
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
    """Paths of the original build that the linker leaves in the header."""

    pathname: str | None
    pathname_encoding: str | None  # 'ascii', 'cp932' (Shift-JIS) or 'ascii+escapes'
    filename: str | None
    unicode_filename: str | None


@dataclass(frozen=True, slots=True)
class XbeAnalysis:
    """Everything the inspector knows about an XBE."""

    image: XbeImage
    sha256: str
    debug_strings: DebugStrings
    certificate: Certificate
    libraries: tuple[LibraryVersion, ...]
    kernel_library_version: LibraryVersion | None
    xapi_library_version: LibraryVersion | None
    tls: TlsDirectory | None
    codeview: CodeViewRecord | None
    kernel: KernelImportTable | None  # None if no XOR key yielded the thunk
    entry_point_section: str | None
    strings: StringSummary | None  # only if requested

    @property
    def xdk_build_guess(self) -> int | None:
        return guess_xdk_build(self.libraries)


def guess_xdk_build(libraries: Sequence[LibraryVersion]) -> int | None:
    """XDK build: the one from XAPILIB if present; otherwise the most frequent."""
    for library in libraries:
        if library.name == XAPI_LIBRARY:
            return library.build
    builds = Counter(library.build for library in libraries)
    return builds.most_common(1)[0][0] if builds else None


def read_debug_strings(image: XbeImage) -> DebugStrings:
    """Debug paths from the header; a null or unbacked pointer gives None."""
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
    """Library versions array: identifies the XDK it was linked with. Lives in the header."""
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
    """Certificate: in the header, whole (fixed part plus the extended one declared by `size`)."""
    va = image.header.certificate_addr
    offset = image.address_map.require_header_range(va, CERT_BASE_SIZE, "certificate")
    certificate = Certificate.parse(image.data, offset)
    if certificate.size > CERT_BASE_SIZE:
        image.address_map.require_header_range(va, certificate.size, "declared certificate")
    return certificate


def _optional_region(image: XbeImage, va: int | None, min_size: int) -> bytes | None:
    """Bytes of an optional pointer up to the end of its region.

    A null pointer, an unbacked one, or one that leaves fewer than `min_size` bytes before
    the end of the region (or of the file) counts as absent: it is ancillary data, not a failure.
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
    """Read every structure of `image`.

    `exports` names the kernel ordinals (by default, the bundled table).
    Raises `XbeFormatError` if a mandatory structure (certificate, library
    versions, thunk table) lies outside its region or is truncated. Optional
    pointers (TLS, CodeView, kernel and XAPI library versions) that cannot be
    read count as absent.
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
