"""XBE sintético ejecutable para probar el loader nativo (nunca datos del juego).

Un `.text` mínimo en 0x11000 llama a imports del kernel y retorna. Todo se escribe a mano
aquí (no se importa `ngb_tools.xbe`), así que el builder no hereda errores del parser.

Layout (offsets de archivo; VA = 0x10000 + offset en las cabeceras):

    0x000  cabecera de imagen (0x178 bytes)
    0x200  nombre ".text"
    0x400  cabecera de la única sección
    0x1000 .text (VA 0x11000): código en 0x11000, thunk en 0x11800, datos en 0x11900
"""

from __future__ import annotations

import struct
from collections.abc import Sequence

BASE = 0x00010000
SIZE_OF_HEADERS = 0x1000
TEXT_VA = 0x00011000
TEXT_RAW = 0x1000
TEXT_SIZE = 0x1000
SIZE_OF_IMAGE = 0x2000
THUNK_VA = TEXT_VA + 0x800
FMT_VA = TEXT_VA + 0x900  # "ngb synthetic %d\n"
FMT2_VA = TEXT_VA + 0x920  # "len=%u\n"
HOLA_VA = TEXT_VA + 0x940  # "hola"
ANSI_VA = TEXT_VA + 0x980  # ANSI_STRING de 8 bytes que rellena RtlInitAnsiString
XOR_ENTRY_RETAIL = 0xA8FC57AB
XOR_THUNK_RETAIL = 0x5B6D40B6
SECTION_FLAGS = 0x07  # WRITABLE | PRELOAD | EXECUTABLE

# Ordinales que importa el XBE sintético (orden = slots del thunk).
KE_QUERY_PERFORMANCE_FREQUENCY = 127
DBG_PRINT = 8
KE_TICK_COUNT = 156  # variable (data)
RTL_INIT_ANSI_STRING = 289
HAL_READ_SMC_TRAY_STATE = 9  # sin implementar en el loader: sirve para la parada
DEFAULT_IMPORTS = (KE_QUERY_PERFORMANCE_FREQUENCY, DBG_PRINT, KE_TICK_COUNT, RTL_INIT_ANSI_STRING)


def slot(index: int) -> int:
    """VA del slot `index` del thunk."""
    return THUNK_VA + 4 * index


def call_slot(index: int) -> bytes:
    """`call dword ptr [slot]` (FF 15 disp32)."""
    return b"\xff\x15" + struct.pack("<I", slot(index))


def push_imm32(value: int) -> bytes:
    return b"\x68" + struct.pack("<I", value)


def boot_code() -> bytes:
    """Llama a 3 funciones (stdcall y cdecl), lee una variable del kernel y retorna."""
    return b"".join(
        (
            call_slot(0),  # KeQueryPerformanceFrequency() -> EDX:EAX
            b"\x6a\x2a",  # push 42
            push_imm32(FMT_VA),
            call_slot(1),  # DbgPrint("ngb synthetic %d\n", 42): cdecl
            b"\x83\xc4\x08",  # add esp, 8
            push_imm32(HOLA_VA),
            push_imm32(ANSI_VA),
            call_slot(3),  # RtlInitAnsiString(&ansi, "hola"): stdcall, limpia el llamado
            b"\x0f\xb7\x05" + struct.pack("<I", ANSI_VA),  # movzx eax, word [ansi.Length]
            b"\x50",  # push eax
            push_imm32(FMT2_VA),
            call_slot(1),  # DbgPrint("len=%u\n", 4)
            b"\x83\xc4\x08",
            b"\xa1" + struct.pack("<I", slot(2)),  # mov eax, [slot KeTickCount] (&KeTickCount)
            b"\x8b\x00",  # mov eax, [eax]
            b"\xc3",  # ret
        )
    )


def mmio_code() -> bytes:
    """Lee un registro del NV2A: el loader debe parar con un crash clasificado como MMIO."""
    return b"\xa1" + struct.pack("<I", 0xFD000000) + b"\xc3"


def unimplemented_code() -> bytes:
    """Llama a HalReadSMCTrayState (slot 0), que el loader no implementa."""
    return b"\x6a\x00\x6a\x00" + call_slot(0) + b"\xc3"


def build_loader_xbe(code: bytes, imports: Sequence[int] = DEFAULT_IMPORTS) -> bytes:
    """XBE retail con una sección .text: `code` en 0x11000 y el thunk de `imports`."""
    if len(code) > THUNK_VA - TEXT_VA:
        raise ValueError("el código pisa el thunk")
    data = bytearray(TEXT_RAW + TEXT_SIZE)

    def put32(offset: int, value: int) -> None:
        struct.pack_into("<I", data, offset, value)

    data[0:4] = b"XBEH"
    header = {
        0x104: BASE,
        0x108: SIZE_OF_HEADERS,
        0x10C: SIZE_OF_IMAGE,
        0x110: 0x178,
        0x118: 0,  # sin certificado
        0x11C: 1,
        0x120: BASE + 0x400,
        0x128: TEXT_VA ^ XOR_ENTRY_RETAIL,
        0x12C: 0,  # sin TLS
        0x130: 0x10000,
        0x158: THUNK_VA ^ XOR_THUNK_RETAIL,
    }
    for offset, value in header.items():
        put32(offset, value)
    data[0x200:0x206] = b".text\0"
    section = struct.pack(
        "<9I", SECTION_FLAGS, TEXT_VA, TEXT_SIZE, TEXT_RAW, TEXT_SIZE, BASE + 0x200, 0, 0, 0
    )
    data[0x400 : 0x400 + len(section)] = section  # el digest SHA-1 (20 B) queda a cero

    def put_text(va: int, raw: bytes) -> None:
        offset = TEXT_RAW + va - TEXT_VA
        data[offset : offset + len(raw)] = raw

    put_text(TEXT_VA, code)
    put_text(THUNK_VA, b"".join(struct.pack("<I", 0x80000000 | o) for o in imports) + b"\0" * 4)
    put_text(FMT_VA, b"ngb synthetic %d\n\0")
    put_text(FMT2_VA, b"len=%u\n\0")
    put_text(HOLA_VA, b"hola\0")
    return bytes(data)
