"""Executable synthetic XBE to test the native loader (never game data).

A minimal `.text` at 0x11000 calls kernel imports and returns. Everything is written by hand
here (`ngb_tools.xbe` is not imported), so the builder does not inherit parser bugs.

Layout (file offsets; VA = 0x10000 + offset in the headers):

    0x000  image header (0x178 bytes)
    0x200  ".text" name
    0x400  header of the only section
    0x1000 .text (VA 0x11000): code at 0x11000, thunk at 0x11800, data at 0x11900
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
TEST_STR_VA = TEXT_VA + 0x940  # "test"
ANSI_VA = TEXT_VA + 0x980  # 8-byte ANSI_STRING filled in by RtlInitAnsiString
XOR_ENTRY_RETAIL = 0xA8FC57AB
XOR_THUNK_RETAIL = 0x5B6D40B6
SECTION_FLAGS = 0x07  # WRITABLE | PRELOAD | EXECUTABLE

# Ordinals imported by the synthetic XBE (order = thunk slots).
KE_QUERY_PERFORMANCE_FREQUENCY = 127
DBG_PRINT = 8
KE_TICK_COUNT = 156  # variable (data)
RTL_INIT_ANSI_STRING = 289
HAL_READ_SMC_TRAY_STATE = 9  # not implemented in the loader: used for the halt
DEFAULT_IMPORTS = (KE_QUERY_PERFORMANCE_FREQUENCY, DBG_PRINT, KE_TICK_COUNT, RTL_INIT_ANSI_STRING)


def slot(index: int) -> int:
    """VA of thunk slot `index`."""
    return THUNK_VA + 4 * index


def call_slot(index: int) -> bytes:
    """`call dword ptr [slot]` (FF 15 disp32)."""
    return b"\xff\x15" + struct.pack("<I", slot(index))


def push_imm32(value: int) -> bytes:
    return b"\x68" + struct.pack("<I", value)


def boot_code() -> bytes:
    """Call 3 functions (stdcall and cdecl), read a kernel variable and return."""
    return b"".join(
        (
            call_slot(0),  # KeQueryPerformanceFrequency() -> EDX:EAX
            b"\x6a\x2a",  # push 42
            push_imm32(FMT_VA),
            call_slot(1),  # DbgPrint("ngb synthetic %d\n", 42): cdecl
            b"\x83\xc4\x08",  # add esp, 8
            push_imm32(TEST_STR_VA),
            push_imm32(ANSI_VA),
            call_slot(3),  # RtlInitAnsiString(&ansi, "test"): stdcall, callee cleans up
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
    """Read an MMIO register (`mov eax, [addr]`) and return."""
    return b"\xa1" + struct.pack("<I", addr) + b"\xc3"


def unimplemented_code() -> bytes:
    """Call HalReadSMCTrayState (slot 0), which the loader does not implement."""
    return b"\x6a\x00\x6a\x00" + call_slot(0) + b"\xc3"


# --- H7a: GPU test (NV2A registers and a push buffer) -----------------------
NV2A = 0xFD000000
PB_VA = TEXT_VA + 0xA00  # push buffer inside .text (writable)
GPU_FMT_VA = TEXT_VA + 0xA80  # "boot=%08x get=%08x\n"
BOOT_VA = TEXT_VA + 0xAC0  # copy of PMC_BOOT_0
DMA_INSTANCE = 0x100  # DMA object at RAMIN + 0x1000
PHYS_XBE = 0x06000000  # "physical" window of the XBE image (port/src/ngb.h)
PB_WORDS = (
    (1 << 18) | 0x1D90,  # SET_COLOR_CLEAR_VALUE
    0xFF336699,
    (1 << 18) | 0x1D94,  # CLEAR_SURFACE
    0x000000F0,
    (1 << 18) | 0x0130,  # FLIP_STALL
    0,
)
PB_GET = PB_VA - BASE  # offsets within the DMA object, which starts at the XBE base
PB_PUT = PB_GET + 4 * len(PB_WORDS)


def mov_mem_imm(addr: int, value: int) -> bytes:
    """`mov dword ptr [addr], imm32` (C7 05)."""
    return b"\xc7\x05" + struct.pack("<II", addr, value)


def gpu_code() -> bytes:
    """Read PMC_BOOT_0, wait for a PFB flush, set up a DMA object and advance DMA_PUT."""
    wbc = NV2A + 0x100410
    return b"".join(
        (
            b"\xa1" + struct.pack("<I", NV2A),  # mov eax, [PMC_BOOT_0]
            b"\xa3" + struct.pack("<I", BOOT_VA),  # mov [BOOT_VA], eax
            mov_mem_imm(wbc, 0x10000),  # PFB_WBC: request the flush
            b"\xf7\x05" + struct.pack("<II", wbc, 0x10000),  # test [PFB_WBC], bit 16
            b"\x75\xf4",  # jnz: until the model clears it
            mov_mem_imm(NV2A + 0x701000, 0x0000003D),  # RAMIN: DMA object (class 0x3D)
            mov_mem_imm(NV2A + 0x701004, 0x00FFFFFF),  # limit
            mov_mem_imm(NV2A + 0x701008, PHYS_XBE + BASE),  # base: the XBE image
            mov_mem_imm(NV2A + 0x00322C, DMA_INSTANCE),  # PFIFO_CACHE1_DMA_INSTANCE
            mov_mem_imm(NV2A + 0x800044, PB_GET),  # USER DMA_GET
            mov_mem_imm(NV2A + 0x800040, PB_PUT),  # USER DMA_PUT: decodes the push buffer
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
    """Data of the GPU test XBE: push buffer and format string."""
    return {
        PB_VA: b"".join(struct.pack("<I", w) for w in PB_WORDS),
        GPU_FMT_VA: b"boot=%08x get=%08x\n\0",
    }


# --- Thread exit: the ETHREAD is signalled with its exit code -------------------
PS_CREATE_SYSTEM_THREAD_EX = 255
PS_TERMINATE_SYSTEM_THREAD = 258
OB_REFERENCE_OBJECT_BY_HANDLE = 246
KE_WAIT_FOR_SINGLE_OBJECT = 159
THREAD_IMPORTS = (
    PS_CREATE_SYSTEM_THREAD_EX,
    PS_TERMINATE_SYSTEM_THREAD,
    OB_REFERENCE_OBJECT_BY_HANDLE,
    KE_WAIT_FOR_SINGLE_OBJECT,
    DBG_PRINT,
)
THREAD_EXIT_STATUS = 0x1234
THREAD_ROUTINE_VA = TEXT_VA + 0x300
THREAD_HANDLE_VA = TEXT_VA + 0xB00
THREAD_ETHREAD_VA = TEXT_VA + 0xB04
THREAD_FMT_VA = TEXT_VA + 0xB10  # "exit signal=%d status=%x\n"


def thread_exit_code() -> bytes:
    """Create a thread that ends with PsTerminateSystemThread(0x1234), wait for it with
    KeWaitForSingleObject on its ETHREAD and print SignalState (+4) and ExitStatus (+0x120),
    which is what XAPI's GetExitCodeThread reads."""
    return b"".join(
        (
            b"\x6a\x00" * 4,  # SystemRoutine, DebuggerThread, CreateSuspended, StartContext
            push_imm32(THREAD_ROUTINE_VA),
            b"\x6a\x00\x6a\x00",  # ThreadId, TlsDataSize
            push_imm32(0x4000),  # KernelStackSize
            b"\x6a\x00",  # ThreadExtensionSize
            push_imm32(THREAD_HANDLE_VA),
            call_slot(0),  # PsCreateSystemThreadEx (stdcall, 10 arguments)
            push_imm32(THREAD_ETHREAD_VA),
            b"\x6a\x00",
            b"\xff\x35" + struct.pack("<I", THREAD_HANDLE_VA),  # push dword [handle]
            call_slot(2),  # ObReferenceObjectByHandle(handle, 0, &ethread)
            b"\x6a\x00" * 4,  # Timeout NULL (infinite), Alertable, WaitMode, WaitReason
            b"\xff\x35" + struct.pack("<I", THREAD_ETHREAD_VA),  # push dword [ethread]
            call_slot(3),  # KeWaitForSingleObject(ethread, ...)
            b"\xa1" + struct.pack("<I", THREAD_ETHREAD_VA),  # mov eax, [ethread]
            b"\xff\xb0\x20\x01\x00\x00",  # push dword [eax+0x120]  (ExitStatus)
            b"\xff\x70\x04",  # push dword [eax+4]  (Header.SignalState)
            push_imm32(THREAD_FMT_VA),
            call_slot(4),  # DbgPrint (cdecl)
            b"\x83\xc4\x0c",  # add esp, 12
            b"\xc3",
        )
    )


def thread_exit_extra() -> dict[int, bytes]:
    """Thread routine (stdcall, 1 argument) and format string."""
    # call_slot(1) is PsTerminateSystemThread: it does not return
    routine = push_imm32(THREAD_EXIT_STATUS) + call_slot(1)
    return {THREAD_ROUTINE_VA: routine, THREAD_FMT_VA: b"exit signal=%d status=%x\n\0"}


def build_loader_xbe(
    code: bytes, imports: Sequence[int] = DEFAULT_IMPORTS, extra: dict[int, bytes] | None = None
) -> bytes:
    """Retail XBE with one .text section: `code` at 0x11000 and the thunk of `imports`.

    `extra` writes more data into .text (VA -> bytes).
    """
    if len(code) > THUNK_VA - TEXT_VA:
        raise ValueError("the code overwrites the thunk")
    data = bytearray(TEXT_RAW + TEXT_SIZE)

    def put32(offset: int, value: int) -> None:
        struct.pack_into("<I", data, offset, value)

    data[0:4] = b"XBEH"
    header = {
        0x104: BASE,
        0x108: SIZE_OF_HEADERS,
        0x10C: SIZE_OF_IMAGE,
        0x110: 0x178,
        0x118: 0,  # no certificate
        0x11C: 1,
        0x120: BASE + 0x400,
        0x128: TEXT_VA ^ XOR_ENTRY_RETAIL,
        0x12C: 0,  # no TLS
        0x130: 0x10000,
        0x158: THUNK_VA ^ XOR_THUNK_RETAIL,
    }
    for offset, value in header.items():
        put32(offset, value)
    data[0x200:0x206] = b".text\0"
    section = struct.pack(
        "<9I", SECTION_FLAGS, TEXT_VA, TEXT_SIZE, TEXT_RAW, TEXT_SIZE, BASE + 0x200, 0, 0, 0
    )
    data[0x400 : 0x400 + len(section)] = section  # the SHA-1 digest (20 B) stays zero

    def put_text(va: int, raw: bytes) -> None:
        offset = TEXT_RAW + va - TEXT_VA
        data[offset : offset + len(raw)] = raw

    put_text(TEXT_VA, code)
    put_text(THUNK_VA, b"".join(struct.pack("<I", 0x80000000 | o) for o in imports) + b"\0" * 4)
    put_text(FMT_VA, b"ngb synthetic %d\n\0")
    put_text(FMT2_VA, b"len=%u\n\0")
    put_text(TEST_STR_VA, b"test\0")
    for va, raw in (extra or {}).items():
        put_text(va, raw)
    return bytes(data)
