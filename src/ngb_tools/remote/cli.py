"""Client of a loader started with --remote: `python -m ngb_tools.remote COMMAND...`.

Each argument is one command line for the running game, sent in order; each answer is printed:

    python -m ngb_tools.remote status
    python -m ngb_tools.remote "god on" "trace on" "input t=0p:A,t=15p:Y" status

Commands (port/src/remote.h): `input <script>` (the loader's --input-script syntax, times counted
from now), `trace on|off`, `god on|off`, `shot`, `status`, `help`.

Exit codes: 0 every command answered "ok", 1 one answered "error", 2 no loader listening.
"""

from __future__ import annotations

import argparse
import sys
from collections.abc import Sequence
from typing import Final

PIPE: Final = r"\\.\pipe\ngb-loader"
PROG: Final = "python -m ngb_tools.remote"


def send(commands: Sequence[str], pipe: str = PIPE) -> list[str]:
    """Sends each command and returns the answers, one per command."""
    answers = []
    with open(pipe, "r+b", buffering=0) as channel:  # noqa: PTH123 (a named pipe, not a path)
        for command in commands:
            channel.write(command.encode("utf-8") + b"\n")
            line = b""
            while not line.endswith(b"\n"):
                chunk = channel.read(1)
                if not chunk:
                    break
                line += chunk
            answers.append(line.decode("utf-8", "replace").rstrip("\n"))
    return answers


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog=PROG, description="Commands for a loader running with --remote."
    )
    parser.add_argument("commands", nargs="+", help='e.g. status, "god on", "input t=0p:Y"')
    parser.add_argument("--pipe", default=PIPE, help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    try:
        answers = send(args.commands, args.pipe)
    except OSError as error:
        print(
            f"{PROG}: no loader listening on {args.pipe} ({error.strerror or error})",
            file=sys.stderr,
        )
        return 2
    for command, answer in zip(args.commands, answers, strict=False):
        print(f"{command} -> {answer}")
    return 1 if any(not a.startswith("ok") for a in answers) else 0


def run() -> int:
    return main()
