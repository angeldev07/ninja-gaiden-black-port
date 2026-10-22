"""CLI of the XPR tool: `python -m ngb_tools.xpr {list,export}`.

    list   <file> [--kmd]              textures, format, size, mips and offsets
    export <file> [--kmd] [--out DIR]  one PNG per texture and `_manifest.json`

With `--kmd` the file is a Team Ninja chunk and the XPR0 is searched among its slots. Without
`--out`, the output goes to `$NGB_DATA/extracted/textures/<file without extension>`; never
inside a git repo.

Exit codes:

- 0: all correct.
- 1: validation finding: a broken XPR0 or textures that cannot be exported.
- 2: usage or input error: arguments, a file that is not an XPR0 (or, with `--kmd`, a
  chunk without an XPR0 slot), I/O and output inside a repo.

On game data it runs in isolated mode (`-I`, or `-P -s` with PYTHONPATH=src),
so that no `.py` in the data directory enters `sys.path`. `run()` (the console
entry point) also makes the console substitute characters it cannot represent;
`main()` does not touch `sys.stdout` or `sys.stderr`.
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
    f"{'#':>4}  {'res':>4}  {'format':<13} {'storage':<10} {'size':>9} {'mips':>4} "
    f"{'data':>9} {'in file':>10}"
)


class ExitCode(IntEnum):
    OK = 0
    FINDINGS = 1
    ERROR = 2


def _describe(source: XprSource) -> str:
    where = f"{source.offset:#x}"
    if source.slot is not None:
        where += f" (chunk slot {source.slot})"
    xpr = source.xpr
    return (
        f"{source.path.name}: XPR0 at {where}, total {xpr.total_size:#x}, header "
        f"{xpr.header_size:#x}, {len(xpr.resources)} resources, {len(xpr.textures)} textures"
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
        print(f"! texture {entry.ordinal}: {entry.issue}")
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
            print(f"! texture {record['index']}: {record['issue']}")
    print(f"{manifest['exported']} exported, {manifest['skipped']} skipped -> {out_dir.resolve()}")
    return ExitCode.FINDINGS if manifest["skipped"] else ExitCode.OK


_COMMANDS: Final = MappingProxyType({"list": cmd_list, "export": cmd_export})


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog=PROG, description="Textures of an Xbox Packed Resource (XPR0)."
    )
    sub = parser.add_subparsers(dest="command", required=True)
    kmd_help = "the file is a Team Ninja chunk (KMD): look for the XPR0 among its slots"

    p = sub.add_parser("list", help="list the textures and their issues")
    p.add_argument("file", type=Path)
    p.add_argument("--kmd", action="store_true", help=kmd_help)

    p = sub.add_parser("export", help="export each texture to PNG (always outside the repo)")
    p.add_argument("file", type=Path)
    p.add_argument("--kmd", action="store_true", help=kmd_help)
    p.add_argument(
        "--out",
        type=Path,
        metavar="DIR",
        help=f"default: ${DATA_ENV_VAR}/extracted/textures/<file without extension>",
    )
    return parser


def tolerate_unencodable_output() -> None:
    """A console that cannot represent a character substitutes it instead of aborting.

    Mutates the process's `sys.stdout` and `sys.stderr`, so only `run()` calls it.
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
    """Console entry point (`ngb-xpr` and `python -m ngb_tools.xpr`).

    Tolerates unencodable console output and runs `main`. `main` stays free of
    console side effects for the tests and for anyone importing it.
    """
    tolerate_unencodable_output()
    return main()
