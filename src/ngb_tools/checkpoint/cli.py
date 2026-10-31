"""CLI of the checkpoint tool: `python -m ngb_tools.checkpoint {saves,add,list,prepare,run}`.

    saves   [--data DIR]              the saves in a data folder, newest first
    add     NAME [--data DIR] [--save ID] [--note TEXT] [--replace]
                                      copy one save (the newest by default) into the library
    list                              the checkpoints in the library
    prepare NAME [--game DIR]         a data folder that loads only that save
    run     NAME [--game DIR] [--loader EXE] [--input SCRIPT] [-- LOADER ARGS...]
                                      prepare it and start the loader, which loads the save

The library is `$NGB_DATA/checkpoints`; `--data` defaults to `$NGB_DATA` and `--game` to
`$NGB_DATA/game`. `run` passes `--input-script` with the load route plus `--input` (polls
counted from the start, e.g. "t=6000p:Y"), and everything after `--` to the loader as it is:

    python -I -m ngb_tools.checkpoint add ch05-start --note "chapter 5, the first fight"
    python -I -m ngb_tools.checkpoint run ch05-start --input "t=6000p:A" -- --ach-trace

Exit codes: 0 done (for `run`, the loader's own code otherwise), 2 usage or I/O error.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from collections.abc import Sequence
from pathlib import Path
from typing import Final

from ngb_tools._paths import (
    DATA_ENV_VAR,
    GAME_SUBDIR,
    UnsafeOutputError,
    data_root,
    package_repo_root,
)
from ngb_tools.checkpoint.library import (
    LOAD_SCRIPT,
    CheckpointError,
    add,
    find_saves,
    list_checkpoints,
    prepare,
)

PROG: Final = "python -m ngb_tools.checkpoint"
ERROR: Final = 2


def _default_loader() -> Path | None:
    root = package_repo_root()
    return None if root is None else root / "build" / "port" / "ngb-loader.exe"


def cmd_saves(args: argparse.Namespace) -> int:
    for save in find_saves(args.data):
        print(f"{save.folder.name}  {save.title}  {save.name}")
    return 0


def cmd_add(args: argparse.Namespace) -> int:
    checkpoint = add(args.name, args.data, save=args.save, note=args.note, replace=args.replace)
    print(f"{checkpoint.name}: {checkpoint.save_name} -> {checkpoint.path}")
    return 0


def cmd_list(args: argparse.Namespace) -> int:
    for checkpoint in list_checkpoints():
        note = f"  ({checkpoint.note})" if checkpoint.note else ""
        print(f"{checkpoint.name:<24} {checkpoint.save_name}{note}")
    return 0


def cmd_prepare(args: argparse.Namespace) -> int:
    play = prepare(args.name, args.game)
    print(f"{DATA_ENV_VAR}={play}")
    print(f'--input-script "{LOAD_SCRIPT}"')
    return 0


def cmd_run(args: argparse.Namespace) -> int:
    loader: Path | None = args.loader or _default_loader()
    if loader is None or not loader.is_file():
        raise CheckpointError(f"no loader at {loader}; build it or pass --loader")
    play = prepare(args.name, args.game)
    script = LOAD_SCRIPT + (f",{args.input}" if args.input else "")
    extra: list[str] = args.loader_args
    command = [
        str(loader),
        str(play / GAME_SUBDIR / "default.xbe"),
        "--input-script",
        script,
        *extra,
    ]
    env = dict(os.environ, **{DATA_ENV_VAR: str(play)})
    print(f"{DATA_ENV_VAR}={play}")
    return subprocess.run(command, env=env, check=False).returncode


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog=PROG, description="Named game saves to load on demand.")
    sub = parser.add_subparsers(dest="command", required=True)
    data_help = f"data folder to read the saves from (default ${DATA_ENV_VAR})"
    game_help = f"game folder to link in (default ${DATA_ENV_VAR}/{GAME_SUBDIR})"

    p = sub.add_parser("saves", help="list the saves in a data folder")
    p.add_argument("--data", type=Path, default=data_root(), help=data_help)
    p.set_defaults(func=cmd_saves)

    p = sub.add_parser("add", help="copy a save into the library")
    p.add_argument("name")
    p.add_argument("--data", type=Path, default=data_root(), help=data_help)
    p.add_argument("--save", help="the save's folder name (default: the newest save)")
    p.add_argument("--note", default="")
    p.add_argument("--replace", action="store_true", help="overwrite a checkpoint of that name")
    p.set_defaults(func=cmd_add)

    p = sub.add_parser("list", help="list the checkpoints")
    p.set_defaults(func=cmd_list)

    for command, func, text in (
        ("prepare", cmd_prepare, "make the data folder that loads it"),
        ("run", cmd_run, "prepare and start the loader on it"),
    ):
        p = sub.add_parser(command, help=text)
        p.add_argument("name")
        p.add_argument("--game", type=Path, default=data_root() / GAME_SUBDIR, help=game_help)
        p.set_defaults(func=func)
        if command == "run":
            p.add_argument("--loader", type=Path, help="ngb-loader.exe (default: the repo's build)")
            p.add_argument(
                "--input", default="", help='more presses after the load, e.g. "t=6000p:Y"'
            )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    split = argv.index("--") if "--" in argv else len(argv)  # what follows goes to the loader
    args = build_parser().parse_args(argv[:split])
    args.loader_args = argv[split + 1 :]
    try:
        result: int = args.func(args)
    except (CheckpointError, UnsafeOutputError, OSError, subprocess.CalledProcessError) as error:
        print(f"{PROG}: {error}", file=sys.stderr)
        return ERROR
    return result


def run() -> int:
    return main()
