"""Constructores de audio sintético: ADX, bancos `.wva`, wave banks XWB y AFS.

Las cabeceras se escriben campo a campo desde la especificación (no con el código bajo
prueba), así que un test que las decodifica contrasta dos lecturas independientes.
"""

from __future__ import annotations

import struct
from collections.abc import Sequence

SECTOR = 0x800


def adx_header(
    *,
    channels: int = 1,
    rate: int = 44100,
    total: int = 32,
    cutoff: int = 500,
    version: int = 3,
    flags: int = 0,
    encoding: int = 3,
    loop: tuple[int, int] | None = None,
) -> bytes:
    """Cabecera ADX de 0x24 bytes (sin loop) o 0x3C (con un loop de la versión 3)."""
    data_offset = 0x3C if loop is not None else 0x24
    header = bytearray(data_offset)
    struct.pack_into(">HH", header, 0, 0x8000, data_offset - 4)
    struct.pack_into(">BBBBIIHBB", header, 4, encoding, 18, 4, channels, rate, total, cutoff,
                     version, flags)  # fmt: skip
    if loop is not None:
        start, end = loop
        # u16 relleno, u16 1, u32 flag, u32 muestra/offset inicial, u32 muestra/offset final
        struct.pack_into(">HHIIIII", header, 0x14, 0, 1, 1, start, 0x100, end, 0x200)
    header[data_offset - 6 : data_offset] = b"(c)CRI"
    return bytes(header)


def adx_frame(scale: int, nibbles: Sequence[int]) -> bytes:
    """Bloque ADX de 18 bytes: escala u16be y 32 nibbles con signo (el alto primero)."""
    padded = [n & 0x0F for n in nibbles] + [0] * (32 - len(nibbles))
    packed = bytes((padded[i] << 4) | padded[i + 1] for i in range(0, 32, 2))
    return struct.pack(">H", scale) + packed


def xbox_block(headers: Sequence[tuple[int, int]], groups: Sequence[Sequence[bytes]]) -> bytes:
    """Bloque Xbox ADPCM: cabeceras (muestra, índice) por canal y 8 grupos de 4 bytes.

    `groups[c][g]` son los 4 bytes del grupo `g` del canal `c`; se intercalan c0, c1, ...
    """
    out = bytearray()
    for sample, index in headers:
        out += struct.pack("<hBB", sample, index, 0)
    for g in range(8):
        for channel in groups:
            out += channel[g]
    return bytes(out)


def wva_record(
    data: bytes, *, channels: int = 1, rate: int = 22050, long_params: bool = True, tag: int = 0x69
) -> bytes:
    """Registro `.wva`: u32 tamaño, WAVEFORMATEX de 20 bytes, parámetros y datos."""
    align = 36 * channels
    wfx = struct.pack("<HHIIHHHH", tag, channels, rate, rate * align // 64, align, 4, 2, 64)
    params = (b"\x00\x00\x80\xbf" * 6) if long_params else bytes(8)
    return struct.pack("<I", len(data)) + wfx + params + data


def build_wva(slots: Sequence[tuple[bytes, bool] | None]) -> bytes:
    """Banco `.wva`: `slots` = [(registro, parámetros largos) o None para un hueco]."""
    table = 4 + 4 * len(slots)
    body = bytearray()
    pointers = []
    for slot in slots:
        if slot is None:
            pointers.append(0)
            continue
        record, long_params = slot
        pointers.append((table + len(body)) | (0x8000_0000 if long_params else 0))
        body += record
    return struct.pack(f"<I{len(slots)}I", len(slots), *pointers) + bytes(body)


def build_xwb(entries: Sequence[tuple[int, int, int, int, bytes]], name: bytes = b"Bank") -> bytes:
    """Wave bank XWB v3: `entries` = [(códec, canales, frecuencia, bits, datos)]."""
    bank_off, bank_size = 0x28, 0x28
    meta_off, meta_size = bank_off + bank_size, 0x18 * len(entries)
    wave_off = meta_off + meta_size
    wave = bytearray()
    meta = bytearray()
    for codec, channels, rate, bits, data in entries:
        fmt = codec | (channels << 2) | (rate << 5) | ((1 if bits == 16 else 0) << 31)
        meta += struct.pack("<6I", 0, fmt, len(wave), len(data), 0, 0)
        wave += data
    header = struct.pack("<4sI8I", b"WBND", 3, bank_off, bank_size, meta_off, meta_size, 0, 0,
                         wave_off, len(wave))  # fmt: skip
    bank = struct.pack("<II16sIII", 0, len(entries), name, 0x18, 0x40, 4).ljust(bank_size, b"\0")
    return header + bank + bytes(meta) + bytes(wave)


def _align(n: int) -> int:
    return (n + SECTOR - 1) // SECTOR * SECTOR


def build_afs(files: Sequence[tuple[str, bytes]]) -> bytes:
    """AFS con datos alineados a 0x800 y tabla de atributos (nombres) tras la TOC."""
    count = len(files)
    pos = _align(8 + 8 * count + 8)
    toc = []
    for _, data in files:
        toc.append((pos, len(data)))
        pos = _align(pos + len(data))
    attr_offset = pos
    buf = bytearray(_align(attr_offset + 0x30 * count))
    struct.pack_into("<4sI", buf, 0, b"AFS\x00", count)
    for i, ((offset, size), (name, data)) in enumerate(zip(toc, files, strict=True)):
        struct.pack_into("<II", buf, 8 + 8 * i, offset, size)
        buf[offset : offset + size] = data
        struct.pack_into("<32s6HI", buf, attr_offset + 0x30 * i, name.encode(), 2004, 1, 1, 0,
                         0, 0, size)  # fmt: skip
    struct.pack_into("<II", buf, 8 + 8 * count, attr_offset, 0x30 * count)
    return bytes(buf)
