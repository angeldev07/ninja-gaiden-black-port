"""CLI of the KMD tool: `python -m ngb_tools.kmd {info,export}`.

    info   <kmd>                          objects, groups, textures, bones and issues
    export <kmd> [--out DIR] [--format gltf|glb|obj] [--all-groups] [--skin] [--textures DIR]
                 [--motions BANK[:ids]]... [--motion ids|all] [--attach WEAPON@ANCHOR]...
                 [--name NAME]

`export` writes `<file stem>.gltf` + `.bin` (or `.obj` + `.mtl`). Without `--out` it goes to
`$NGB_DATA/extracted/models/<file stem>`, never inside a git repo. The textures are the PNGs
from `python -m ngb_tools.xpr export --kmd`: in `--textures`, by default
`$NGB_DATA/extracted/textures/<file stem>`. If they are missing or do not match the KMD,
they are exported right there. By default only the detail and skin groups come out;
`--all-groups` adds LOD, shadow and effects.

`--motions` (repeatable) adds glTF animations from a `.mtn` or `.mot` bank. After `:` come
the ids (`ryu.mtn:1,3,90`) or `all`; without them, `--motion` is used (default `all`: every
distinct motion of the bank). Each clip is named `<bank>#<id>`. It needs the humanoid rig of
`ngb_tools.kmd.rig`: if the KMD lacks it, that is a finding (1). Animations do not go in
OBJ: `--format obj` with `--motions` is a usage error (2).

`--attach` (repeatable) mounts another KMD, a weapon, on a bone of the model: the exporter
hangs it as a child of that node so it inherits the animation. There are two anchor forms:

    weapon.kmd@weapon.itm[#state]       the anchor table of the .itm (default state 2:
                                        the weapon in the right hand)
    weapon.kmd@BONE[:x,y,z[,rx,ry,rz]]  a model object and an offset in meters and
                                        degrees

`--name` changes the name of the output files (default: that of the KMD).

Exit codes:

- 0: all good.
- 1: finding about the data: unreadable KMD, violated invariants or skipped parts.
- 2: usage or I/O error: arguments, output inside the repo, files.

On game data it runs in isolated mode (`-I`, or `-P -s` with PYTHONPATH=src).
`run()` also makes the console substitute characters it cannot represent.
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
UNNAMED: Final = "unnamed"
_IDS: Final = re.compile(r"all|\d+(,\d+)*")
_NAME: Final = re.compile(r"[A-Za-z0-9_.-]+")
IDENTITY_QUAT: Final = (0.0, 0.0, 0.0, 1.0)


class UsageError(Exception):
    """Arguments valid for argparse but inconsistent with each other (code 2)."""


class ExitCode(IntEnum):
    OK = 0
    FINDINGS = 1
    ERROR = 2


def model_stem(path: Path) -> str:
    """Base name of the output; one made only of dots or spaces is replaced."""
    return path.stem if path.stem.strip(" .") else UNNAMED


def default_output_dir(path: Path, environ: Mapping[str, str] | None = None) -> Path:
    """`<data>/extracted/models/<KMD name without extension>`."""
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
    return "no HIERARC" if model.hierarchy is None else f"obj{model.hierarchy.root}"


def cmd_info(args: argparse.Namespace) -> ExitCode:
    model = _load(args.file)
    groups = Counter(o.group for o in model.objects)
    bones = sum(1 for o in model.objects if model.children(o.index))
    print(
        f"{args.file.name}: {model.tag}, {len(model.objects)} objects, {len(model.textures)} "
        f"textures, {len(model.push_buffers)} push buffers, root {_root(model)}"
    )
    print("groups: " + ", ".join(f"{group_name(g)} {n}" for g, n in sorted(groups.items())))
    print(f"bones (objects with children): {bones}")
    print(
        f"{'obj':>4} {'group':<8} {'fvf':>5} {'vert':>5} {'tris':>5} {'sub':>3} "
        f"{'parent':>6} {'child':>5}  textures"
    )
    for obj in model.objects:
        k = obj.index
        parent = model.parent(k)
        tris = _triangle_count(model, k) if obj.layout is not None else 0
        print(
            f"{k:>4} {group_name(obj.group):<8} {obj.mesh.fvf:>#5x} {obj.mesh.vertex_count:>5} "
            f"{tris:>5} {len(obj.mesh.submeshes):>3} {'-' if parent is None else parent:>6} "
            f"{len(model.children(k)):>5}  {_texture_list(model, k)}"
        )
    return ExitCode.FINDINGS if _print_issues(model) else ExitCode.OK


def split_motion_spec(spec: str, default: str) -> tuple[Path, str]:
    """`BANK[:ids]` into (path, ids). The colon of `C:/x.mtn` does not split ids."""
    path, sep, ids = spec.rpartition(":")
    if sep and path and _IDS.fullmatch(ids):
        return Path(path), ids
    return Path(spec), default


def load_clips(model: KmdModel, specs: Sequence[str], default: str) -> list[AnimationClip]:
    """Clips of each `--motions` bank, with the requested ids (or all its motions)."""
    if not _IDS.fullmatch(default):
        raise UsageError(f"--motion {default!r}: expected 'all' or comma-separated ids")
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
    """`BONE[:x,y,z[,rx,ry,rz]]`: object, translation and rotation (degrees) of the anchor."""
    bone_text, colon, offset_text = target.partition(":")
    try:
        bone = int(bone_text)
        values = [float(v) for v in offset_text.split(",")] if colon else []
    except ValueError as error:
        raise UsageError(f"--attach: unreadable anchor {target!r}") from error
    if len(values) not in (0, 3, 6):
        raise UsageError("--attach: the offset is 3 or 6 numbers (x,y,z[,rx,ry,rz])")
    translation = (values[0], values[1], values[2]) if values else (0.0, 0.0, 0.0)
    rotation = IDENTITY_QUAT
    if len(values) == 6:
        rotation = quaternion(euler_xyz(*(math.radians(v) for v in values[3:])))
    return bone, translation, rotation


def load_attachment(model: KmdModel, spec: str) -> Attachment:
    """One `--attach`: the weapon KMD, its textures and the anchor (from the .itm or explicit)."""
    weapon_text, at, target = spec.rpartition("@")
    if not at or not weapon_text or not target:
        raise UsageError(f"--attach {spec!r}: expected WEAPON.kmd@ANCHOR")
    weapon_path = Path(weapon_text)
    weapon = _load(weapon_path)
    item_text, hash_sign, state_text = target.partition("#")
    if item_text.lower().endswith(".itm"):
        if hash_sign and not state_text.isdigit():
            raise UsageError(f"--attach: state {state_text!r} is not a number")
        state = int(state_text) if hash_sign else HELD_STATE
        anchor = find_anchor(parse_item_anchors(Path(item_text).read_bytes()), state)
        bone, translation, rotation = anchor.bone, anchor.position, anchor.quaternion
    else:
        bone, translation, rotation = _bone_anchor(target)
    if not 0 <= bone < len(model.objects):
        raise UsageError(f"--attach: bone {bone} is not an object of the model")
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
        raise UsageError("animations (--motions) do not fit in OBJ: use gltf or glb")
    if args.attach and args.format == "obj":
        raise UsageError("mounted models (--attach) do not fit in OBJ: use gltf or glb")
    if args.name is not None and not (_NAME.fullmatch(args.name) and args.name.strip(".")):
        raise UsageError(f"--name {args.name!r}: only letters, digits, _ . -")
    model = _load(args.file)
    out_dir: Path = args.out if args.out is not None else default_output_dir(args.file)
    ensure_outside_repos(out_dir)  # before exporting textures: nothing half-done
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
            f"{obj_stats.objects} objects, {obj_stats.vertices} vertices, "
            f"{obj_stats.triangles} triangles, {obj_stats.materials} materials"
        )
    elif args.format == "glb":
        glb_path, stats = write_glb(model, out_dir, stem, files, options)
        paths = (glb_path,)
        warnings = stats.warnings
        summary = (
            f"{stats.objects} objects, {stats.vertices} vertices, {stats.triangles} triangles, "
            f"{stats.primitives} primitives, {stats.materials} materials (GLB)"
        )
    else:
        paths, stats = write_gltf(model, out_dir, stem, files, options)
        warnings = stats.warnings
        summary = (
            f"{stats.objects} objects, {stats.vertices} vertices, {stats.triangles} triangles, "
            f"{stats.primitives} primitives, {stats.materials} materials"
        )
    origin = "exported now" if exported else "reused"
    print(f"textures: {len(files)} PNG {origin} in {texture_dir.resolve()}")
    print(summary)
    if clips:
        frames = sum(len(clip.times) for clip in clips)
        print(f"animations: {len(clips)} clips, {frames} samples at {clips[0].extras['fps']} fps")
    for attachment in attachments:
        print(f"mounted: {attachment.name} on obj{attachment.parent}")
    for warning in warnings:
        print(f"warning: {warning}")
    findings = _print_issues(model) + len(warnings)
    for path in paths:
        print(f"-> {path}")
    return ExitCode.FINDINGS if findings else ExitCode.OK


_COMMANDS: Final = MappingProxyType({"info": cmd_info, "export": cmd_export})


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog=PROG, description="Team Ninja KMD models: summary and export to glTF/OBJ."
    )
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("info", help="objects, groups, textures, bones and issues")
    p.add_argument("file", type=Path)

    p = sub.add_parser("export", help="export to glTF 2.0 or OBJ (always outside the repo)")
    p.add_argument("file", type=Path)
    p.add_argument(
        "--out",
        type=Path,
        metavar="DIR",
        help=f"default ${DATA_ENV_VAR}/extracted/models/<file stem>",
    )
    p.add_argument("--format", choices=("gltf", "glb", "obj"), default="gltf")
    p.add_argument("--all-groups", action="store_true", help="also LOD, shadow and effects")
    p.add_argument("--skin", action="store_true", help="glTF: skin for the XYZB1 pieces")
    p.add_argument(
        "--textures",
        type=Path,
        metavar="DIR",
        help=f"texture PNGs; default ${DATA_ENV_VAR}/extracted/textures/<file>",
    )
    p.add_argument(
        "--motions",
        action="append",
        default=[],
        metavar="BANK[:ids]",
        help="motion bank (.mtn/.mot) to animate; repeatable; optional ids after ':'",
    )
    p.add_argument(
        "--motion",
        default="all",
        metavar="ids|all",
        help="default ids for banks without ':ids' (default all)",
    )
    p.add_argument(
        "--attach",
        action="append",
        default=[],
        metavar="WEAPON.kmd@ANCHOR",
        help="mount another KMD on a bone: @weapon.itm[#state] or @BONE[:x,y,z[,rx,ry,rz]]",
    )
    p.add_argument("--name", metavar="NAME", help="name of the output files")
    return parser


def tolerate_unencodable_output() -> None:
    """A console that cannot represent a character substitutes it instead of aborting.

    Mutates the process `sys.stdout` and `sys.stderr`, so only `run()` calls it.
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
    """Console entry (`python -m ngb_tools.kmd`)."""
    tolerate_unencodable_output()
    return main()
