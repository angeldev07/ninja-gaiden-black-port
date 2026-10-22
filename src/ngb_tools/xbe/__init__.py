"""Inspector for XBE executables (original Xbox).

Typical flow::

    image = XbeImage.load(path)                 # header, sections, XOR keys
    analysis = analyze(image, with_strings=True)
    render_json(analysis), render_markdown(analysis)

Modules, from lowest to highest level:

- `binary`: little-endian integers and text, with bounds checks.
- `structs`: every structure of the format, parsed from a file offset.
- `image`: `XbeImage`, VA -> file map, XOR keys, digests and validation.
- `kernel`: xboxkrnl export table and import thunk table.
- `strings`: extraction and heuristic classification of strings.
- `analysis`: follows the header pointers and gathers everything into `XbeAnalysis`.
- `report` / `markdown`: JSON (TypedDict) and Markdown reports.
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
