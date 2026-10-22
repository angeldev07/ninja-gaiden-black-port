"""Gallery CLI: `python -m ngb_tools.gallery build [--out DIR] [--game DIR] [--only X]`.

Walks the `chr_*.afs` files in `--game` (default `$NGB_DATA/game`) and leaves in `--out`
(default `$NGB_DATA/extracted/gallery`, never inside a repo) one `<character>.glb` per
character, `gallery.png` (contact sheet), `gallery.md` and `gallery.json`. `--only`
(repeatable) limits the gallery to those characters (`ryu`, `chr_ryu` or `chr_ryu.afs`).

Exit codes:

- 0: all the characters were exported.
- 1: some failed (recorded in the report with its reason; the others are exported anyway).
- 2: usage or I/O error: game folder, output inside the repo, files.

On game data it runs in isolated mode (`-I`, or `-P -s` with PYTHONPATH=src).
"""

from __future__ import annotations

import argparse
import io
import sys
from collections.abc import Sequence
from enum import IntEnum
from pathlib import Path
from typing import Final

from ngb_tools._paths import (
    DATA_ENV_VAR,
    EXTRACTED_SUBDIR,
    GAME_SUBDIR,
    UnsafeOutputError,
    atomic_write_bytes,
    data_root,
    ensure_outside_repos,
)
from ngb_tools.gallery.build import GalleryEntry, Status, build_gallery
from ngb_tools.gallery.report import manifest, markdown, summary
from ngb_tools.gallery.sheet import contact_sheet
from ngb_tools.gallery.sources import MotionIndex, character_archives, common_archives

PROG: Final = "python -m ngb_tools.gallery"
GALLERY_SUBDIR: Final = "gallery"
TILE_SIZE: Final = (180, 240)
COLUMNS: Final = 8


class ExitCode(IntEnum):
    OK = 0
    FINDINGS = 1
    ERROR = 2


class UsageError(Exception):
    """Arguments that cannot be satisfied (exit code 2)."""


def _selected(archives: Sequence[Path], only: Sequence[str]) -> list[Path]:
    if not only:
        return list(archives)
    wanted = {name.lower().removesuffix(".afs").removeprefix("chr_") for name in only}
    chosen = [p for p in archives if p.stem.lower().removeprefix("chr_") in wanted]
    missing = wanted - {p.stem.lower().removeprefix("chr_") for p in chosen}
    if missing:
        raise UsageError(f"--only: no chr_*.afs for {', '.join(sorted(missing))}")
    return chosen


def _progress(entry: GalleryEntry) -> None:
    if entry.status is Status.OK:
        anim = "anim" if entry.motion else "no anim"
        print(f"  ok     {entry.archive:<16} {entry.triangles:>6} tris  {anim}", flush=True)
    else:
        print(f"  FAILED {entry.archive:<16} {entry.reason}", flush=True)


def cmd_build(args: argparse.Namespace) -> ExitCode:
    game: Path = args.game if args.game is not None else data_root() / GAME_SUBDIR
    out: Path = (
        args.out if args.out is not None else data_root() / EXTRACTED_SUBDIR / GALLERY_SUBDIR
    )
    out = ensure_outside_repos(out)
    if not game.is_dir():
        raise UsageError(f"game folder does not exist: {game}")
    archives = _selected(character_archives(game), args.only)
    if not archives:
        raise UsageError(f"no chr_*.afs in {game}")
    print(f"{len(archives)} characters in {game} -> {out}")
    results = build_gallery(archives, MotionIndex(common_archives(game)), out, TILE_SIZE, _progress)
    total, ok, animated = summary(results)
    title = f"NINJA GAIDEN BLACK - CHARACTERS {ok}/{total} - {animated} ANIMATED"
    atomic_write_bytes(out / "gallery.png", contact_sheet(results, TILE_SIZE, COLUMNS, title))
    atomic_write_bytes(out / "gallery.md", markdown(results).encode("utf-8"))
    atomic_write_bytes(out / "gallery.json", manifest(results).encode("utf-8"), fsync=True)
    print(f"{ok} of {total} exported, {animated} with animated idle")
    for name in ("gallery.png", "gallery.md", "gallery.json"):
        print(f"-> {out / name}")
    return ExitCode.OK if ok == total else ExitCode.FINDINGS


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog=PROG, description="Gallery of the game characters.")
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("build", help="GLB per character, contact sheet and report")
    p.add_argument(
        "--out", type=Path, metavar="DIR", help=f"default: ${DATA_ENV_VAR}/extracted/gallery"
    )
    p.add_argument("--game", type=Path, metavar="DIR", help=f"default: ${DATA_ENV_VAR}/game")
    p.add_argument("--only", action="append", default=[], metavar="CHR", help="repeatable")
    return parser


def tolerate_unencodable_output() -> None:
    """A console that cannot represent a character substitutes it instead of aborting."""
    for stream in (sys.stdout, sys.stderr):
        if isinstance(stream, io.TextIOWrapper):
            stream.reconfigure(errors="replace")


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return cmd_build(args)
    except (UnsafeOutputError, UsageError, OSError) as err:
        print(f"error: {err}", file=sys.stderr)
        return ExitCode.ERROR


def run() -> int:
    """Console entry (`ngb-gallery` and `python -m ngb_tools.gallery`)."""
    tolerate_unencodable_output()
    return main()
