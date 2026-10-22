"""CLI of the XBE inspector.

Usage (with the game data never on `sys.path`):

    python -P -s -m ngb_tools.xbe <path.xbe> [--json X] [--md Y] [--strings]
        [--min-len N] [--exports TSV]

Without `--json` or `--md`, the Markdown summary goes to stdout.

Exit codes:

- 0: the XBE is valid and the reports were written.
- 1: validation failure. If the file is not a coherent XBE (magic,
  truncated structure or one outside its region, range that overflows the image
  or 32 bits, runaway counter, thunk table without terminator) there is no
  report; if it is coherent but does not verify (section SHA-1 digest,
  overlapping sections, unknown or ambiguous XOR keys), the reports are
  written anyway so the failure can be studied.
- 2: usage or I/O error (arguments, missing file, malformed exports table,
  unwritable output or one that would overwrite the input).
"""

from __future__ import annotations

import argparse
import io
import sys
from collections.abc import Sequence
from enum import IntEnum
from pathlib import Path

from ngb_tools.xbe.analysis import analyze
from ngb_tools.xbe.errors import KernelExportsError, XbeFormatError
from ngb_tools.xbe.image import XbeImage
from ngb_tools.xbe.kernel import load_kernel_exports
from ngb_tools.xbe.markdown import render_markdown
from ngb_tools.xbe.report import render_json
from ngb_tools.xbe.strings import DEFAULT_MIN_LEN


class ExitCode(IntEnum):
    OK = 0
    VALIDATION = 1
    USAGE = 2  # the same one argparse uses for its errors


class UsageError(Exception):
    """Combination of arguments that cannot be run."""


def _positive_int(text: str) -> int:
    try:
        value = int(text)
    except ValueError:
        raise argparse.ArgumentTypeError(f"not an integer: {text!r}") from None
    if value < 1:
        raise argparse.ArgumentTypeError(f"must be >= 1 (got {value})")
    return value


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="python -m ngb_tools.xbe",
        description="Inspector for XBE executables (original Xbox).",
    )
    parser.add_argument("xbe", type=Path, help="path to the .xbe")
    parser.add_argument("--json", type=Path, help="full JSON report")
    parser.add_argument("--md", type=Path, help="Markdown summary (no outputs: stdout)")
    parser.add_argument("--strings", action="store_true", help="extract and classify strings")
    parser.add_argument(
        "--min-len",
        type=_positive_int,
        default=DEFAULT_MIN_LEN,
        help=f"minimum string length ({DEFAULT_MIN_LEN})",
    )
    parser.add_argument(
        "--exports",
        type=Path,
        help="xboxkrnl exports TSV (default: the table bundled in the package)",
    )
    return parser


def _check_outputs(xbe: Path, outputs: Sequence[Path]) -> None:
    """No output may be the XBE itself: the game data is immutable."""
    source = xbe.resolve()
    for output in outputs:
        if output.resolve() == source:
            raise UsageError(f"output {output} is the input file")


def _write_text(path: Path, text: str) -> None:
    """UTF-8 with LF line endings, creating intermediate directories."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def _fail(code: ExitCode, message: object) -> ExitCode:
    print(f"error: {message}", file=sys.stderr)
    return code


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    xbe_path: Path = args.xbe
    json_path: Path | None = args.json
    md_path: Path | None = args.md
    exports_path: Path | None = args.exports
    with_strings: bool = args.strings
    min_len: int = args.min_len
    outputs = [path for path in (json_path, md_path) if path is not None]

    try:
        _check_outputs(xbe_path, outputs)
        exports = load_kernel_exports(exports_path)
        image = XbeImage.load(xbe_path)
        analysis = analyze(image, exports, with_strings=with_strings, min_len=min_len)
    except (UsageError, OSError, KernelExportsError) as exc:
        return _fail(ExitCode.USAGE, exc)
    except XbeFormatError as exc:
        return _fail(ExitCode.VALIDATION, exc)

    try:
        if json_path is not None:
            _write_text(json_path, render_json(analysis))
        if md_path is not None:
            _write_text(md_path, render_markdown(analysis))
    except OSError as exc:
        return _fail(ExitCode.USAGE, exc)
    if not outputs:
        sys.stdout.write(render_markdown(analysis))

    issues = image.validation_issues()
    for issue in issues:
        print(f"validation: {issue}", file=sys.stderr)
    return ExitCode.VALIDATION if issues else ExitCode.OK


def run() -> int:
    """Console entry point (`ngb-xbe` and `python -m ngb_tools.xbe`).

    The Windows console is not always UTF-8: it is forced to avoid mojibake in
    accented characters and in the build's Shift-JIS paths. It lives here and not in
    `main` so tests and anyone importing `main` do not see the global `sys.stdout`
    mutated.
    """
    for stream in (sys.stdout, sys.stderr):
        if isinstance(stream, io.TextIOWrapper):
            stream.reconfigure(encoding="utf-8")
    return main()
