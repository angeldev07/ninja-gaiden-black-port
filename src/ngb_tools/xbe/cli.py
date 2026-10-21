"""CLI del inspector de XBE.

Uso (con los datos del juego nunca en `sys.path`):

    python -P -s -m ngb_tools.xbe <ruta.xbe> [--json X] [--md Y] [--strings]
        [--min-len N] [--exports TSV]

Sin `--json` ni `--md`, el resumen Markdown sale por stdout.

Códigos de salida:

- 0: el XBE es válido y los informes se escribieron.
- 1: fallo de validación. Si el archivo no es un XBE coherente (magic,
  estructura truncada o fuera de su región, rango que desborda la imagen o los
  32 bits, contador desorbitado, tabla de thunks sin terminador) no hay
  informe; si lo es pero no verifica (digest SHA-1 de sección, secciones
  solapadas, claves XOR desconocidas o ambiguas), los informes se escriben
  igualmente para poder estudiar el fallo.
- 2: error de uso o de E/S (argumentos, archivo inexistente, tabla de exports
  mal formada, salida no escribible o que pisaría la entrada).
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
    USAGE = 2  # el mismo que usa argparse para sus errores


class UsageError(Exception):
    """Combinación de argumentos que no se puede ejecutar."""


def _positive_int(text: str) -> int:
    try:
        value = int(text)
    except ValueError:
        raise argparse.ArgumentTypeError(f"no es un entero: {text!r}") from None
    if value < 1:
        raise argparse.ArgumentTypeError(f"debe ser >= 1 (es {value})")
    return value


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="python -m ngb_tools.xbe",
        description="Inspector de ejecutables XBE (Xbox original).",
    )
    parser.add_argument("xbe", type=Path, help="ruta al .xbe")
    parser.add_argument("--json", type=Path, help="informe JSON completo")
    parser.add_argument("--md", type=Path, help="resumen Markdown (sin salidas: stdout)")
    parser.add_argument("--strings", action="store_true", help="extraer y clasificar cadenas")
    parser.add_argument(
        "--min-len",
        type=_positive_int,
        default=DEFAULT_MIN_LEN,
        help=f"longitud mínima de cadena ({DEFAULT_MIN_LEN})",
    )
    parser.add_argument(
        "--exports",
        type=Path,
        help="TSV de exports de xboxkrnl (por defecto, la tabla incluida en el paquete)",
    )
    return parser


def _check_outputs(xbe: Path, outputs: Sequence[Path]) -> None:
    """Ninguna salida puede ser el propio XBE: los datos del juego son inmutables."""
    source = xbe.resolve()
    for output in outputs:
        if output.resolve() == source:
            raise UsageError(f"la salida {output} es el archivo de entrada")


def _write_text(path: Path, text: str) -> None:
    """UTF-8 con fin de línea LF, creando los directorios intermedios."""
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
        print(f"validación: {issue}", file=sys.stderr)
    return ExitCode.VALIDATION if issues else ExitCode.OK


def run() -> int:
    """Entrada de consola (`ngb-xbe` y `python -m ngb_tools.xbe`).

    La consola de Windows no siempre es UTF-8: se fuerza para evitar mojibake en las
    tildes y en las rutas Shift-JIS del build. Va aquí y no en `main` para que los
    tests y quien importe `main` no vean mutado el `sys.stdout` global.
    """
    for stream in (sys.stdout, sys.stderr):
        if isinstance(stream, io.TextIOWrapper):
            stream.reconfigure(encoding="utf-8")
    return main()
