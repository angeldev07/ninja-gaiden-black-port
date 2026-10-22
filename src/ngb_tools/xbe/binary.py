"""Primitive reads of the XBE format: little-endian integers and text.

An XBE is an x86 image, so all integers are little-endian.
Fixed-size reads check bounds and raise `TruncatedDataError`; C-string reads
trim silently, because the trailing NUL is what delimits the data.
"""

from __future__ import annotations

from ngb_tools.xbe.errors import TruncatedDataError

DEFAULT_C_STRING_MAX = 1024


def read_bytes(data: bytes, offset: int, size: int) -> bytes:
    """Exactly `size` bytes from `offset`, or `TruncatedDataError`."""
    if offset < 0 or offset + size > len(data):
        raise TruncatedDataError(offset, size)
    return data[offset : offset + size]


def _read_uints(data: bytes, offset: int, count: int, width: int) -> tuple[int, ...]:
    raw = read_bytes(data, offset, count * width)
    return tuple(int.from_bytes(raw[i : i + width], "little") for i in range(0, len(raw), width))


def read_u16s(data: bytes, offset: int, count: int) -> tuple[int, ...]:
    """`count` consecutive u16 values."""
    return _read_uints(data, offset, count, 2)


def read_u32s(data: bytes, offset: int, count: int) -> tuple[int, ...]:
    """`count` consecutive u32 values."""
    return _read_uints(data, offset, count, 4)


def read_u32(data: bytes, offset: int) -> int:
    """A single u32."""
    return _read_uints(data, offset, 1, 4)[0]


def read_c_bytes(data: bytes, offset: int, max_len: int = DEFAULT_C_STRING_MAX) -> bytes:
    """Bytes of a C string: up to the NUL (excluded), the end of `data` or `max_len`."""
    return data[offset : offset + max_len].split(b"\0", 1)[0]


def decode_ascii(raw: bytes) -> str:
    """ASCII with non-ASCII bytes escaped (`\\x99`): never fails or invents text."""
    return raw.decode("ascii", errors="backslashreplace")


def decode_utf16z(raw: bytes) -> str:
    """UTF-16LE up to the 0x0000 terminator; a trailing odd byte is ignored."""
    text = raw[: len(raw) & ~1].decode("utf-16-le", errors="replace")
    return text.split("\0", 1)[0]


def decode_build_text(raw: bytes) -> tuple[str, str]:
    """Build paths: ASCII or, failing that, Shift-JIS (cp932, as on Japanese PCs).

    Returns `(text, encoding)`. If neither fits, ASCII with escapes.
    """
    for encoding in ("ascii", "cp932"):
        try:
            return raw.decode(encoding), encoding
        except UnicodeDecodeError:
            continue
    return decode_ascii(raw), "ascii+escapes"


def hex32(value: int) -> str:
    """Format of a 32-bit address or mask: `0x0001ABCD`."""
    return f"0x{value:08X}"
