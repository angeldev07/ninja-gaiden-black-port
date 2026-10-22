"""CLI of the AFS tool: `python -I -m ngb_tools.afs {list,extract,inventory}`.

Exit codes:

- 0: everything fine.
- 1: finding about the data: invariant issues or an unreadable AFS.
- 2: usage or I/O error: arguments, indices, output inside a repo, files.

Outputs: `extract` never writes inside a git repo. The `inventory` reports (`--json`,
`--md`) carry names and headers of the game files, so they go through the same guard with a
single explicit exception: `build/` of the tools repo, which .gitignore ignores and the
hygiene guard blocks, and where reports go by convention (`build/reports/afs/`).

On game data it runs in isolated mode (`-I`, or `-P -s` with PYTHONPATH=src), so that no
`.py` in the data directory enters `sys.path`. `__main__` also makes the console substitute
characters it cannot represent; `main()` does not touch `sys.stdout`.
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
    print(f"{len(manifest['entries'])} entries -> {out_dir.resolve()}")
    return ExitCode.OK


def _write_report(target: Path, text: str) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    atomic_write_bytes(target, text.encode("utf-8"), fsync=True)  # small: fsync is cheap


def cmd_inventory(args: argparse.Namespace) -> ExitCode:
    root: Path = args.root
    requested: tuple[tuple[Path | None, Callable[[InventoryRecord], str]], ...] = (
        (args.json, render_json),
        (args.md, render_markdown),
    )
    # The destinations are validated before inventorying: a forbidden one costs no traversal.
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
        f"{totals['archives']} AFS, {totals['entries']} entries, "
        f"{totals['pct_named']} % named, {totals['entry_bytes']} bytes in entries"
    )
    with_issues = totals["archives"] - inventory["summary"]["invariants"]["archives_ok"]
    if with_issues:
        print(f"{with_issues} AFS with issues (see the report)", file=sys.stderr)
        return ExitCode.FINDINGS
    return ExitCode.OK


_COMMANDS: Final = MappingProxyType(
    {"list": cmd_list, "extract": cmd_extract, "inventory": cmd_inventory}
)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog=PROG, description="AFS containers (CRI Middleware).")
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("list", help="list the entries and their issues")
    p.add_argument("afs", type=Path)

    p = sub.add_parser("extract", help="extract entries (always outside the repo)")
    p.add_argument("afs", type=Path)
    p.add_argument(
        "out_dir",
        nargs="?",
        type=Path,
        help=f"default: ${DATA_ENV_VAR}/extracted/<afs path inside game/>",
    )
    p.add_argument("--index", type=int, action="append", metavar="N", help="repeatable")

    p = sub.add_parser("inventory", help="streaming inventory of all the .afs files")
    p.add_argument("root", type=Path)
    p.add_argument("--json", type=Path, metavar="PATH", help="JSON report")
    p.add_argument("--md", type=Path, metavar="PATH", help="Markdown report")
    return parser


def tolerate_unencodable_output() -> None:
    """A console that cannot represent a character substitutes it instead of aborting.

    Mutates the process `sys.stdout`, so only the entry point (`__main__`) calls it.
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
    """Console entry (`ngb-afs` and `python -m ngb_tools.afs`).

    Tolerates console output that cannot be encoded and runs `main`. `main` stays free of
    effects on `sys.stdout` for the tests and for whoever imports it.
    """
    tolerate_unencodable_output()
    return main()
