"""CLI de escenarios: `python -m ngb_tools.stage {info,export}`.

    info   <zona> [--game DIR]                    partes, variantes repetidas y recuentos
    export <zona> [--parts 01,02,...|all] [--out DIR] [--format glb|gltf] [--collision]
                  [--game DIR]

Las partes salen de `<zona>_NN.afs` y `<zona>_cmn.afs` en `--game` (por defecto
`$NGB_DATA/game`; ver `ngb_tools.stage.zone`). `export` escribe `<zona>.glb` (o `.gltf` +
`.bin`) en `--out`, por defecto `$NGB_DATA/extracted/stages/<zona>`, nunca dentro de un
repo git. Las texturas son los PNG de `$NGB_DATA/extracted/textures/<parte>`; si faltan o
no cuadran, se exportan ahí. Una parte idéntica a otra ya incluida se salta.

Códigos de salida:

- 0: todo correcto.
- 1: hallazgo sobre los datos: alguna parte ilegible, invariantes incumplidas o partes saltadas.
- 2: error de uso o de E/S: zona inexistente, argumentos, salida dentro del repo, archivos.

Sobre datos del juego se ejecuta en modo aislado (`-I`, o `-P -s` con PYTHONPATH=src).
"""

from __future__ import annotations

import argparse
import io
import sys
from collections.abc import Mapping, Sequence
from enum import IntEnum
from pathlib import Path
from types import MappingProxyType
from typing import Final

from ngb_tools._paths import (
    DATA_ENV_VAR,
    EXTRACTED_SUBDIR,
    GAME_SUBDIR,
    UnsafeOutputError,
    data_root,
    ensure_outside_repos,
)
from ngb_tools.afs import AfsError
from ngb_tools.kmd.errors import KmdError
from ngb_tools.stage.errors import StageError, ZoneNotFoundError
from ngb_tools.stage.gltf import SceneFormat, ScenePart, write_scene
from ngb_tools.stage.model import StageModel, parse_stage_kmd
from ngb_tools.stage.textures import resolve_textures, texture_dir
from ngb_tools.stage.zone import (
    LoadedPart,
    check_zone_name,
    find_zone_parts,
    load_parts,
    select_parts,
)
from ngb_tools.xpr.errors import XprError

PROG: Final = "python -m ngb_tools.stage"
STAGES_SUBDIR: Final = "stages"
ALL: Final = "all"


class ExitCode(IntEnum):
    OK = 0
    FINDINGS = 1
    ERROR = 2


def default_output_dir(zone: str, environ: Mapping[str, str] | None = None) -> Path:
    """`<datos>/extracted/stages/<zona>`."""
    return data_root(environ) / EXTRACTED_SUBDIR / STAGES_SUBDIR / check_zone_name(zone)


def _game_dir(args: argparse.Namespace) -> Path:
    return args.game if args.game is not None else data_root() / GAME_SUBDIR


def _wanted(text: str | None) -> list[str] | None:
    if text is None or text.strip().lower() == ALL:
        return None
    return [item.strip() for item in text.split(",") if item.strip()]


def _node_names(loaded: Sequence[LoadedPart]) -> list[str]:
    """Nombre de nodo (y de carpeta de texturas) único por parte."""
    names: list[str] = []
    for item in loaded:
        name = item.part.name
        if name in names:
            name = f"{name}_cmn" if item.part.common else f"{name}_{item.part.entry.index}"
        names.append(name)
    return names


def _origin(item: LoadedPart) -> str:
    return f"{item.part.afs.name}#{item.part.entry.index}"


def _parse(item: LoadedPart) -> StageModel:
    return parse_stage_kmd(item.data, item.part.name)


def _triangles(model: StageModel) -> int:
    return sum(
        len(model.triangles(ref))
        for obj in model.objects
        for ref, _ in obj.submeshes()
        if model.exportable(ref)
    )


def cmd_info(args: argparse.Namespace) -> ExitCode:
    parts = find_zone_parts(_game_dir(args), args.zone)
    findings = 0
    for item in load_parts(parts):
        head = f"{item.part.name:<12} {_origin(item):<20} {len(item.data):>9} B"
        if item.duplicate_of is not None:
            print(f"{head}  = {item.duplicate_of} (sha256 {item.sha256[:12]}, se salta)")
            continue
        try:
            model = _parse(item)
        except (StageError, KmdError, XprError) as error:
            print(f"{head}  ! ilegible: {error}")
            findings += 1
            continue
        collision = sum(len(c.triangles) for c in model.collision.values())
        print(
            f"{head}  {len(model.objects)} objetos, {model.part_count} VB, "
            f"{_triangles(model)} triángulos, {len(model.textures)} texturas, "
            f"colisión {collision} triángulos, {len(model.issues)} incidencias"
        )
        for issue in model.issues:
            print(f"  ! {issue}")
        findings += len(model.issues)
    return ExitCode.FINDINGS if findings else ExitCode.OK


def _scene_part(item: LoadedPart, name: str, problems: list[str]) -> ScenePart | None:
    try:
        model = _parse(item)
        textures = resolve_textures(
            model, item.part.afs / f"{item.part.name}.kmd", texture_dir(name)
        )
    except (StageError, KmdError, XprError) as error:
        problems.append(f"{name}: parte saltada: {error}")
        return None
    problems += [f"{name}: {issue}" for issue in model.issues]
    extras = {
        "source": _origin(item),
        "sha256": item.sha256,
        "kmd_objects": len(model.objects),
        "kmd_vertex_buffers": model.part_count,
        "common": item.part.common,
    }
    return ScenePart(name, model, textures.files, extras)


def cmd_export(args: argparse.Namespace) -> ExitCode:
    zone = check_zone_name(args.zone)
    out_dir: Path = args.out if args.out is not None else default_output_dir(zone)
    ensure_outside_repos(out_dir)  # antes de leer nada: no dejar texturas a medias
    parts = select_parts(find_zone_parts(_game_dir(args), zone), _wanted(args.parts))
    loaded = load_parts(parts)
    problems: list[str] = []
    scene: list[ScenePart] = []
    for item, name in zip(loaded, _node_names(loaded), strict=True):
        if item.duplicate_of is not None:
            print(f"{name}: idéntica a {item.duplicate_of}, se salta")
            continue
        part = _scene_part(item, name, problems)
        if part is not None:
            scene.append(part)
            print(f"{name}: {len(part.model.objects)} objetos, {len(part.textures)} PNG")
    if not scene:
        for problem in problems:
            print(f"! {problem}")
        print("error: ninguna parte exportable", file=sys.stderr)
        return ExitCode.FINDINGS
    fmt = SceneFormat(args.format)
    paths, stats = write_scene(zone, scene, out_dir, fmt=fmt, collision=args.collision)
    print(
        f"{stats.parts} partes, {stats.objects} objetos, {stats.vertices} vértices, "
        f"{stats.triangles} triángulos, {stats.primitives} primitivas, "
        f"{stats.materials} materiales, colisión {stats.collision_triangles} triángulos"
    )
    for problem in (*problems, *stats.warnings):
        print(f"! {problem}")
    for path in paths:
        print(f"-> {path}")
    return ExitCode.FINDINGS if problems or stats.warnings else ExitCode.OK


_COMMANDS: Final = MappingProxyType({"info": cmd_info, "export": cmd_export})


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog=PROG, description="Escenarios de Team Ninja: una zona entera a glTF/glb."
    )
    sub = parser.add_subparsers(dest="command", required=True)
    game_help = f"carpeta con los AFS; por defecto ${DATA_ENV_VAR}/{GAME_SUBDIR}"

    p = sub.add_parser("info", help="partes de la zona, variantes repetidas y recuentos")
    p.add_argument("zone", metavar="zona")
    p.add_argument("--game", type=Path, metavar="DIR", help=game_help)

    p = sub.add_parser("export", help="exporta la zona a una escena glTF (fuera del repo)")
    p.add_argument("zone", metavar="zona")
    p.add_argument("--parts", metavar="01,02|all", help="partes por número; por defecto todas")
    p.add_argument(
        "--out",
        type=Path,
        metavar="DIR",
        help=f"por defecto ${DATA_ENV_VAR}/extracted/stages/<zona>",
    )
    p.add_argument("--format", choices=[f.value for f in SceneFormat], default=SceneFormat.GLB)
    p.add_argument("--collision", action="store_true", help="añade HITMDL (malla translúcida)")
    p.add_argument("--game", type=Path, metavar="DIR", help=game_help)
    return parser


def tolerate_unencodable_output() -> None:
    """Una consola que no sabe representar un carácter lo sustituye en vez de abortar."""
    for stream in (sys.stdout, sys.stderr):
        if isinstance(stream, io.TextIOWrapper):
            stream.reconfigure(errors="replace")


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    command = _COMMANDS[args.command]
    try:
        return command(args)
    except ZoneNotFoundError as err:
        print(f"error: {err}", file=sys.stderr)
        return ExitCode.ERROR
    except (StageError, KmdError, XprError, AfsError) as err:
        print(f"error: {err}", file=sys.stderr)
        return ExitCode.FINDINGS
    except (UnsafeOutputError, OSError) as err:
        print(f"error: {err}", file=sys.stderr)
        return ExitCode.ERROR


def run() -> int:
    """Entrada de consola (`python -m ngb_tools.stage`; el entry point lo añade el líder)."""
    tolerate_unencodable_output()
    return main()
