"""CLI de la herramienta AFS: `python -I -m ngb_tools.afs {list,extract,inventory}`.

Códigos de salida:

- 0: todo correcto.
- 1: hallazgo sobre los datos: incidencias de invariantes o un AFS ilegible.
- 2: error de uso o de E/S: argumentos, índices, salida dentro de un repo, archivos.

Salidas: `extract` nunca escribe dentro de un repo git. Los informes de `inventory`
(`--json`, `--md`) llevan nombres y cabeceras de los archivos del juego, así que pasan por
la misma guarda con una única excepción explícita: `build/` del repo de las herramientas,
que .gitignore ignora y la guardia de higiene bloquea, y donde van los informes por
convención (`build/reports/afs/`).

Sobre datos del juego se ejecuta en modo aislado (`-I`, o `-P -s` con PYTHONPATH=src),
para que ningún `.py` del directorio de datos entre en `sys.path`. `__main__` además hace
que la consola sustituya los caracteres que no sabe representar; `main()` no toca
`sys.stdout`.
"""

from __future__ import annotations

import argparse
import io
import sys
from collections.abc import Callable, Sequence
from enum import IntEnum
from pathlib import Path
from types import MappingProxyType
from typing import Final

from ngb_tools._paths import (
    DATA_ENV_VAR,
    UnsafeOutputError,
    atomic_write_bytes,
    ensure_outside_repos,
    report_dirs,
)
from ngb_tools.afs.archive import open_afs
from ngb_tools.afs.errors import AfsFormatError, AfsUsageError
from ngb_tools.afs.extract import default_extract_dir, extract_archive
from ngb_tools.afs.invariants import validate_archive
from ngb_tools.afs.inventory import build_inventory
from ngb_tools.afs.records import InventoryRecord
from ngb_tools.afs.report import format_listing, render_json, render_markdown

PROG: Final = "python -m ngb_tools.afs"


class ExitCode(IntEnum):
    OK = 0
    FINDINGS = 1
    ERROR = 2


def cmd_list(args: argparse.Namespace) -> ExitCode:
    afs_path: Path = args.afs
    with afs_path.open("rb") as source:
        archive = open_afs(source, afs_path.name)
    issues = validate_archive(archive)
    print("\n".join(format_listing(archive)))
    for issue in issues:
        print(f"! {issue}")
    return ExitCode.FINDINGS if issues else ExitCode.OK


def cmd_extract(args: argparse.Namespace) -> ExitCode:
    afs_path: Path = args.afs
    out_dir: Path = args.out_dir if args.out_dir is not None else default_extract_dir(afs_path)
    indices: list[int] | None = args.index
    manifest = extract_archive(afs_path, out_dir, indices)
    for m in manifest["entries"]:
        print(f"{m['index']:4d}  {m['size']:>10}  {m['kind']:<16}  {m['file']}")
    print(f"{len(manifest['entries'])} entradas -> {out_dir.resolve()}")
    return ExitCode.OK


def _write_report(target: Path, text: str) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    atomic_write_bytes(target, text.encode("utf-8"), fsync=True)  # pequeño: el fsync es barato


def cmd_inventory(args: argparse.Namespace) -> ExitCode:
    root: Path = args.root
    requested: tuple[tuple[Path | None, Callable[[InventoryRecord], str]], ...] = (
        (args.json, render_json),
        (args.md, render_markdown),
    )
    # Los destinos se validan antes de inventariar: uno prohibido no cuesta un recorrido.
    allowed = report_dirs()
    renderers = [
        (ensure_outside_repos(target, allow=allowed), render)
        for target, render in requested
        if target is not None
    ]
    inventory = build_inventory(root)
    for target, render in renderers:
        _write_report(target, render(inventory))
    totals = inventory["summary"]["totals"]
    print(
        f"{totals['archives']} AFS, {totals['entries']} entradas, "
        f"{totals['pct_named']} % con nombre, {totals['entry_bytes']} bytes en entradas"
    )
    with_issues = totals["archives"] - inventory["summary"]["invariants"]["archives_ok"]
    if with_issues:
        print(f"{with_issues} AFS con incidencias (ver el informe)", file=sys.stderr)
        return ExitCode.FINDINGS
    return ExitCode.OK


_COMMANDS: Final = MappingProxyType(
    {"list": cmd_list, "extract": cmd_extract, "inventory": cmd_inventory}
)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog=PROG, description="Contenedores AFS (CRI Middleware).")
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("list", help="lista las entradas y sus incidencias")
    p.add_argument("afs", type=Path)

    p = sub.add_parser("extract", help="extrae entradas (siempre fuera del repo)")
    p.add_argument("afs", type=Path)
    p.add_argument(
        "out_dir",
        nargs="?",
        type=Path,
        help=f"por defecto ${DATA_ENV_VAR}/extracted/<ruta del afs dentro de game/>",
    )
    p.add_argument("--index", type=int, action="append", metavar="N", help="repetible")

    p = sub.add_parser("inventory", help="inventario por streaming de todos los .afs")
    p.add_argument("root", type=Path)
    p.add_argument("--json", type=Path, metavar="RUTA", help="informe JSON")
    p.add_argument("--md", type=Path, metavar="RUTA", help="informe Markdown")
    return parser


def tolerate_unencodable_output() -> None:
    """Una consola que no sabe representar un carácter lo sustituye en vez de abortar.

    Muta el `sys.stdout` del proceso, así que solo la llama el punto de entrada (`__main__`).
    """
    if isinstance(sys.stdout, io.TextIOWrapper):
        sys.stdout.reconfigure(errors="replace")


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    command = _COMMANDS[args.command]
    try:
        return command(args)
    except AfsFormatError as err:
        print(f"error: {err}", file=sys.stderr)
        return ExitCode.FINDINGS
    except (AfsUsageError, UnsafeOutputError, OSError) as err:
        print(f"error: {err}", file=sys.stderr)
        return ExitCode.ERROR


def run() -> int:
    """Entrada de consola (`ngb-afs` y `python -m ngb_tools.afs`).

    Tolera la salida no codificable de la consola y ejecuta `main`. `main` queda libre
    de efectos sobre `sys.stdout` para los tests y para quien lo importe.
    """
    tolerate_unencodable_output()
    return main()
