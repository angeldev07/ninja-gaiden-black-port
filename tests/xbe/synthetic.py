"""XBE sintético construido en memoria para los tests.

Todo valor esperado sale de este archivo, escrito a mano: el builder no importa
nada de `ngb_tools.xbe`, así un error del código bajo prueba no se cuela en
las expectativas.

Layout (offsets de archivo; en la cabecera, VA = BASE + offset):

    0x000  cabecera de imagen (0x184 bytes: incluye el campo 0x180 de CodeView)
    0x200  certificado (0x1EC bytes)
    0x480  nombre ".text" / 0x488 nombre "$$XTIMAGE"
    0x4A0  library version XAPILIB
    0x4C0  ruta de depuración / 0x500 nombre / 0x510 nombre UTF-16
    0x540  nombres de las secciones extra (8 bytes cada uno)
    0x800  cabeceras de sección (0x38 bytes cada una): .text, $$XTIMAGE, extras
    0x1000 .text: cadenas en 0x1010, thunk en 0x1100, registro RSDS en 0x1180
    0x1200 $$XTIMAGE (solo con `inserted_file=True`)
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
SECTION_VSIZE = 0x400  # 0x200 de BSS sin respaldo en el archivo
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
# Parece una ruta pero no acaba en NUL (le sigue 0x99): no debe clasificarse.
UNTERMINATED_STRING = b"data\\junk\\file.cpp"
STRINGS_END = 0x1100  # las cadenas no pueden pisar el thunk

# Registro CodeView RSDS apuntado por el campo 0x180 de la cabecera.
CODEVIEW_OFF = 0x1180
CODEVIEW_VA = SECTION_VA + 0x180
CODEVIEW_GUID = bytes.fromhex("88a5afbabb838f4692500560d06ec3a6")
CODEVIEW_PDB = b"c:\\Proj\\Game.pdb"

# Claves XOR publicadas en xboxdevwiki.net/Xbe: (entry point, kernel thunk).
XOR_KEYS = {
    "retail": (0xA8FC57AB, 0x5B6D40B6),
    "debug": (0x94859D4B, 0xEFB1F152),
}
ENTRY, THUNK = 0, 1


def alias_under(kind: str, va: int, field: int) -> int:
    """VA que obtiene la clave `kind` del campo codificado con la clave retail para `va`."""
    return va ^ XOR_KEYS["retail"][field] ^ XOR_KEYS[kind][field]


@dataclass(frozen=True)
class ExtraSection:
    """Sección adicional; su digest se calcula sobre los bytes del archivo que existan."""

    name: bytes  # 7 caracteres como máximo
    va: int
    vsize: int
    raw: int
    raw_size: int
    flags: int = SECTION_FLAGS


def _section_header(
    data: bytes, *, flags: int, va: int, vsize: int, raw: int, raw_size: int, name: int
) -> bytes:
    """Cabecera de sección de 0x38 bytes con el SHA-1 de `u32 raw_size || datos crudos`."""
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
    """Cabecera + certificado + sección .text (thunk, cadenas, RSDS) + 1 library version.

    `extra_strings` se añaden tras las cadenas fijas; `text_tail` ocupa los últimos
    bytes de .text (antes de calcular el digest); `inserted_file` añade una sección
    INSERTED_FILE ($$XTIMAGE) justo detrás de .text en el archivo; `extra_sections`
    añade más secciones al final de la tabla.
    """
    if len(extra_sections) > MAX_EXTRA_SECTIONS:
        raise ValueError("demasiadas secciones extra para el layout sintético")
    entry_key, thunk_key = XOR_KEYS[kind]
    file_size = INSERTED_RAW + INSERTED_SIZE if inserted_file else SECTION_RAW + SECTION_RAW_SIZE
    data = bytearray(file_size)
    section_count = 1 + int(inserted_file) + len(extra_sections)

    def put32(offset: int, value: int) -> None:
        struct.pack_into("<I", data, offset, value)

    def put(offset: int, raw: bytes) -> None:
        data[offset : offset + len(raw)] = raw

    # Cabecera de imagen.
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

    # Cadenas de depuración de la cabecera.
    put(DEBUG_PATH_OFF, DEBUG_PATH.encode("ascii"))
    put(DEBUG_FILE_OFF, b"Game.exe")
    put(DEBUG_UFILE_OFF, "Game.exe".encode("utf-16-le"))

    # Certificado.
    put32(CERT_OFF + 0x00, CERT_SIZE)
    put32(CERT_OFF + 0x08, TITLE_ID)
    put(CERT_OFF + 0x0C, "NINJA GAIDEN Test".encode("utf-16-le"))
    put32(CERT_OFF + 0x5C, 0x54430003)  # alternate title id
    put32(CERT_OFF + 0x9C, 0x00000202)  # DVD_X2 | MEDIA_BOARD
    put32(CERT_OFF + 0xA0, 0x00000004)  # REST_OF_WORLD
    put32(CERT_OFF + 0xA8, 1)  # disk number
    put32(CERT_OFF + 0xAC, 3)  # version

    # Datos de .text: thunk + cadenas + registro CodeView.
    for index, ordinal in enumerate(THUNK_ORDINALS):
        put32(THUNK_OFF + 4 * index, 0x80000000 | ordinal)
    cursor = SECTION_RAW + 0x10
    for text in (*SECTION_STRINGS, *extra_strings):
        put(cursor, text)
        cursor += len(text) + 2
    put(cursor, UNTERMINATED_STRING + b"\x99")
    if cursor + len(UNTERMINATED_STRING) + 1 > STRINGS_END:
        raise ValueError("las cadenas sintéticas pisan el thunk")
    put(CODEVIEW_OFF, b"RSDS" + CODEVIEW_GUID + struct.pack("<I", 1) + CODEVIEW_PDB)
    put(SECTION_RAW + SECTION_RAW_SIZE - len(text_tail), text_tail)
    if inserted_file:
        put(INSERTED_RAW, INSERTED_TEXT)

    # Tabla de secciones (digests sobre los datos ya escritos) y nombres.
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
    """Cambia un byte de .text sin actualizar su digest (simula un XBE alterado)."""
    corrupted = bytearray(data)
    corrupted[SECTION_RAW + 0x1F0] ^= 0xFF
    return bytes(corrupted)


def with_u32(data: bytes, offset: int, value: int) -> bytes:
    """Copia de `data` con un u32 little-endian sobrescrito en `offset`."""
    patched = bytearray(data)
    struct.pack_into("<I", patched, offset, value)
    return bytes(patched)
