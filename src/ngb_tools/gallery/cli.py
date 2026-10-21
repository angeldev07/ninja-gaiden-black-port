"""CLI de la galería: `python -m ngb_tools.gallery build [--out DIR] [--game DIR] [--only X]`.

Recorre los `chr_*.afs` de `--game` (por defecto `$NGB_DATA/game`) y deja en `--out` (por
defecto `$NGB_DATA/extracted/gallery`, nunca dentro de un repo) un `<personaje>.glb` por
personaje, `gallery.png` (hoja de contactos), `gallery.md` y `gallery.json`. `--only`
(repetible) limita la galería a esos personajes (`ryu`, `chr_ryu` o `chr_ryu.afs`).

Códigos de salida:

- 0: todos los personajes se exportaron.
- 1: alguno falló (queda en el informe con su motivo; los demás sí se exportan).
- 2: error de uso o de E/S: carpeta del juego, salida dentro del repo, archivos.

Sobre datos del juego se ejecuta en modo aislado (`-I`, o `-P -s` con PYTHONPATH=src).
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
    """Argumentos que no se pueden cumplir (código 2)."""


def _selected(archives: Sequence[Path], only: Sequence[str]) -> list[Path]:
    if not only:
        return list(archives)
    wanted = {name.lower().removesuffix(".afs").removeprefix("chr_") for name in only}
    chosen = [p for p in archives if p.stem.lower().removeprefix("chr_") in wanted]
    missing = wanted - {p.stem.lower().removeprefix("chr_") for p in chosen}
    if missing:
        raise UsageError(f"--only: no hay chr_*.afs para {', '.join(sorted(missing))}")
    return chosen


def _progress(entry: GalleryEntry) -> None:
    if entry.status is Status.OK:
        anim = "anim" if entry.motion else "sin anim"
        print(f"  ok     {entry.archive:<16} {entry.triangles:>6} tris  {anim}", flush=True)
    else:
        print(f"  FALLO  {entry.archive:<16} {entry.reason}", flush=True)


def cmd_build(args: argparse.Namespace) -> ExitCode:
    game: Path = args.game if args.game is not None else data_root() / GAME_SUBDIR
    out: Path = (
        args.out if args.out is not None else data_root() / EXTRACTED_SUBDIR / GALLERY_SUBDIR
    )
    out = ensure_outside_repos(out)
    if not game.is_dir():
        raise UsageError(f"no existe la carpeta del juego {game}")
    archives = _selected(character_archives(game), args.only)
    if not archives:
        raise UsageError(f"no hay chr_*.afs en {game}")
    print(f"{len(archives)} personajes en {game} -> {out}")
    results = build_gallery(archives, MotionIndex(common_archives(game)), out, TILE_SIZE, _progress)
    total, ok, animated = summary(results)
    title = f"NINJA GAIDEN BLACK - PERSONAJES {ok}/{total} - {animated} ANIMADOS"
    atomic_write_bytes(out / "gallery.png", contact_sheet(results, TILE_SIZE, COLUMNS, title))
    atomic_write_bytes(out / "gallery.md", markdown(results).encode("utf-8"))
    atomic_write_bytes(out / "gallery.json", manifest(results).encode("utf-8"), fsync=True)
    print(f"{ok} de {total} exportados, {animated} con idle animado")
    for name in ("gallery.png", "gallery.md", "gallery.json"):
        print(f"-> {out / name}")
    return ExitCode.OK if ok == total else ExitCode.FINDINGS


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog=PROG, description="Galería de los personajes del juego.")
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("build", help="GLB por personaje, hoja de contactos e informe")
    p.add_argument(
        "--out", type=Path, metavar="DIR", help=f"por defecto ${DATA_ENV_VAR}/extracted/gallery"
    )
    p.add_argument("--game", type=Path, metavar="DIR", help=f"por defecto ${DATA_ENV_VAR}/game")
    p.add_argument("--only", action="append", default=[], metavar="CHR", help="repetible")
    return parser


def tolerate_unencodable_output() -> None:
    """Una consola que no sabe representar un carácter lo sustituye en vez de abortar."""
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
    """Entrada de consola (`python -m ngb_tools.gallery`; el entry point lo añade el líder)."""
    tolerate_unencodable_output()
    return main()
