"""Synthetic XBE built in memory for the tests.

Every expected value comes from this file, written by hand: the builder imports
nothing from `ngb_tools.xbe`, so a bug in the code under test does not leak into
the expectations.

Layout (file offsets; in the header, VA = BASE + offset):

    0x000  image header (0x184 bytes: includes the CodeView field at 0x180)
    0x200  certificate (0x1EC bytes)
    0x480  ".text" name / 0x488 "$$XTIMAGE" name
    0x4A0  XAPILIB library version
    0x4C0  debug path / 0x500 name / 0x510 UTF-16 name
    0x540  names of the extra sections (8 bytes each)
    0x800  section headers (0x38 bytes each): .text, $$XTIMAGE, extras
    0x1000 .text: strings at 0x1010, thunk at 0x1100, RSDS record at 0x1180
    0x1200 $$XTIMAGE (only with `inserted_file=True`)
"""

from __future__ import annotations

import hashlib
import struct
from dataclasses import dataclass

BASE = 0x00010000
SIZE_OF_HEADERS = 0x1000
SIZE_OF_IMAGE = 0x2000
CERT_OFF = 0x200
CERT_SIZE = 0x1EC
SECTION_NAME_OFF = 0x480
INSERTED_NAME_OFF = 0x488
LIBRARY_OFF = 0x4A0
DEBUG_PATH_OFF = 0x4C0
DEBUG_FILE_OFF = 0x500
DEBUG_UFILE_OFF = 0x510
EXTRA_NAMES_OFF = 0x540
SECTION_HEADERS_OFF = 0x800
SECTION_HEADER_SIZE = 0x38
MAX_EXTRA_SECTIONS = 8

SECTION_RAW = 0x1000
SECTION_RAW_SIZE = 0x200
SECTION_VA = 0x00011000
SECTION_VSIZE = 0x400  # 0x200 of BSS with no backing in the file
SECTION_FLAGS = 0x06  # PRELOAD | EXECUTABLE
ENTRY_VA = SECTION_VA
THUNK_OFF = 0x1100
THUNK_VA = SECTION_VA + 0x100
THUNK_ORDINALS = (1, 8, 156)  # AvGetSavedDataAddress, DbgPrint, KeTickCount

INSERTED_RAW = 0x1200
INSERTED_SIZE = 0x40
INSERTED_VA = 0x00011400
INSERTED_TEXT = b"inserted_file_payload"

TITLE_ID = 0x5443000D  # 'TC' + 13
TIMEDATE = 0x4317F16E  # 2005-09-02T06:30:06Z
DEBUG_PATH = "c:\\Proj\\~output\\release\\Game.exe"

SECTION_STRINGS = (
    b"src\\engine\\player.cpp",
    b"assert failed: hp >= 0",
    b"chr_ryu.afs",
    b"STG_TOWER_05",
    b"ADXT/XBOX Ver.9.50 Build:May 16 2005",
    b".?AVCPlayer@Game@@",
)
# Looks like a path but does not end in NUL (0x99 follows): it must not be classified.
UNTERMINATED_STRING = b"data\\junk\\file.cpp"
STRINGS_END = 0x1100  # the strings cannot overwrite the thunk

# CodeView RSDS record pointed to by header field 0x180.
CODEVIEW_OFF = 0x1180
CODEVIEW_VA = SECTION_VA + 0x180
CODEVIEW_GUID = bytes.fromhex("88a5afbabb838f4692500560d06ec3a6")
CODEVIEW_PDB = b"c:\\Proj\\Game.pdb"

# XOR keys published at xboxdevwiki.net/Xbe: (entry point, kernel thunk).
XOR_KEYS = {
    "retail": (0xA8FC57AB, 0x5B6D40B6),
    "debug": (0x94859D4B, 0xEFB1F152),
}
ENTRY, THUNK = 0, 1


def alias_under(kind: str, va: int, field: int) -> int:
    """VA that key `kind` obtains from the field encoded with the retail key for `va`."""
    return va ^ XOR_KEYS["retail"][field] ^ XOR_KEYS[kind][field]


@dataclass(frozen=True)
class ExtraSection:
    """Extra section; its digest is computed over whichever file bytes exist."""

    name: bytes  # 7 characters at most
    va: int
    vsize: int
    raw: int
    raw_size: int
    flags: int = SECTION_FLAGS


def _section_header(
    data: bytes, *, flags: int, va: int, vsize: int, raw: int, raw_size: int, name: int
) -> bytes:
    """0x38-byte section header with the SHA-1 of `u32 raw_size || raw data`."""
    body = data[raw : raw + raw_size]
    digest = hashlib.sha1(struct.pack("<I", raw_size) + body).digest()
    return struct.pack("<9I", flags, va, vsize, raw, raw_size, name, 0, 0, 0) + digest


def build_synthetic_xbe(
    kind: str = "retail",
    *,
    extra_strings: tuple[bytes, ...] = (),
    text_tail: bytes = b"",
    text_flags: int = SECTION_FLAGS,
    inserted_file: bool = False,
    extra_sections: tuple[ExtraSection, ...] = (),
) -> bytes:
    """Header + certificate + .text section (thunk, strings, RSDS) + 1 library version.

    `extra_strings` are appended after the fixed strings; `text_tail` takes the last
    bytes of .text (before computing the digest); `inserted_file` adds an
    INSERTED_FILE section ($$XTIMAGE) right after .text in the file; `extra_sections`
    adds more sections at the end of the table.
    """
    if len(extra_sections) > MAX_EXTRA_SECTIONS:
        raise ValueError("too many extra sections for the synthetic layout")
    entry_key, thunk_key = XOR_KEYS[kind]
    file_size = INSERTED_RAW + INSERTED_SIZE if inserted_file else SECTION_RAW + SECTION_RAW_SIZE
    data = bytearray(file_size)
    section_count = 1 + int(inserted_file) + len(extra_sections)

    def put32(offset: int, value: int) -> None:
        struct.pack_into("<I", data, offset, value)

    def put(offset: int, raw: bytes) -> None:
        data[offset : offset + len(raw)] = raw

    # Image header.
    put(0, b"XBEH")
    header_values = {
        0x104: BASE,
        0x108: SIZE_OF_HEADERS,
        0x10C: SIZE_OF_IMAGE,
        0x110: 0x184,
        0x114: TIMEDATE,
        0x118: BASE + CERT_OFF,
        0x11C: section_count,
        0x120: BASE + SECTION_HEADERS_OFF,
        0x124: 0x5,
        0x128: ENTRY_VA ^ entry_key,
        0x12C: 0,
        0x130: 0x10000,
        0x14C: BASE + DEBUG_PATH_OFF,
        0x150: BASE + DEBUG_FILE_OFF,
        0x154: BASE + DEBUG_UFILE_OFF,
        0x158: THUNK_VA ^ thunk_key,
        0x160: 1,
        0x164: BASE + LIBRARY_OFF,
        0x168: 0,
        0x16C: BASE + LIBRARY_OFF,
        0x180: CODEVIEW_VA,
    }
    for offset, value in header_values.items():
        put32(offset, value)

    # Debug strings of the header.
    put(DEBUG_PATH_OFF, DEBUG_PATH.encode("ascii"))
    put(DEBUG_FILE_OFF, b"Game.exe")
    put(DEBUG_UFILE_OFF, "Game.exe".encode("utf-16-le"))

    # Certificate.
    put32(CERT_OFF + 0x00, CERT_SIZE)
    put32(CERT_OFF + 0x08, TITLE_ID)
    put(CERT_OFF + 0x0C, "NINJA GAIDEN Test".encode("utf-16-le"))
    put32(CERT_OFF + 0x5C, 0x54430003)  # alternate title id
    put32(CERT_OFF + 0x9C, 0x00000202)  # DVD_X2 | MEDIA_BOARD
    put32(CERT_OFF + 0xA0, 0x00000004)  # REST_OF_WORLD
    put32(CERT_OFF + 0xA8, 1)  # disk number
    put32(CERT_OFF + 0xAC, 3)  # version

    # .text data: thunk + strings + CodeView record.
    for index, ordinal in enumerate(THUNK_ORDINALS):
        put32(THUNK_OFF + 4 * index, 0x80000000 | ordinal)
    cursor = SECTION_RAW + 0x10
    for text in (*SECTION_STRINGS, *extra_strings):
        put(cursor, text)
        cursor += len(text) + 2
    put(cursor, UNTERMINATED_STRING + b"\x99")
    if cursor + len(UNTERMINATED_STRING) + 1 > STRINGS_END:
        raise ValueError("the synthetic strings overwrite the thunk")
    put(CODEVIEW_OFF, b"RSDS" + CODEVIEW_GUID + struct.pack("<I", 1) + CODEVIEW_PDB)
    put(SECTION_RAW + SECTION_RAW_SIZE - len(text_tail), text_tail)
    if inserted_file:
        put(INSERTED_RAW, INSERTED_TEXT)

    # Section table (digests over the data already written) and names.
    sections = [
        (text_flags, SECTION_VA, SECTION_VSIZE, SECTION_RAW, SECTION_RAW_SIZE, SECTION_NAME_OFF),
    ]
    put(SECTION_NAME_OFF, b".text")
    if inserted_file:
        sections.append(
            (0x08, INSERTED_VA, INSERTED_SIZE, INSERTED_RAW, INSERTED_SIZE, INSERTED_NAME_OFF)
        )
        put(INSERTED_NAME_OFF, b"$$XTIMAGE")
    for index, extra in enumerate(extra_sections):
        name_off = EXTRA_NAMES_OFF + 8 * index
        put(name_off, extra.name[:7])
        sections.append((extra.flags, extra.va, extra.vsize, extra.raw, extra.raw_size, name_off))
    snapshot = bytes(data)
    for index, (flags, va, vsize, raw, raw_size, name_off) in enumerate(sections):
        header = _section_header(
            snapshot,
            flags=flags,
            va=va,
            vsize=vsize,
            raw=raw,
            raw_size=raw_size,
            name=BASE + name_off,
        )
        put(SECTION_HEADERS_OFF + index * SECTION_HEADER_SIZE, header)

    # Library version: XAPILIB 1.0.5849, QFE 1, approved=2, no debug.
    put(LIBRARY_OFF, b"XAPILIB")
    struct.pack_into("<4H", data, LIBRARY_OFF + 8, 1, 0, 5849, (2 << 13) | 1)
    return bytes(data)


def corrupt_text_section(data: bytes) -> bytes:
    """Flip a byte of .text without updating its digest (simulates a tampered XBE)."""
    corrupted = bytearray(data)
    corrupted[SECTION_RAW + 0x1F0] ^= 0xFF
    return bytes(corrupted)


def with_u32(data: bytes, offset: int, value: int) -> bytes:
    """Copy of `data` with a little-endian u32 overwritten at `offset`."""
    patched = bytearray(data)
    struct.pack_into("<I", patched, offset, value)
    return bytes(patched)
