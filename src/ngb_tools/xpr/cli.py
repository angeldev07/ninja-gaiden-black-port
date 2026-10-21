"""CLI de la herramienta XPR: `python -m ngb_tools.xpr {list,export}`.

    list   <archivo> [--kmd]              texturas, formato, tamaño, mips y offsets
    export <archivo> [--kmd] [--out DIR]  un PNG por textura y `_manifest.json`

Con `--kmd` el archivo es un chunk de Team Ninja y el XPR0 se busca entre sus slots. Sin
`--out`, la salida va a `$NGB_DATA/extracted/textures/<archivo sin extensión>`; nunca
dentro de un repo git.

Códigos de salida:

- 0: todo correcto.
- 1: hallazgo de validación: un XPR0 roto o texturas que no se pueden exportar.
- 2: error de uso o de entrada: argumentos, un archivo que no es XPR0 (o, con `--kmd`, un
  chunk sin slot XPR0), E/S y salida dentro de un repo.

Sobre datos del juego se ejecuta en modo aislado (`-I`, o `-P -s` con PYTHONPATH=src),
para que ningún `.py` del directorio de datos entre en `sys.path`. `run()` (la entrada de
consola) además hace que la consola sustituya los caracteres que no sabe representar;
`main()` no toca `sys.stdout` ni `sys.stderr`.
"""

from __future__ import annotations

import argparse
import io
import sys
from collections.abc import Sequence
from enum import IntEnum
from pathlib import Path
from types import MappingProxyType
from typing import Final

from ngb_tools._paths import DATA_ENV_VAR, UnsafeOutputError
from ngb_tools.xpr.catalog import CatalogEntry, catalog
from ngb_tools.xpr.errors import UnsupportedTextureError, XprFormatError, XprInputError
from ngb_tools.xpr.export import default_output_dir, export_textures
from ngb_tools.xpr.formats import lookup_format
from ngb_tools.xpr.source import XprSource, open_source

PROG: Final = "python -m ngb_tools.xpr"
_COLUMNS: Final = (
    f"{'#':>4}  {'res':>4}  {'formato':<13} {'almacén':<10} {'tamaño':>9} {'mips':>4} "
    f"{'data':>9} {'en archivo':>10}"
)


class ExitCode(IntEnum):
    OK = 0
    FINDINGS = 1
    ERROR = 2


def _describe(source: XprSource) -> str:
    where = f"{source.offset:#x}"
    if source.slot is not None:
        where += f" (slot {source.slot} del chunk)"
    xpr = source.xpr
    return (
        f"{source.path.name}: XPR0 en {where}, total {xpr.total_size:#x}, cabecera "
        f"{xpr.header_size:#x}, {len(xpr.resources)} recursos, {len(xpr.textures)} texturas"
    )


def _row(source: XprSource, entry: CatalogEntry) -> str:
    texture = entry.texture
    fmt = lookup_format(texture.format.code)
    storage = fmt.storage.value if fmt is not None else "?"
    size = f"{entry.width}x{entry.height}"
    file_offset = source.offset + source.xpr.header_size + texture.data
    return (
        f"{entry.ordinal:>4}  {texture.index:>4}  {entry.format_name:<13} {storage:<10} "
        f"{size:>9} {texture.format.levels:>4} {texture.data:>#9x} {file_offset:>#10x}"
    )


def _print_issues(entries: Sequence[CatalogEntry]) -> int:
    issues = [entry for entry in entries if entry.issue is not None]
    for entry in issues:
        print(f"! textura {entry.ordinal}: {entry.issue}")
    return len(issues)


def cmd_list(args: argparse.Namespace) -> ExitCode:
    source = open_source(args.file, kmd=args.kmd)
    entries = catalog(source.xpr)
    print(_describe(source))
    print(_COLUMNS)
    for entry in entries:
        print(_row(source, entry))
    return ExitCode.FINDINGS if _print_issues(entries) else ExitCode.OK


def cmd_export(args: argparse.Namespace) -> ExitCode:
    source = open_source(args.file, kmd=args.kmd)
    out_dir: Path = args.out if args.out is not None else default_output_dir(args.file)
    manifest = export_textures(source, out_dir)
    print(_describe(source))
    for record in manifest["textures"]:
        if record["file"] is not None:
            size = f"{record['width']}x{record['height']}"
            print(f"{record['index']:>4}  {record['format']:<13} {size:>9}  {record['file']}")
        else:
            print(f"! textura {record['index']}: {record['issue']}")
    print(
        f"{manifest['exported']} exportadas, {manifest['skipped']} saltadas -> {out_dir.resolve()}"
    )
    return ExitCode.FINDINGS if manifest["skipped"] else ExitCode.OK


_COMMANDS: Final = MappingProxyType({"list": cmd_list, "export": cmd_export})


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog=PROG, description="Texturas de un Xbox Packed Resource (XPR0)."
    )
    sub = parser.add_subparsers(dest="command", required=True)
    kmd_help = "el archivo es un chunk de Team Ninja (KMD): buscar el XPR0 entre sus slots"

    p = sub.add_parser("list", help="lista las texturas y sus incidencias")
    p.add_argument("file", type=Path)
    p.add_argument("--kmd", action="store_true", help=kmd_help)

    p = sub.add_parser("export", help="exporta cada textura a PNG (siempre fuera del repo)")
    p.add_argument("file", type=Path)
    p.add_argument("--kmd", action="store_true", help=kmd_help)
    p.add_argument(
        "--out",
        type=Path,
        metavar="DIR",
        help=f"por defecto ${DATA_ENV_VAR}/extracted/textures/<archivo sin extensión>",
    )
    return parser


def tolerate_unencodable_output() -> None:
    """Una consola que no sabe representar un carácter lo sustituye en vez de abortar.

    Muta `sys.stdout` y `sys.stderr` del proceso, así que solo la llama `run()`.
    """
    for stream in (sys.stdout, sys.stderr):
        if isinstance(stream, io.TextIOWrapper):
            stream.reconfigure(errors="replace")


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    command = _COMMANDS[args.command]
    try:
        return command(args)
    except (XprFormatError, UnsupportedTextureError) as err:
        print(f"error: {err}", file=sys.stderr)
        return ExitCode.FINDINGS
    except (XprInputError, UnsafeOutputError, OSError) as err:
        print(f"error: {err}", file=sys.stderr)
        return ExitCode.ERROR


def run() -> int:
    """Entrada de consola (`ngb-xpr` y `python -m ngb_tools.xpr`).

    Tolera la salida no codificable de la consola y ejecuta `main`. `main` queda libre
    de efectos sobre la consola para los tests y para quien lo importe.
    """
    tolerate_unencodable_output()
    return main()
