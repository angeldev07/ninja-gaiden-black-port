"""Strict decoder for the push buffers of a KMD: a triangle strip and nothing else.

A push buffer is a set of commands already recorded for the NV2A GPU. Each command is a u32
header followed by its arguments:

    bits 2-12   method (register address of the Kelvin class, NV097)
    bits 13-15  subchannel (always 0 here)
    bits 18-28  argument count
    bit 30      non-incrementing: all arguments go to the same method

Jump or call headers (bits 0-1 or 29) are not supported. In `ryu.kmd` the only push buffer
is exactly `SET_BEGIN_END(TRIANGLE_STRIP)`, `ARRAY_ELEMENT16` with 1085 words (two u16
indices per word, low one first) and `SET_BEGIN_END(END)`, plus one padding u32. If the strip
has an odd number of indices, the last one goes alone in an `ARRAY_ELEMENT32` (one index per
word; seen in `ayane.kmd` and `murai.kmd`). Any other method is rejected: the vertex format
does not travel in the push buffer but in the FVF of the descriptor.

**Padding after END.** In the 2137 push buffers of the game's KMDs, there is exactly one u32
after END: 0 in 2065 and an arbitrary value in 72 (from stages: `ssato_02.kmd` res236 has
`0xc2f916e6`). That first u32 after END is ignored whatever it is; anything that comes after
it must be 0. The garbage is only tolerated if it really sits after END: before END, every
word is a command and is validated.

Source of the methods: xemu, `hw/xbox/nv2a/nv2a_regs.h` (`NV097_SET_BEGIN_END` 0x17FC with
`OP_TRIANGLE_STRIP` 6 and `OP_END` 0, `NV097_ARRAY_ELEMENT16` 0x1800, `NV097_ARRAY_ELEMENT32`
0x1808).
"""

from __future__ import annotations

import struct
from typing import Final

from ngb_tools.kmd.errors import PushBufferError

NV097_SET_BEGIN_END: Final = 0x17FC
NV097_ARRAY_ELEMENT16: Final = 0x1800
NV097_ARRAY_ELEMENT32: Final = 0x1808
OP_END: Final = 0
OP_TRIANGLE_STRIP: Final = 6
_KIND_MASK: Final = 0xE0030003
_INCREASING: Final = 0x00000000
_NON_INCREASING: Final = 0x40000000


def _header(word: int, pos: int) -> tuple[int, int]:
    """Method and argument count of a command header."""
    if word & _KIND_MASK not in (_INCREASING, _NON_INCREASING):
        raise PushBufferError(f"push buffer {pos:#x}: header {word:#010x} is not a method")
    subchannel = (word >> 13) & 0x7
    if subchannel:
        raise PushBufferError(f"push buffer {pos:#x}: subchannel {subchannel} not supported")
    return word & 0x1FFC, (word >> 18) & 0x7FF


def _begin_end(state: str, args: tuple[int, ...], pos: int) -> str:
    """Transition idle -> strip -> done; any other combination is rejected."""
    if len(args) != 1:
        raise PushBufferError(f"push buffer {pos:#x}: SET_BEGIN_END with {len(args)} args")
    op = args[0]
    if state == "idle" and op == OP_TRIANGLE_STRIP:
        return "strip"
    if state == "strip" and op == OP_END:
        return "done"
    raise PushBufferError(f"push buffer {pos:#x}: unexpected SET_BEGIN_END({op}) ({state})")


def decode_push_buffer(data: bytes) -> tuple[int, ...]:
    """Indices of the single triangle strip in the push buffer."""
    if len(data) % 4:
        raise PushBufferError(f"push buffer of {len(data)} bytes, not a multiple of 4")
    words = struct.unpack(f"<{len(data) // 4}I", data)
    indices: list[int] = []
    state = "idle"
    i = 0
    while i < len(words) and state != "done":
        pos = 4 * i
        method, count = _header(words[i], pos)
        args = tuple(words[i + 1 : i + 1 + count])
        if len(args) != count:
            raise PushBufferError(f"push buffer {pos:#x}: {count} arguments, {len(args)} left")
        if method == NV097_SET_BEGIN_END:
            state = _begin_end(state, args, pos)
        elif method == NV097_ARRAY_ELEMENT16 and state == "strip":
            for word in args:
                indices += (word & 0xFFFF, word >> 16)
        elif method == NV097_ARRAY_ELEMENT32 and state == "strip":
            indices += args
        else:
            raise PushBufferError(f"push buffer {pos:#x}: method {method:#06x} not supported")
        i += 1 + count
    if state != "done":
        raise PushBufferError("push buffer without SET_BEGIN_END(END)")
    if any(words[i + 1 :]):  # words[i] is the padding u32: it can be anything
        raise PushBufferError(f"push buffer {4 * (i + 1):#x}: data after SET_BEGIN_END(END)")
    return tuple(indices)
