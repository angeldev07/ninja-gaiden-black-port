"""Lectura primitiva del formato XBE: enteros little-endian y texto.

El XBE es una imagen para x86, así que todos los enteros son little-endian.
Las lecturas de tamaño fijo comprueban los límites y lanzan
`TruncatedDataError`; las de cadenas C recortan en silencio, porque el NUL
final es lo que delimita el dato.
"""

from __future__ import annotations

from ngb_tools.xbe.errors import TruncatedDataError

DEFAULT_C_STRING_MAX = 1024


def read_bytes(data: bytes, offset: int, size: int) -> bytes:
    """Exactamente `size` bytes desde `offset`, o `TruncatedDataError`."""
    if offset < 0 or offset + size > len(data):
        raise TruncatedDataError(offset, size)
    return data[offset : offset + size]


def _read_uints(data: bytes, offset: int, count: int, width: int) -> tuple[int, ...]:
    raw = read_bytes(data, offset, count * width)
    return tuple(int.from_bytes(raw[i : i + width], "little") for i in range(0, len(raw), width))


def read_u16s(data: bytes, offset: int, count: int) -> tuple[int, ...]:
    """`count` u16 consecutivos."""
    return _read_uints(data, offset, count, 2)


def read_u32s(data: bytes, offset: int, count: int) -> tuple[int, ...]:
    """`count` u32 consecutivos."""
    return _read_uints(data, offset, count, 4)


def read_u32(data: bytes, offset: int) -> int:
    """Un u32."""
    return _read_uints(data, offset, 1, 4)[0]


def read_c_bytes(data: bytes, offset: int, max_len: int = DEFAULT_C_STRING_MAX) -> bytes:
    """Bytes de una cadena C: hasta el NUL (excluido), el final de `data` o `max_len`."""
    return data[offset : offset + max_len].split(b"\0", 1)[0]


def decode_ascii(raw: bytes) -> str:
    """ASCII con los bytes no ASCII escapados (`\\x99`): nunca falla ni inventa texto."""
    return raw.decode("ascii", errors="backslashreplace")


def decode_utf16z(raw: bytes) -> str:
    """UTF-16LE hasta el terminador 0x0000; un byte impar final se ignora."""
    text = raw[: len(raw) & ~1].decode("utf-16-le", errors="replace")
    return text.split("\0", 1)[0]


def decode_build_text(raw: bytes) -> tuple[str, str]:
    """Rutas del build: ASCII o, si falla, Shift-JIS (cp932, el de los PCs japoneses).

    Devuelve `(texto, codificación)`. Si ninguna sirve, ASCII con escapes.
    """
    for encoding in ("ascii", "cp932"):
        try:
            return raw.decode(encoding), encoding
        except UnicodeDecodeError:
            continue
    return decode_ascii(raw), "ascii+escapes"


def hex32(value: int) -> str:
    """Formato de una dirección o máscara de 32 bits: `0x0001ABCD`."""
    return f"0x{value:08X}"
