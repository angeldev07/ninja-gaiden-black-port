"""CLI de la herramienta KMD: `python -m ngb_tools.kmd {info,export}`.

    info   <kmd>                          objetos, grupos, texturas, huesos e incidencias
    export <kmd> [--out DIR] [--format gltf|glb|obj] [--all-groups] [--skin] [--textures DIR]
                 [--motions BANCO[:ids]]... [--motion ids|all] [--attach ARMA@ANCLA]...
                 [--name NOMBRE]

`export` escribe `<archivo sin extensión>.gltf` + `.bin` (o `.obj` + `.mtl`). Sin `--out`
va a `$NGB_DATA/extracted/models/<archivo sin extensión>`, nunca dentro de un repo git. Las
texturas son los PNG de `python -m ngb_tools.xpr export --kmd`: en `--textures`, por defecto
`$NGB_DATA/extracted/textures/<archivo sin extensión>`. Si faltan o no cuadran con el KMD,
se exportan ahí mismo. Por defecto solo salen los grupos de detalle y skin; `--all-groups`
añade LOD, sombra y efectos.

`--motions` (repetible) añade animaciones glTF desde un banco `.mtn` o `.mot`. Detrás de
`:` pueden ir los ids (`ryu.mtn:1,3,90`) o `all`; sin ellos, se usa `--motion` (por defecto
`all`: todas las motions distintas del banco). Cada clip se llama `<banco>#<id>`. Hace falta
el rig humanoide de `ngb_tools.kmd.rig`: si el KMD no lo tiene, es un hallazgo (1). Las
animaciones no van en OBJ: `--format obj` con `--motions` es un error de uso (2).

`--attach` (repetible) monta otro KMD, un arma, en un hueso del modelo: el exportador
lo cuelga como hijo de ese nodo y así hereda la animación. Hay dos formas de ancla:

    arma.kmd@arma.itm[#estado]          la tabla de anclajes del .itm (por defecto el
                                        estado 2: el arma en la mano derecha)
    arma.kmd@HUESO[:x,y,z[,rx,ry,rz]]   un objeto del modelo y un desplazamiento en
                                        metros y grados

`--name` cambia el nombre de los archivos de salida (por defecto, el del KMD).

Códigos de salida:

- 0: todo correcto.
- 1: hallazgo sobre los datos: KMD ilegible, invariantes incumplidas o partes saltadas.
- 2: error de uso o de E/S: argumentos, salida dentro del repo, archivos.

Sobre datos del juego se ejecuta en modo aislado (`-I`, o `-P -s` con PYTHONPATH=src).
`run()` hace además que la consola sustituya los caracteres que no sabe representar.
"""

from __future__ import annotations

import argparse
import io
import math
import re
import sys
from collections import Counter
from collections.abc import Mapping, Sequence
from enum import IntEnum
from pathlib import Path
from types import MappingProxyType
from typing import Final

from ngb_tools._paths import (
    DATA_ENV_VAR,
    EXTRACTED_SUBDIR,
    UnsafeOutputError,
    data_root,
    ensure_outside_repos,
)
from ngb_tools.kmd.animation import AnimationClip
from ngb_tools.kmd.attach import (
    HELD_STATE,
    Attachment,
    ItemFormatError,
    find_anchor,
    parse_item_anchors,
)
from ngb_tools.kmd.errors import KmdError
from ngb_tools.kmd.gltf import DEFAULT_GROUPS, ExportOptions, write_glb, write_gltf
from ngb_tools.kmd.model import KmdModel, parse_kmd
from ngb_tools.kmd.motion import parse_motion_bank
from ngb_tools.kmd.obj import write_obj
from ngb_tools.kmd.objinfo import group_name
from ngb_tools.kmd.rig import euler_xyz, find_rig, motion_clip, quaternion
from ngb_tools.kmd.textures import default_texture_dir, resolve_texture_files
from ngb_tools.xpr.errors import XprError

PROG: Final = "python -m ngb_tools.kmd"
MODELS_SUBDIR: Final = "models"
UNNAMED: Final = "sin_nombre"
_IDS: Final = re.compile(r"all|\d+(,\d+)*")
_NAME: Final = re.compile(r"[A-Za-z0-9_.-]+")
IDENTITY_QUAT: Final = (0.0, 0.0, 0.0, 1.0)


class UsageError(Exception):
    """Argumentos válidos para argparse pero incoherentes entre sí (código 2)."""


class ExitCode(IntEnum):
    OK = 0
    FINDINGS = 1
    ERROR = 2


def model_stem(path: Path) -> str:
    """Nombre base de la salida; uno hecho solo de puntos o espacios se sustituye."""
    return path.stem if path.stem.strip(" .") else UNNAMED


def default_output_dir(path: Path, environ: Mapping[str, str] | None = None) -> Path:
    """`<datos>/extracted/models/<nombre del KMD sin extensión>`."""
    return data_root(environ) / EXTRACTED_SUBDIR / MODELS_SUBDIR / model_stem(path)


def _load(path: Path) -> KmdModel:
    return parse_kmd(path.read_bytes())


def _print_issues(model: KmdModel) -> int:
    for issue in model.issues:
        print(f"! {issue}")
    return len(model.issues)


def _texture_list(model: KmdModel, obj: int) -> str:
    textures = sorted({s.base_texture for s in model.objects[obj].mesh.submeshes})
    return ",".join(str(t) for t in textures) or "-"


def _triangle_count(model: KmdModel, obj: int) -> int:
    subs = model.objects[obj].mesh.submeshes
    return sum(len(model.triangles(obj, i)) for i in range(len(subs)) if model.exportable(obj, i))


def _root(model: KmdModel) -> str:
    return "sin HIERARC" if model.hierarchy is None else f"obj{model.hierarchy.root}"


def cmd_info(args: argparse.Namespace) -> ExitCode:
    model = _load(args.file)
    groups = Counter(o.group for o in model.objects)
    bones = sum(1 for o in model.objects if model.children(o.index))
    print(
        f"{args.file.name}: {model.tag}, {len(model.objects)} objetos, {len(model.textures)} "
        f"texturas, {len(model.push_buffers)} push buffers, raíz {_root(model)}"
    )
    print("grupos: " + ", ".join(f"{group_name(g)} {n}" for g, n in sorted(groups.items())))
    print(f"huesos (objetos con hijos): {bones}")
    print(
        f"{'obj':>4} {'grupo':<8} {'fvf':>5} {'vért':>5} {'tris':>5} {'sub':>3} "
        f"{'padre':>5} {'hijos':>5}  texturas"
    )
    for obj in model.objects:
        k = obj.index
        parent = model.parent(k)
        tris = _triangle_count(model, k) if obj.layout is not None else 0
        print(
            f"{k:>4} {group_name(obj.group):<8} {obj.mesh.fvf:>#5x} {obj.mesh.vertex_count:>5} "
            f"{tris:>5} {len(obj.mesh.submeshes):>3} {'-' if parent is None else parent:>5} "
            f"{len(model.children(k)):>5}  {_texture_list(model, k)}"
        )
    return ExitCode.FINDINGS if _print_issues(model) else ExitCode.OK


def split_motion_spec(spec: str, default: str) -> tuple[Path, str]:
    """`BANCO[:ids]` en (ruta, ids). Los dos puntos de `C:/x.mtn` no separan ids."""
    path, sep, ids = spec.rpartition(":")
    if sep and path and _IDS.fullmatch(ids):
        return Path(path), ids
    return Path(spec), default


def load_clips(model: KmdModel, specs: Sequence[str], default: str) -> list[AnimationClip]:
    """Clips de cada banco de `--motions`, con los ids pedidos (o todas sus motions)."""
    if not _IDS.fullmatch(default):
        raise UsageError(f"--motion {default!r}: se esperaba 'all' o ids separados por comas")
    rig = find_rig(model)
    clips: list[AnimationClip] = []
    for spec in specs:
        path, ids = split_motion_spec(spec, default)
        bank = parse_motion_bank(path.read_bytes())
        chosen = bank.distinct_ids() if ids == "all" else tuple(int(i) for i in ids.split(","))
        for motion_id in chosen:
            name = f"{model_stem(path)}#{motion_id}"
            clips.append(motion_clip(rig, bank.motion(motion_id), name))
    return clips


def _bone_anchor(
    target: str,
) -> tuple[int, tuple[float, float, float], tuple[float, float, float, float]]:
    """`HUESO[:x,y,z[,rx,ry,rz]]`: objeto, traslación y rotación (grados) del ancla."""
    bone_text, colon, offset_text = target.partition(":")
    try:
        bone = int(bone_text)
        values = [float(v) for v in offset_text.split(",")] if colon else []
    except ValueError as error:
        raise UsageError(f"--attach: ancla {target!r} ilegible") from error
    if len(values) not in (0, 3, 6):
        raise UsageError("--attach: el desplazamiento son 3 o 6 números (x,y,z[,rx,ry,rz])")
    translation = (values[0], values[1], values[2]) if values else (0.0, 0.0, 0.0)
    rotation = IDENTITY_QUAT
    if len(values) == 6:
        rotation = quaternion(euler_xyz(*(math.radians(v) for v in values[3:])))
    return bone, translation, rotation


def load_attachment(model: KmdModel, spec: str) -> Attachment:
    """Un `--attach`: el KMD del arma, sus texturas y el ancla (del .itm o explícita)."""
    weapon_text, at, target = spec.rpartition("@")
    if not at or not weapon_text or not target:
        raise UsageError(f"--attach {spec!r}: se esperaba ARMA.kmd@ANCLA")
    weapon_path = Path(weapon_text)
    weapon = _load(weapon_path)
    item_text, hash_sign, state_text = target.partition("#")
    if item_text.lower().endswith(".itm"):
        if hash_sign and not state_text.isdigit():
            raise UsageError(f"--attach: estado {state_text!r} no es un número")
        state = int(state_text) if hash_sign else HELD_STATE
        anchor = find_anchor(parse_item_anchors(Path(item_text).read_bytes()), state)
        bone, translation, rotation = anchor.bone, anchor.position, anchor.quaternion
    else:
        bone, translation, rotation = _bone_anchor(target)
    if not 0 <= bone < len(model.objects):
        raise UsageError(f"--attach: el hueso {bone} no es un objeto del modelo")
    files, _ = resolve_texture_files(weapon, weapon_path, default_texture_dir(weapon_path))
    return Attachment(
        name=model_stem(weapon_path),
        model=weapon,
        texture_files=files,
        parent=bone,
        translation=translation,
        rotation=rotation,
    )


def cmd_export(args: argparse.Namespace) -> ExitCode:
    if args.motions and args.format == "obj":
        raise UsageError("las animaciones (--motions) no caben en OBJ: usa gltf o glb")
    if args.attach and args.format == "obj":
        raise UsageError("los modelos montados (--attach) no caben en OBJ: usa gltf o glb")
    if args.name is not None and not (_NAME.fullmatch(args.name) and args.name.strip(".")):
        raise UsageError(f"--name {args.name!r}: solo letras, cifras, _ . -")
    model = _load(args.file)
    out_dir: Path = args.out if args.out is not None else default_output_dir(args.file)
    ensure_outside_repos(out_dir)  # antes de exportar texturas: nada a medias
    texture_dir: Path = (
        args.textures if args.textures is not None else default_texture_dir(args.file)
    )
    files, exported = resolve_texture_files(model, args.file, texture_dir)
    clips = load_clips(model, args.motions, args.motion) if args.motions else []
    attachments = [load_attachment(model, spec) for spec in args.attach]
    options = ExportOptions(
        groups=None if args.all_groups else DEFAULT_GROUPS,
        skin=args.skin,
        animations=tuple(clips),
        attachments=tuple(attachments),
    )
    stem = args.name if args.name is not None else model_stem(args.file)
    paths: tuple[Path, ...]
    if args.format == "obj":
        paths, obj_stats = write_obj(model, out_dir, stem, files, options)
        warnings = obj_stats.warnings
        summary = (
            f"{obj_stats.objects} objetos, {obj_stats.vertices} vértices, "
            f"{obj_stats.triangles} triángulos, {obj_stats.materials} materiales"
        )
    elif args.format == "glb":
        glb_path, stats = write_glb(model, out_dir, stem, files, options)
        paths = (glb_path,)
        warnings = stats.warnings
        summary = (
            f"{stats.objects} objetos, {stats.vertices} vértices, {stats.triangles} triángulos, "
            f"{stats.primitives} primitivas, {stats.materials} materiales (GLB)"
        )
    else:
        paths, stats = write_gltf(model, out_dir, stem, files, options)
        warnings = stats.warnings
        summary = (
            f"{stats.objects} objetos, {stats.vertices} vértices, {stats.triangles} triángulos, "
            f"{stats.primitives} primitivas, {stats.materials} materiales"
        )
    origin = "exportadas ahora" if exported else "reutilizadas"
    print(f"texturas: {len(files)} PNG {origin} en {texture_dir.resolve()}")
    print(summary)
    if clips:
        frames = sum(len(clip.times) for clip in clips)
        print(f"animaciones: {len(clips)} clips, {frames} muestras a {clips[0].extras['fps']} fps")
    for attachment in attachments:
        print(f"montado: {attachment.name} en obj{attachment.parent}")
    for warning in warnings:
        print(f"aviso: {warning}")
    findings = _print_issues(model) + len(warnings)
    for path in paths:
        print(f"-> {path}")
    return ExitCode.FINDINGS if findings else ExitCode.OK


_COMMANDS: Final = MappingProxyType({"info": cmd_info, "export": cmd_export})


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog=PROG, description="Modelos KMD de Team Ninja: resumen y exportación a glTF/OBJ."
    )
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("info", help="objetos, grupos, texturas, huesos e incidencias")
    p.add_argument("file", type=Path)

    p = sub.add_parser("export", help="exporta a glTF 2.0 u OBJ (siempre fuera del repo)")
    p.add_argument("file", type=Path)
    p.add_argument(
        "--out",
        type=Path,
        metavar="DIR",
        help=f"por defecto ${DATA_ENV_VAR}/extracted/models/<archivo sin extensión>",
    )
    p.add_argument("--format", choices=("gltf", "glb", "obj"), default="gltf")
    p.add_argument("--all-groups", action="store_true", help="también LOD, sombra y efectos")
    p.add_argument("--skin", action="store_true", help="glTF: skin para las piezas XYZB1")
    p.add_argument(
        "--textures",
        type=Path,
        metavar="DIR",
        help=f"PNG de las texturas; por defecto ${DATA_ENV_VAR}/extracted/textures/<archivo>",
    )
    p.add_argument(
        "--motions",
        action="append",
        default=[],
        metavar="BANCO[:ids]",
        help="banco de motions (.mtn/.mot) a animar; repetible; ids opcionales tras ':'",
    )
    p.add_argument(
        "--motion",
        default="all",
        metavar="ids|all",
        help="ids por defecto para los bancos sin ':ids' (por defecto all)",
    )
    p.add_argument(
        "--attach",
        action="append",
        default=[],
        metavar="ARMA.kmd@ANCLA",
        help="monta otro KMD en un hueso: @arma.itm[#estado] o @HUESO[:x,y,z[,rx,ry,rz]]",
    )
    p.add_argument("--name", metavar="NOMBRE", help="nombre de los archivos de salida")
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
    except (KmdError, XprError, ItemFormatError) as err:
        print(f"error: {err}", file=sys.stderr)
        return ExitCode.FINDINGS
    except (UnsafeOutputError, UsageError, OSError) as err:
        print(f"error: {err}", file=sys.stderr)
        return ExitCode.ERROR


def run() -> int:
    """Entrada de consola (`python -m ngb_tools.kmd`; el entry point lo añade el líder)."""
    tolerate_unencodable_output()
    return main()
