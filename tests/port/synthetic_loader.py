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


def mmio_code(addr: int = 0xFD000000) -> bytes:
    """Lee un registro MMIO (`mov eax, [addr]`) y retorna."""
    return b"\xa1" + struct.pack("<I", addr) + b"\xc3"


def unimplemented_code() -> bytes:
    """Llama a HalReadSMCTrayState (slot 0), que el loader no implementa."""
    return b"\x6a\x00\x6a\x00" + call_slot(0) + b"\xc3"


# --- H7a: prueba de la GPU (registros del NV2A y un push buffer) -----------------------
NV2A = 0xFD000000
PB_VA = TEXT_VA + 0xA00  # push buffer dentro de .text (escribible)
GPU_FMT_VA = TEXT_VA + 0xA80  # "boot=%08x get=%08x\n"
BOOT_VA = TEXT_VA + 0xAC0  # copia de PMC_BOOT_0
DMA_INSTANCE = 0x100  # objeto DMA en RAMIN + 0x1000
PHYS_XBE = 0x06000000  # ventana "física" de la imagen del XBE (port/src/ngb.h)
PB_WORDS = (
    (1 << 18) | 0x1D90,  # SET_COLOR_CLEAR_VALUE
    0xFF336699,
    (1 << 18) | 0x1D94,  # CLEAR_SURFACE
    0x000000F0,
    (1 << 18) | 0x0130,  # FLIP_STALL
    0,
)
PB_GET = PB_VA - BASE  # offsets dentro del objeto DMA, que empieza en la base del XBE
PB_PUT = PB_GET + 4 * len(PB_WORDS)


def mov_mem_imm(addr: int, value: int) -> bytes:
    """`mov dword ptr [addr], imm32` (C7 05)."""
    return b"\xc7\x05" + struct.pack("<II", addr, value)


def gpu_code() -> bytes:
    """Lee PMC_BOOT_0, espera un flush de PFB, monta un objeto DMA y avanza DMA_PUT."""
    wbc = NV2A + 0x100410
    return b"".join(
        (
            b"\xa1" + struct.pack("<I", NV2A),  # mov eax, [PMC_BOOT_0]
            b"\xa3" + struct.pack("<I", BOOT_VA),  # mov [BOOT_VA], eax
            mov_mem_imm(wbc, 0x10000),  # PFB_WBC: pide el flush
            b"\xf7\x05" + struct.pack("<II", wbc, 0x10000),  # test [PFB_WBC], bit 16
            b"\x75\xf4",  # jnz: hasta que el modelo lo limpie
            mov_mem_imm(NV2A + 0x701000, 0x0000003D),  # RAMIN: objeto DMA (clase 0x3D)
            mov_mem_imm(NV2A + 0x701004, 0x00FFFFFF),  # límite
            mov_mem_imm(NV2A + 0x701008, PHYS_XBE + BASE),  # base: la imagen del XBE
            mov_mem_imm(NV2A + 0x00322C, DMA_INSTANCE),  # PFIFO_CACHE1_DMA_INSTANCE
            mov_mem_imm(NV2A + 0x800044, PB_GET),  # USER DMA_GET
            mov_mem_imm(NV2A + 0x800040, PB_PUT),  # USER DMA_PUT: decodifica el push buffer
            b"\xa1" + struct.pack("<I", NV2A + 0x800044),  # mov eax, [DMA_GET]
            b"\x50",  # push eax
            b"\xff\x35" + struct.pack("<I", BOOT_VA),  # push [BOOT_VA]
            push_imm32(GPU_FMT_VA),
            call_slot(0),  # DbgPrint("boot=%08x get=%08x\n", boot, get)
            b"\x83\xc4\x0c",  # add esp, 12
            b"\xc3",  # ret
        )
    )


def gpu_extra() -> dict[int, bytes]:
    """Datos del XBE de prueba de la GPU: push buffer y cadena de formato."""
    return {
        PB_VA: b"".join(struct.pack("<I", w) for w in PB_WORDS),
        GPU_FMT_VA: b"boot=%08x get=%08x\n\0",
    }


def build_loader_xbe(
    code: bytes, imports: Sequence[int] = DEFAULT_IMPORTS, extra: dict[int, bytes] | None = None
) -> bytes:
    """XBE retail con una sección .text: `code` en 0x11000 y el thunk de `imports`.

    `extra` escribe más datos en .text (VA -> bytes).
    """
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
    for va, raw in (extra or {}).items():
        put_text(va, raw)
    return bytes(data)
