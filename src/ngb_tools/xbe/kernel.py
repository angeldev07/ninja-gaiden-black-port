"""Imports del kernel: tabla de exports de xboxkrnl.exe y resolución del thunk.

Un XBE no importa el kernel por nombre, sino por ordinal. La tabla de thunks
(cuya VA está ofuscada en la cabecera) es un array de u32
`0x80000000 | ordinal` terminado en 0. Al cargar, el kernel sobrescribe cada
slot con la dirección real de la función y el código del juego llama con
`call [slot]`. Para poner nombre a cada ordinal se usa la tabla de exports
`ngb_tools/data/xboxkrnl_exports.tsv`, derivada del `.def` de nxdk (CC0-1.0).
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

BUNDLED_EXPORTS = ("data", "xboxkrnl_exports.tsv")  # ruta dentro del paquete ngb_tools
ORDINAL_FLAG = 0x80000000
MAX_ORDINAL = ORDINAL_FLAG - 1
MAX_KERNEL_IMPORTS = 512  # cota de seguridad: el kernel exporta menos de 400 símbolos
_TSV_COLUMNS = 5


class ExportKind(StrEnum):
    """Qué exporta el kernel en un ordinal."""

    FUNC = "func"
    DATA = "data"  # variable exportada: el slot recibe su dirección


class CallConv(StrEnum):
    """Convención de llamada, deducida de la decoración del `.def` de nxdk."""

    STDCALL = "stdcall"  # sufijo '@N': el llamado limpia N bytes de argumentos
    FASTCALL = "fastcall"  # prefijo '@': dos primeros argumentos en ECX y EDX
    CDECL = "cdecl"  # sin decoración: el llamador limpia la pila (variádicas)
    NONE = "-"  # no aplica (variable exportada)


@dataclass(frozen=True, slots=True)
class KernelExport:
    """Una fila de la tabla: `ordinal<TAB>name[<TAB>kind<TAB>callconv<TAB>arg_bytes]`."""

    ordinal: int
    name: str
    kind: ExportKind | None
    callconv: CallConv | None
    arg_bytes: int | None


type KernelExports = Mapping[int, KernelExport]


def parse_kernel_exports(text: str, source: str = "<texto>") -> dict[int, KernelExport]:
    """Parsea el TSV; ignora líneas vacías y comentarios (`#`)."""
    exports: dict[int, KernelExport] = {}
    for number, line in enumerate(text.splitlines(), start=1):
        if not line.strip() or line.startswith("#"):
            continue
        export = _parse_export_line(line, f"{source}, línea {number}")
        if export.ordinal in exports:
            raise KernelExportsError(
                f"{source}, línea {number}: ordinal {export.ordinal} duplicado"
            )
        exports[export.ordinal] = export
    return exports


def _parse_export_line(line: str, where: str) -> KernelExport:
    columns = line.split("\t")
    if not 2 <= len(columns) <= _TSV_COLUMNS or not columns[1]:
        raise KernelExportsError(f"{where}: se esperaban 2 a 5 columnas con nombre")
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
    # El ordinal comparte el u32 del slot con el bit 31 (ORDINAL_FLAG): caben 31 bits.
    if not 0 <= export.ordinal <= MAX_ORDINAL:
        raise KernelExportsError(f"{where}: ordinal {export.ordinal} fuera de 0..0x{MAX_ORDINAL:X}")
    if export.arg_bytes is not None and export.arg_bytes < 0:
        raise KernelExportsError(f"{where}: arg_bytes negativo ({export.arg_bytes})")
    return export


@cache
def bundled_kernel_exports() -> KernelExports:
    """Tabla incluida en el paquete; se parsea una vez y es de solo lectura."""
    resource = files("ngb_tools").joinpath(*BUNDLED_EXPORTS)
    text = resource.read_text(encoding="utf-8")
    return MappingProxyType(parse_kernel_exports(text, BUNDLED_EXPORTS[-1]))


def load_kernel_exports(path: Path | None = None) -> KernelExports:
    """Tabla de `path` (mismo formato TSV) o, sin ruta, la incluida en el paquete."""
    if path is None:
        return bundled_kernel_exports()
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError as exc:
        raise KernelExportsError(f"{path}: no es UTF-8 ({exc})") from exc
    return MappingProxyType(parse_kernel_exports(text, str(path)))


@dataclass(frozen=True, slots=True)
class KernelImport:
    """Un slot de la tabla de thunks y el export que lo resuelve (si se conoce)."""

    slot_va: int
    raw_entry: int
    ordinal: int | None  # None si el slot no lleva el bit de import por ordinal
    export: KernelExport | None

    @property
    def name(self) -> str:
        if self.ordinal is None:
            return "<no es import por ordinal>"
        if self.export is None:
            return f"<ordinal {self.ordinal} desconocido>"
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
    """Tabla de thunks localizada: dónde está y qué importa."""

    va: int
    file_offset: int
    section: str | None
    imports: tuple[KernelImport, ...]


def parse_thunk_table(
    table: bytes, thunk_va: int, exports: KernelExports
) -> tuple[KernelImport, ...]:
    """Lee slots u32 de `table` (los bytes respaldados desde `thunk_va`) hasta el 0.

    `table` acaba donde acaba la región que respalda el thunk: una tabla que llega
    ahí sin terminador seguiría en memoria por BSS o por otra sección, y eso no es
    una tabla de imports real.
    """
    imports: list[KernelImport] = []
    for index in range(MAX_KERNEL_IMPORTS):
        offset = 4 * index
        if offset + 4 > len(table):
            raise ThunkTableError(
                f"la tabla de thunks llega al final de su región sin terminador ({index} slots)"
            )
        entry = read_u32(table, offset)
        if entry == 0:
            return tuple(imports)
        ordinal = entry & MAX_ORDINAL if entry & ORDINAL_FLAG else None
        export = exports.get(ordinal) if ordinal is not None else None
        imports.append(KernelImport(thunk_va + offset, entry, ordinal, export))
    raise ThunkTableError(f"tabla de thunks sin terminador tras {MAX_KERNEL_IMPORTS} entradas")


def read_kernel_imports(image: XbeImage, exports: KernelExports) -> KernelImportTable | None:
    """Tabla de thunks de `image`, o None si ninguna clave XOR dio su dirección."""
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
