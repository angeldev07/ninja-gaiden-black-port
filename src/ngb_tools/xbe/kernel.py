"""Kernel imports: xboxkrnl.exe export table and thunk resolution.

An XBE does not import the kernel by name, but by ordinal. The thunk table
(whose VA is obfuscated in the header) is a u32 array of
`0x80000000 | ordinal` terminated by 0. On load, the kernel overwrites each
slot with the real address of the function and the game code calls with
`call [slot]`. To name each ordinal, the export table
`ngb_tools/data/xboxkrnl_exports.tsv` is used, derived from the nxdk `.def` (CC0-1.0).
"""

from __future__ import annotations

from collections.abc import Mapping
from dataclasses import dataclass
from enum import StrEnum
from functools import cache
from importlib.resources import files
from pathlib import Path
from types import MappingProxyType

from ngb_tools.xbe.binary import read_u32
from ngb_tools.xbe.errors import KernelExportsError, ThunkTableError, UnmappedAddressError
from ngb_tools.xbe.image import XbeImage

BUNDLED_EXPORTS = ("data", "xboxkrnl_exports.tsv")  # path inside the ngb_tools package
ORDINAL_FLAG = 0x80000000
MAX_ORDINAL = ORDINAL_FLAG - 1
MAX_KERNEL_IMPORTS = 512  # safety bound: the kernel exports fewer than 400 symbols
_TSV_COLUMNS = 5


class ExportKind(StrEnum):
    """What the kernel exports at an ordinal."""

    FUNC = "func"
    DATA = "data"  # exported variable: the slot receives its address


class CallConv(StrEnum):
    """Calling convention, deduced from the decoration in the nxdk `.def`."""

    STDCALL = "stdcall"  # '@N' suffix: the callee cleans N bytes of arguments
    FASTCALL = "fastcall"  # '@' prefix: first two arguments in ECX and EDX
    CDECL = "cdecl"  # no decoration: the caller cleans the stack (variadics)
    NONE = "-"  # not applicable (exported variable)


@dataclass(frozen=True, slots=True)
class KernelExport:
    """One table row: `ordinal<TAB>name[<TAB>kind<TAB>callconv<TAB>arg_bytes]`."""

    ordinal: int
    name: str
    kind: ExportKind | None
    callconv: CallConv | None
    arg_bytes: int | None


type KernelExports = Mapping[int, KernelExport]


def parse_kernel_exports(text: str, source: str = "<text>") -> dict[int, KernelExport]:
    """Parse the TSV; ignores blank lines and comments (`#`)."""
    exports: dict[int, KernelExport] = {}
    for number, line in enumerate(text.splitlines(), start=1):
        if not line.strip() or line.startswith("#"):
            continue
        export = _parse_export_line(line, f"{source}, line {number}")
        if export.ordinal in exports:
            raise KernelExportsError(f"{source}, line {number}: duplicate ordinal {export.ordinal}")
        exports[export.ordinal] = export
    return exports


def _parse_export_line(line: str, where: str) -> KernelExport:
    columns = line.split("\t")
    if not 2 <= len(columns) <= _TSV_COLUMNS or not columns[1]:
        raise KernelExportsError(f"{where}: expected 2 to 5 columns including a name")
    ordinal, name, kind, callconv, arg_bytes = columns + [""] * (_TSV_COLUMNS - len(columns))
    try:
        export = KernelExport(
            ordinal=int(ordinal),
            name=name,
            kind=ExportKind(kind) if kind else None,
            callconv=CallConv(callconv) if callconv else None,
            arg_bytes=int(arg_bytes) if arg_bytes else None,
        )
    except ValueError as exc:
        raise KernelExportsError(f"{where}: {exc}") from exc
    # The ordinal shares the slot's u32 with bit 31 (ORDINAL_FLAG): 31 bits fit.
    if not 0 <= export.ordinal <= MAX_ORDINAL:
        raise KernelExportsError(f"{where}: ordinal {export.ordinal} outside 0..0x{MAX_ORDINAL:X}")
    if export.arg_bytes is not None and export.arg_bytes < 0:
        raise KernelExportsError(f"{where}: negative arg_bytes ({export.arg_bytes})")
    return export


@cache
def bundled_kernel_exports() -> KernelExports:
    """Table bundled in the package; parsed once and read-only."""
    resource = files("ngb_tools").joinpath(*BUNDLED_EXPORTS)
    text = resource.read_text(encoding="utf-8")
    return MappingProxyType(parse_kernel_exports(text, BUNDLED_EXPORTS[-1]))


def load_kernel_exports(path: Path | None = None) -> KernelExports:
    """Table from `path` (same TSV format) or, with no path, the one bundled in the package."""
    if path is None:
        return bundled_kernel_exports()
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError as exc:
        raise KernelExportsError(f"{path}: not UTF-8 ({exc})") from exc
    return MappingProxyType(parse_kernel_exports(text, str(path)))


@dataclass(frozen=True, slots=True)
class KernelImport:
    """A slot of the thunk table and the export that resolves it (if known)."""

    slot_va: int
    raw_entry: int
    ordinal: int | None  # None if the slot does not carry the import-by-ordinal bit
    export: KernelExport | None

    @property
    def name(self) -> str:
        if self.ordinal is None:
            return "<not an ordinal import>"
        if self.export is None:
            return f"<unknown ordinal {self.ordinal}>"
        return self.export.name

    @property
    def kind(self) -> ExportKind | None:
        return self.export.kind if self.export else None

    @property
    def callconv(self) -> CallConv | None:
        return self.export.callconv if self.export else None

    @property
    def arg_bytes(self) -> int | None:
        return self.export.arg_bytes if self.export else None


@dataclass(frozen=True, slots=True)
class KernelImportTable:
    """Located thunk table: where it is and what it imports."""

    va: int
    file_offset: int
    section: str | None
    imports: tuple[KernelImport, ...]


def parse_thunk_table(
    table: bytes, thunk_va: int, exports: KernelExports
) -> tuple[KernelImport, ...]:
    """Read u32 slots from `table` (the bytes backed from `thunk_va`) up to the 0.

    `table` ends where the region backing the thunk ends: a table that reaches
    that point without a terminator would continue in memory through BSS or another
    section, and that is not a real import table.
    """
    imports: list[KernelImport] = []
    for index in range(MAX_KERNEL_IMPORTS):
        offset = 4 * index
        if offset + 4 > len(table):
            raise ThunkTableError(
                f"the thunk table reaches the end of its region without a terminator "
                f"({index} slots)"
            )
        entry = read_u32(table, offset)
        if entry == 0:
            return tuple(imports)
        ordinal = entry & MAX_ORDINAL if entry & ORDINAL_FLAG else None
        export = exports.get(ordinal) if ordinal is not None else None
        imports.append(KernelImport(thunk_va + offset, entry, ordinal, export))
    raise ThunkTableError(f"thunk table without a terminator after {MAX_KERNEL_IMPORTS} entries")


def read_kernel_imports(image: XbeImage, exports: KernelExports) -> KernelImportTable | None:
    """Thunk table of `image`, or None if no XOR key yielded its address."""
    thunk_va = image.xor.kernel_thunk_addr
    if thunk_va is None:
        return None
    span = image.address_map.backed_span(thunk_va)
    if span is None:
        raise UnmappedAddressError(thunk_va, "kernel thunk")
    offset, end = span
    return KernelImportTable(
        va=thunk_va,
        file_offset=offset,
        section=image.address_map.section_name(thunk_va),
        imports=parse_thunk_table(image.data[offset:end], thunk_va, exports),
    )
