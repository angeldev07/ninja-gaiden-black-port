"""Structures of the XBE format, parsed from bytes and file offsets.

An XBE starts with the image header. The kernel loader copies the
header as-is to `base_address` (normally 0x00010000) and each section from
`raw_addr` to `virtual_addr`, so almost all pointers in the format are
virtual addresses (VA) of that loaded image. This module only knows how to read
each structure at a given file offset; translating VA to offset is the job of
`ngb_tools.xbe.image`.

Public references used for the offsets:

- https://xboxdevwiki.net/Xbe (header, XOR keys, media flags)
- https://github.com/Cxbx-Reloaded/Cxbx-Reloaded, `src/common/xbe/Xbe.h`
  (certificate, section header and region offsets)
"""

from __future__ import annotations

import operator
from collections.abc import Mapping
from dataclasses import dataclass
from datetime import UTC, datetime
from functools import partial, reduce

from ngb_tools.xbe.binary import (
    decode_build_text,
    decode_utf16z,
    read_bytes,
    read_c_bytes,
    read_u16s,
    read_u32,
    read_u32s,
)
from ngb_tools.xbe.errors import BadMagicError, TruncatedDataError

XBE_MAGIC = b"XBEH"
IMAGE_HEADER_MIN_SIZE = 0x178  # oldest header; newer ones add fields at the end
SECTION_HEADER_SIZE = 0x38  # 9 u32 + 20-byte SHA-1
LIBRARY_VERSION_SIZE = 0x10  # name[8] + 4 u16
TLS_DIRECTORY_SIZE = 0x18  # 6 u32, same layout as the PE TLS directory
CERT_BASE_SIZE = 0x1D0  # original certificate, up to and including the alternate keys
CERT_TITLE_NAME_CHARS = 40  # fixed-length UTF-16LE title
CERT_ALTERNATE_IDS = 16
CODEVIEW_RSDS = b"RSDS"
CODEVIEW_HEADER_SIZE = 24  # signature + GUID (16) + age; the .pdb path follows

# Bit masks of each flags field: bit -> name.
INIT_FLAGS: Mapping[int, str] = {
    0x1: "MOUNT_UTILITY_DRIVE",
    0x2: "FORMAT_UTILITY_DRIVE",
    0x4: "LIMIT_64MB",
    0x8: "DONT_SETUP_HARDDISK",
}
ALLOWED_MEDIA_FLAGS: Mapping[int, str] = {
    0x00000001: "HARD_DISK",
    0x00000002: "DVD_X2",
    0x00000004: "DVD_CD",
    0x00000008: "CD",
    0x00000010: "DVD_5_RO",
    0x00000020: "DVD_9_RO",
    0x00000040: "DVD_5_RW",
    0x00000080: "DVD_9_RW",
    0x00000100: "DONGLE",
    0x00000200: "MEDIA_BOARD",
    0x40000000: "NONSECURE_HARD_DISK",
    0x80000000: "NONSECURE_MODE",
}
GAME_REGION_FLAGS: Mapping[int, str] = {
    0x00000001: "NA",
    0x00000002: "JAPAN",
    0x00000004: "REST_OF_WORLD",
    0x80000000: "MANUFACTURING",
}
SECTION_EXECUTABLE = 0x04
SECTION_INSERTED_FILE = 0x08
SECTION_FLAGS: Mapping[int, str] = {
    0x01: "WRITABLE",
    0x02: "PRELOAD",
    SECTION_EXECUTABLE: "EXECUTABLE",
    SECTION_INSERTED_FILE: "INSERTED_FILE",
    0x10: "HEAD_PAGE_RO",
    0x20: "TAIL_PAGE_RO",
}
# 2-bit field of a library version: did Microsoft approve it for retail?
LIBRARY_APPROVED: Mapping[int, str] = {0: "no", 1: "possible", 2: "yes", 3: "unknown"}


def decode_flags(value: int, table: Mapping[int, str]) -> list[str]:
    """Names of the set bits of `value`; unknown bits are given in hex."""
    names = [name for bit, name in table.items() if value & bit]
    unknown = value & ~reduce(operator.or_, table, 0)
    if unknown:
        names.append(f"UNKNOWN(0x{unknown:X})")
    return names


def iso_utc(timestamp: int) -> str:
    """UNIX timestamp (XBE ones are UTC) as ISO 8601."""
    return datetime.fromtimestamp(timestamp, tz=UTC).isoformat()


def format_title_id(title_id: int) -> str:
    """0x5443000D -> 'TC-013': 2 publisher letters (high byte) + game number (u16)."""
    publisher = bytes(((title_id >> 24) & 0xFF, (title_id >> 16) & 0xFF))
    if all(0x20 < byte < 0x7F for byte in publisher):
        prefix = publisher.decode("ascii")
    else:
        prefix = publisher.hex().upper()
    return f"{prefix}-{title_id & 0xFFFF:03d}"


@dataclass(frozen=True, slots=True)
class ImageHeader:
    """Image header (offset 0): sizes and VA pointers to the rest of the structures.

    Between the magic and `base_address` (0x004-0x103) sits the 256-byte RSA
    signature, which is not verified here. `entry_point_encoded` and
    `kernel_thunk_encoded` are XOR-obfuscated; see `ngb_tools.xbe.image.decode_xor`.
    """

    base_address: int
    size_of_headers: int
    size_of_image: int
    size_of_image_header: int
    timedate: int
    certificate_addr: int
    section_count: int
    section_headers_addr: int
    init_flags: int
    entry_point_encoded: int
    tls_addr: int
    stack_size: int  # xboxdevwiki: SizeOfStackReserve; Cxbx calls it StackCommit
    pe_heap_reserve: int
    pe_heap_commit: int
    pe_base_address: int
    pe_size_of_image: int
    pe_checksum: int
    pe_timedate: int
    debug_pathname_addr: int
    debug_filename_addr: int
    debug_unicode_filename_addr: int
    kernel_thunk_encoded: int
    non_kernel_import_dir_addr: int
    library_version_count: int
    library_versions_addr: int
    kernel_library_version_addr: int
    xapi_library_version_addr: int
    logo_bitmap_addr: int
    logo_bitmap_size: int
    unknown1: bytes | None  # xboxdevwiki: Unknown1, XDK >= 5028
    debug_info_addr: int | None  # xboxdevwiki: Unknown2, XDK >= 5455; observed: VA of an RSDS

    @classmethod
    def parse(cls, data: bytes) -> ImageHeader:
        """Read the header from the start of the file."""
        if len(data) < IMAGE_HEADER_MIN_SIZE:
            raise TruncatedDataError(0, IMAGE_HEADER_MIN_SIZE)
        if data[:4] != XBE_MAGIC:
            raise BadMagicError(f"invalid magic {data[:4]!r}; expected {XBE_MAGIC!r}")
        u32 = partial(read_u32, data)
        # Optional fields exist only if the declared header covers them.
        size_of_image_header = u32(0x110)
        return cls(
            base_address=u32(0x104),
            size_of_headers=u32(0x108),
            size_of_image=u32(0x10C),
            size_of_image_header=size_of_image_header,
            timedate=u32(0x114),
            certificate_addr=u32(0x118),
            section_count=u32(0x11C),
            section_headers_addr=u32(0x120),
            init_flags=u32(0x124),
            entry_point_encoded=u32(0x128),
            tls_addr=u32(0x12C),
            stack_size=u32(0x130),
            pe_heap_reserve=u32(0x134),
            pe_heap_commit=u32(0x138),
            pe_base_address=u32(0x13C),
            pe_size_of_image=u32(0x140),
            pe_checksum=u32(0x144),
            pe_timedate=u32(0x148),
            debug_pathname_addr=u32(0x14C),
            debug_filename_addr=u32(0x150),
            debug_unicode_filename_addr=u32(0x154),
            kernel_thunk_encoded=u32(0x158),
            non_kernel_import_dir_addr=u32(0x15C),
            library_version_count=u32(0x160),
            library_versions_addr=u32(0x164),
            kernel_library_version_addr=u32(0x168),
            xapi_library_version_addr=u32(0x16C),
            logo_bitmap_addr=u32(0x170),
            logo_bitmap_size=u32(0x174),
            unknown1=read_bytes(data, 0x178, 8) if size_of_image_header >= 0x180 else None,
            debug_info_addr=u32(0x180) if size_of_image_header >= 0x184 else None,
        )

    @property
    def timedate_iso(self) -> str:
        return iso_utc(self.timedate)

    @property
    def pe_timedate_iso(self) -> str:
        return iso_utc(self.pe_timedate)

    @property
    def init_flags_decoded(self) -> list[str]:
        return decode_flags(self.init_flags, INIT_FLAGS)


@dataclass(frozen=True, slots=True)
class Certificate:
    """Certificate: title identity, allowed media and regions, version.

    Between 0xB0 and 0x1CF sit the LAN, signature and alternate keys (not
    exposed). The later fields depend on the size declared in `size`.
    """

    size: int
    timedate: int
    title_id: int
    title_name: str
    alternate_title_ids: tuple[int, ...]  # non-zero ones only
    allowed_media: int
    game_region: int
    game_ratings: int
    disk_number: int
    version: int
    original_certificate_size: int | None
    online_service: int | None
    security_flags: int | None

    @classmethod
    def parse(cls, data: bytes, offset: int) -> Certificate:
        """Read the certificate that starts at file offset `offset`."""

        def u32(field: int) -> int:
            return read_u32(data, offset + field)

        size = u32(0x00)

        def optional_u32(field: int) -> int | None:
            """Field of the extended part: exists only if the declared `size` covers it."""
            return u32(field) if size >= field + 4 else None

        name_raw = read_bytes(data, offset + 0x0C, 2 * CERT_TITLE_NAME_CHARS)
        alternates = read_u32s(data, offset + 0x5C, CERT_ALTERNATE_IDS)
        return cls(
            size=size,
            timedate=u32(0x04),
            title_id=u32(0x08),
            title_name=decode_utf16z(name_raw),
            alternate_title_ids=tuple(title_id for title_id in alternates if title_id),
            allowed_media=u32(0x9C),
            game_region=u32(0xA0),
            game_ratings=u32(0xA4),
            disk_number=u32(0xA8),
            version=u32(0xAC),
            original_certificate_size=optional_u32(0x1D0),
            online_service=optional_u32(0x1D4),
            security_flags=optional_u32(0x1D8),
        )

    @property
    def timedate_iso(self) -> str:
        return iso_utc(self.timedate)

    @property
    def title_id_str(self) -> str:
        return format_title_id(self.title_id)

    @property
    def allowed_media_decoded(self) -> list[str]:
        return decode_flags(self.allowed_media, ALLOWED_MEDIA_FLAGS)

    @property
    def game_region_decoded(self) -> list[str]:
        return decode_flags(self.game_region, GAME_REGION_FLAGS)

    @property
    def online_service_text(self) -> str | None:
        """The online service is a FourCC: read as a big-endian u32 it gives the text."""
        if self.online_service is None:
            return None
        raw = self.online_service.to_bytes(4, "big")
        return raw.decode("ascii", errors="replace").strip("\0")


@dataclass(frozen=True, slots=True)
class SectionHeader:
    """Section header (0x38 bytes) of the section table.

    `digest` is the SHA-1 of `u32 raw_size || raw data`; see
    `ngb_tools.xbe.image.section_digest_ok`.
    """

    flags: int
    virtual_addr: int
    virtual_size: int
    raw_addr: int
    raw_size: int
    name_addr: int
    reference_count: int
    head_shared_ref_count_addr: int
    tail_shared_ref_count_addr: int
    digest: bytes

    @classmethod
    def parse(cls, data: bytes, offset: int) -> SectionHeader:
        (
            flags,
            virtual_addr,
            virtual_size,
            raw_addr,
            raw_size,
            name_addr,
            ref_count,
            head,
            tail,
        ) = read_u32s(data, offset, 9)
        return cls(
            flags=flags,
            virtual_addr=virtual_addr,
            virtual_size=virtual_size,
            raw_addr=raw_addr,
            raw_size=raw_size,
            name_addr=name_addr,
            reference_count=ref_count,
            head_shared_ref_count_addr=head,
            tail_shared_ref_count_addr=tail,
            digest=read_bytes(data, offset + 36, 20),
        )

    @property
    def flags_decoded(self) -> list[str]:
        return decode_flags(self.flags, SECTION_FLAGS)

    @property
    def backed_size(self) -> int:
        """Section bytes that come from the file; the rest in memory is BSS (zeros)."""
        return min(self.raw_size, self.virtual_size)

    @property
    def is_executable(self) -> bool:
        return bool(self.flags & SECTION_EXECUTABLE)

    @property
    def is_inserted_file(self) -> bool:
        """Embedded binary file ($$XTIMAGE, $$XSIMAGE, .XTLID), not code or data."""
        return bool(self.flags & SECTION_INSERTED_FILE)


@dataclass(frozen=True, slots=True)
class LibraryVersion:
    """Version of a linked XDK library (16 bytes); identifies the XDK used.

    `flags` (u16) packs: bits 0-12 QFE, bits 13-14 approval, bit 15 debug.
    """

    name: str
    major: int
    minor: int
    build: int
    flags: int

    @classmethod
    def parse(cls, data: bytes, offset: int) -> LibraryVersion:
        raw = read_bytes(data, offset, LIBRARY_VERSION_SIZE)
        major, minor, build, flags = read_u16s(raw, 8, 4)
        name = raw[:8].split(b"\0", 1)[0].decode("ascii", errors="replace")
        return cls(name=name, major=major, minor=minor, build=build, flags=flags)

    @property
    def version(self) -> str:
        return f"{self.major}.{self.minor}.{self.build}"

    @property
    def qfe(self) -> int:
        return self.flags & 0x1FFF

    @property
    def approved(self) -> int:
        return (self.flags >> 13) & 0x3

    @property
    def approved_text(self) -> str:
        return LIBRARY_APPROVED[self.approved]

    @property
    def debug_build(self) -> bool:
        return bool(self.flags & 0x8000)


@dataclass(frozen=True, slots=True)
class TlsDirectory:
    """TLS directory: same layout as in PE, with absolute addresses (VA)."""

    raw_data_start_addr: int
    raw_data_end_addr: int
    index_addr: int
    callbacks_addr: int
    size_of_zero_fill: int
    characteristics: int

    @classmethod
    def parse(cls, data: bytes, offset: int) -> TlsDirectory:
        start, end, index, callbacks, zero_fill, characteristics = read_u32s(data, offset, 6)
        return cls(start, end, index, callbacks, zero_fill, characteristics)


@dataclass(frozen=True, slots=True)
class CodeViewRecord:
    """CodeView 'RSDS' record (PDB 7.0): GUID + age + .pdb path.

    GUID and age identify the exact PDB the executable was linked with; it is the
    Microsoft symbol server key (`pdb_id`).
    """

    guid_bytes: bytes
    age: int
    pdb_path: str

    @classmethod
    def parse(cls, data: bytes, offset: int) -> CodeViewRecord | None:
        """The record at `offset`, or None if it is not a complete RSDS."""
        raw = data[offset : offset + CODEVIEW_HEADER_SIZE]
        if len(raw) < CODEVIEW_HEADER_SIZE or raw[:4] != CODEVIEW_RSDS:
            return None
        pdb_path, _ = decode_build_text(read_c_bytes(data, offset + CODEVIEW_HEADER_SIZE))
        return cls(guid_bytes=raw[4:20], age=read_u32(raw, 20), pdb_path=pdb_path)

    @property
    def signature(self) -> str:
        return CODEVIEW_RSDS.decode("ascii")

    @property
    def guid(self) -> str:
        """GUID in Windows notation: the first 3 fields are little-endian."""
        data1 = read_u32(self.guid_bytes, 0)
        data2, data3 = read_u16s(self.guid_bytes, 4, 2)
        data4 = self.guid_bytes[8:].hex().upper()
        return f"{data1:08X}-{data2:04X}-{data3:04X}-{data4[:4]}-{data4[4:]}"

    @property
    def pdb_id(self) -> str:
        """Symbol server key: GUID without dashes + age in hex."""
        return self.guid.replace("-", "") + f"{self.age:X}"
