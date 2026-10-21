"""Decodificador estricto de los push buffers de un KMD: una tira de triángulos y nada más.

Un push buffer son comandos ya grabados para la GPU NV2A. Cada comando es una cabecera u32
seguida de sus argumentos:

    bits 2-12   método (dirección del registro de la clase Kelvin, NV097)
    bits 13-15  subcanal (aquí siempre 0)
    bits 18-28  número de argumentos
    bit 30      sin incremento: todos los argumentos van al mismo método

Las cabeceras de salto o llamada (bits 0-1 o 29) no se admiten. En `ryu.kmd` el único push
buffer es exactamente `SET_BEGIN_END(TRIANGLE_STRIP)`, `ARRAY_ELEMENT16` con 1085 palabras
(dos índices u16 por palabra, el bajo primero) y `SET_BEGIN_END(END)`, más un u32 de
relleno. Si la tira tiene un número impar de índices, el último va solo en un
`ARRAY_ELEMENT32` (un índice por palabra; visto en `ayane.kmd` y `murai.kmd`). Cualquier
otro método se rechaza: el formato de vértice no viaja en el push buffer sino en el FVF del
descriptor.

**Relleno tras END.** En los 2137 push buffers de los KMD del juego, detrás de END hay
exactamente un u32: 0 en 2065 y un valor cualquiera en 72 (de escenarios: `ssato_02.kmd`
res236 trae `0xc2f916e6`). Ese primer u32 tras END se ignora sea cual sea; lo que venga
después tiene que ser 0. La basura solo se tolera si de verdad va detrás de END: antes de
END, cualquier palabra es un comando y se valida.

Fuente de los métodos: xemu, `hw/xbox/nv2a/nv2a_regs.h` (`NV097_SET_BEGIN_END` 0x17FC con
`OP_TRIANGLE_STRIP` 6 y `OP_END` 0, `NV097_ARRAY_ELEMENT16` 0x1800, `NV097_ARRAY_ELEMENT32`
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
    """Método y número de argumentos de una cabecera de comando."""
    if word & _KIND_MASK not in (_INCREASING, _NON_INCREASING):
        raise PushBufferError(f"push buffer {pos:#x}: cabecera {word:#010x} no es un método")
    subchannel = (word >> 13) & 0x7
    if subchannel:
        raise PushBufferError(f"push buffer {pos:#x}: subcanal {subchannel} no admitido")
    return word & 0x1FFC, (word >> 18) & 0x7FF


def _begin_end(state: str, args: tuple[int, ...], pos: int) -> str:
    """Transición idle -> strip -> done; cualquier otra combinación se rechaza."""
    if len(args) != 1:
        raise PushBufferError(f"push buffer {pos:#x}: SET_BEGIN_END con {len(args)} args")
    op = args[0]
    if state == "idle" and op == OP_TRIANGLE_STRIP:
        return "strip"
    if state == "strip" and op == OP_END:
        return "done"
    raise PushBufferError(f"push buffer {pos:#x}: SET_BEGIN_END({op}) inesperado ({state})")


def decode_push_buffer(data: bytes) -> tuple[int, ...]:
    """Índices de la única tira de triángulos del push buffer."""
    if len(data) % 4:
        raise PushBufferError(f"push buffer de {len(data)} bytes, no es múltiplo de 4")
    words = struct.unpack(f"<{len(data) // 4}I", data)
    indices: list[int] = []
    state = "idle"
    i = 0
    while i < len(words) and state != "done":
        pos = 4 * i
        method, count = _header(words[i], pos)
        args = tuple(words[i + 1 : i + 1 + count])
        if len(args) != count:
            raise PushBufferError(f"push buffer {pos:#x}: {count} argumentos, quedan {len(args)}")
        if method == NV097_SET_BEGIN_END:
            state = _begin_end(state, args, pos)
        elif method == NV097_ARRAY_ELEMENT16 and state == "strip":
            for word in args:
                indices += (word & 0xFFFF, word >> 16)
        elif method == NV097_ARRAY_ELEMENT32 and state == "strip":
            indices += args
        else:
            raise PushBufferError(f"push buffer {pos:#x}: método {method:#06x} no admitido")
        i += 1 + count
    if state != "done":
        raise PushBufferError("push buffer sin SET_BEGIN_END(END)")
    if any(words[i + 1 :]):  # words[i] es el u32 de relleno: puede valer cualquier cosa
        raise PushBufferError(f"push buffer {4 * (i + 1):#x}: datos tras SET_BEGIN_END(END)")
    return tuple(indices)
