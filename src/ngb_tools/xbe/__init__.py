"""Inspector de ejecutables XBE (Xbox original).

Flujo típico::

    image = XbeImage.load(path)                 # cabecera, secciones, claves XOR
    analysis = analyze(image, with_strings=True)
    render_json(analysis), render_markdown(analysis)

Módulos, de lo más bajo a lo más alto:

- `binary`: enteros little-endian y texto, con límites.
- `structs`: cada estructura del formato, parseada desde un offset de archivo.
- `image`: `XbeImage`, mapa VA -> archivo, claves XOR, digests y validación.
- `kernel`: tabla de exports de xboxkrnl y thunk de imports.
- `strings`: extracción y clasificación heurística de cadenas.
- `analysis`: sigue los punteros de la cabecera y reúne todo en `XbeAnalysis`.
- `report` / `markdown`: informes JSON (TypedDict) y Markdown.
- `cli`: `python -m ngb_tools.xbe`.
"""

from ngb_tools.xbe.analysis import XbeAnalysis, analyze
from ngb_tools.xbe.errors import (
    BadMagicError,
    KernelExportsError,
    ThunkTableError,
    TruncatedDataError,
    UnmappedAddressError,
    XbeError,
    XbeFormatError,
)
from ngb_tools.xbe.image import XbeImage
from ngb_tools.xbe.kernel import load_kernel_exports
from ngb_tools.xbe.markdown import render_markdown
from ngb_tools.xbe.report import build_report, render_json

__all__ = [
    "BadMagicError",
    "KernelExportsError",
    "ThunkTableError",
    "TruncatedDataError",
    "UnmappedAddressError",
    "XbeAnalysis",
    "XbeError",
    "XbeFormatError",
    "XbeImage",
    "analyze",
    "build_report",
    "load_kernel_exports",
    "render_json",
    "render_markdown",
]
