"""Synthetic XBE to test the XAPI input HLE (H8), without game data.

The loader only patches an XAPI function if its first bytes at its address are the
expected ones and its epilogue holds the expected `ret N` (port/src/input/xapi_hle.c). This
module adds to the `synthetic_loader` XBE a second section "XPP" that reproduces only those
signatures (eight prologue bytes and `C2 N 00` per function: what the loader checks, not the
XAPI code), and a `.text` that calls the patched addresses and writes the result with DbgPrint.
"""

from __future__ import annotations

import struct

from .synthetic_loader import (
    BASE,
    DBG_PRINT,
    TEXT_RAW,
    TEXT_SIZE,
    TEXT_VA,
    build_loader_xbe,
    call_slot,
    push_imm32,
)

XPP_VA = 0x003DB000
XPP_SIZE = 0x4000
XPP_RAW = TEXT_RAW + TEXT_SIZE
GAMEPAD = 0x003DBD54  # XDEVICE_TYPE_GAMEPAD (&XDEVICE_TYPE_GAMEPAD_TABLE)

# Signature of each function (copy of the xapi_hle.c table): VA, 8 bytes, VA of the ret and N.
SIGNATURES = {
    "XGetDevices": (0x003DCC99, "56 FF 15 84 3B 3E 00 8B", 0x003DCCB8, 4),
    "XGetDeviceChanges": (0x003DCCBB, "55 8B EC 56 8B 75 08 33", 0x003DCD25, 12),
    "XPeekDevices": (0x003DCC58, "56 57 FF 15 84 3B 3E 00", 0x003DCC91, 12),
    "XInputOpen": (0x003DDFF1, "55 8B EC 51 8B 4D 08 83", 0x003DE044, 16),
    "XInputClose": (0x003DE047, "8B 4C 24 04 E8 E9 38 00", 0x003DE050, 4),
    "XInputGetCapabilities": (0x003DE053, "55 8B EC 83 EC 48 83 65", 0x003DE228, 8),
    "XInputGetState": (0x003DE22B, "53 56 33 DB FF 15 84 3B", 0x003DE29B, 8),
    "XInputSetState": (0x003DE29E, "8B 4C 24 04 8D 81 A3 00", 0x003DE2CE, 8),
}

# Data in .text (writable): format strings and the output buffers.
FMT_CHANGES_VA = TEXT_VA + 0xA00  # "changes=%u ins=%x rem=%x\n"
FMT_DEVICES_VA = TEXT_VA + 0xA40  # "devices=%x\n"
FMT_STATE_VA = TEXT_VA + 0xA60  # "state=%x pkt=%u btn=%04x a=%u b=%u lt=%u\n"
FMT_FEEDBACK_VA = TEXT_VA + 0xAA0  # "feedback=%x status=%x\n"
INS_VA = TEXT_VA + 0xB00  # DWORD insertions, DWORD removals
STATE_VA = TEXT_VA + 0xB10  # XINPUT_STATE (22 bytes)
FEEDBACK_VA = TEXT_VA + 0xB40  # XINPUT_FEEDBACK (header + data)


def call_va(at: int, target: int) -> bytes:
    """`call rel32` from VA `at` to `target`."""
    return b"\xe8" + struct.pack("<i", target - (at + 5))


def input_code() -> bytes:
    """XGetDeviceChanges, XGetDevices, XInputOpen, XInputGetState and XInputSetState.

    Prints the results with DbgPrint (slot 0, cdecl). If a `ret N` did not match the
    number of arguments, the stack would be unbalanced and the code would not reach the end.
    """
    out = bytearray()

    def emit(raw: bytes) -> None:
        out.extend(raw)

    def call(name: str) -> None:
        emit(call_va(TEXT_VA + len(out), SIGNATURES[name][0]))

    def dbg(fmt_va: int, nargs: int) -> None:
        emit(push_imm32(fmt_va) + call_slot(0) + b"\x83\xc4" + bytes([4 * (nargs + 1)]))

    emit(b"\x53\x56")  # push ebx; push esi (callee-saved registers used below)
    # XGetDeviceChanges(GAMEPAD, &ins, &rem) -> eax
    emit(push_imm32(INS_VA + 4) + push_imm32(INS_VA) + push_imm32(GAMEPAD))
    call("XGetDeviceChanges")
    emit(b"\xff\x35" + struct.pack("<I", INS_VA + 4))  # push [rem]
    emit(b"\xff\x35" + struct.pack("<I", INS_VA))  # push [ins]
    emit(b"\x50")  # push eax
    dbg(FMT_CHANGES_VA, 3)
    # XGetDevices(GAMEPAD) -> port mask
    emit(push_imm32(GAMEPAD))
    call("XGetDevices")
    emit(b"\x50")
    dbg(FMT_DEVICES_VA, 1)
    # XInputOpen(GAMEPAD, port 0, slot 0, NULL) -> handle in ebx
    emit(b"\x6a\x00\x6a\x00\x6a\x00" + push_imm32(GAMEPAD))
    call("XInputOpen")
    emit(b"\x8b\xd8")  # mov ebx, eax
    # XInputGetState(handle, &state) -> esi
    emit(push_imm32(STATE_VA) + b"\x53")
    call("XInputGetState")
    emit(b"\x8b\xf0")  # mov esi, eax
    for offset in (4 + 2 + 6, 4 + 2 + 1, 4 + 2 + 0):  # LT, B, A (bAnalogButtons)
        emit(b"\x0f\xb6\x05" + struct.pack("<I", STATE_VA + offset) + b"\x50")
    emit(b"\x0f\xb7\x05" + struct.pack("<I", STATE_VA + 4) + b"\x50")  # wButtons
    emit(b"\xff\x35" + struct.pack("<I", STATE_VA))  # dwPacketNumber
    emit(b"\x56")  # push esi
    dbg(FMT_STATE_VA, 6)
    # XInputSetState(handle, &feedback): rumble ignored, completes immediately
    emit(b"\xc7\x05" + struct.pack("<II", FEEDBACK_VA, 0xDEADBEEF))  # dwStatus untouched
    emit(push_imm32(FEEDBACK_VA) + b"\x53")
    call("XInputSetState")
    emit(b"\xff\x35" + struct.pack("<I", FEEDBACK_VA) + b"\x50")
    dbg(FMT_FEEDBACK_VA, 2)
    emit(b"\x5e\x5b\xc3")  # pop esi; pop ebx; ret
    return bytes(out)


def input_extra() -> dict[int, bytes]:
    return {
        FMT_CHANGES_VA: b"changes=%u ins=%x rem=%x\n\0",
        FMT_DEVICES_VA: b"devices=%x\n\0",
        FMT_STATE_VA: b"state=%x pkt=%u btn=%04x a=%u b=%u lt=%u\n\0",
        FMT_FEEDBACK_VA: b"feedback=%x status=%x\n\0",
    }


def xpp_section(signatures: dict[str, tuple[int, str, int, int]] = SIGNATURES) -> bytes:
    """Section with the signatures the loader checks (int3 elsewhere)."""
    data = bytearray(b"\xcc" * XPP_SIZE)
    for va, prologue, ret_va, ret_n in signatures.values():
        raw = bytes.fromhex(prologue)
        data[va - XPP_VA : va - XPP_VA + len(raw)] = raw
        data[ret_va - XPP_VA : ret_va - XPP_VA + 3] = bytes((0xC2, ret_n, 0))
    return bytes(data)


def build_input_xbe(code: bytes, xpp: bytes | None = None) -> bytes:
    """`synthetic_loader` XBE with `code` in .text, imports [DbgPrint] and the XPP section."""
    data = bytearray(build_loader_xbe(code, imports=(DBG_PRINT,), extra=input_extra()))
    xpp = xpp_section() if xpp is None else xpp
    data[0x10C:0x110] = struct.pack("<I", XPP_VA + XPP_SIZE - BASE)  # SizeOfImage
    data[0x11C:0x120] = struct.pack("<I", 2)  # two sections
    data[0x210:0x214] = b"XPP\0"
    header = struct.pack("<9I", 0x07, XPP_VA, XPP_SIZE, XPP_RAW, XPP_SIZE, BASE + 0x210, 0, 0, 0)
    data[0x400 + 0x38 : 0x400 + 0x38 + len(header)] = header
    if len(data) != XPP_RAW:
        raise ValueError("the base XBE does not end where the XPP section begins")
    return bytes(data) + xpp
