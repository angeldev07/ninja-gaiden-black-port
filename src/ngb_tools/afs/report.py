"""Presentación en texto: el listado de `list` y el inventario en Markdown o JSON.

Solo da formato; los datos ya vienen calculados por `archive` e `inventory`.
"""

from __future__ import annotations

import json
import re
from collections.abc import Mapping
from typing import Final

from ngb_tools.afs.archive import AfsArchive, AttrLocation
from ngb_tools.afs.classify import HIGH_ENTROPY_ABOVE, LOW_ENTROPY_BELOW
from ngb_tools.afs.records import InventoryRecord, SummaryRecord, TotalsRecord

TOP_KINDS: Final = 40
NONE_ITEM: Final = ("- ninguno",)
_CONTROL_CHARS: Final = re.compile(r"[\x00-\x1f\x7f]")


def printable(text: str) -> str:
    """Escapa los caracteres de control (0x00-0x1f y 0x7f) como `\\xNN`.

    Igual que hace `backslashreplace` con los bytes no ASCII: un nombre del juego no puede
    mover el cursor de la consola ni partir una línea del informe.
    """
    return _CONTROL_CHARS.sub(lambda m: f"\\x{ord(m.group()):02x}", text)


def format_listing(archive: AfsArchive) -> list[str]:
    """Líneas de `list`: resumen del archivo, cabecera de columnas y una fila por entrada."""
    ptr = archive.attr_ptr
    where = "" if ptr.location is AttrLocation.NONE else f" @{ptr.offset:#x} ({ptr.size:#x} B)"
    lines = [
        f"{printable(archive.label)}: {archive.limit} bytes, magia {archive.magic!r}, "
        f"{archive.count} entradas, atributos={ptr.location}{where}",
        f"{'idx':>5}  {'offset':>10}  {'size':>10}  {'fecha':19}  nombre",
    ]
    for e in archive.entries:
        stamp = (e.attr.timestamp if e.attr is not None else None) or "-"
        name = printable(e.name or "-")
        lines.append(f"{e.index:5d}  {e.rel_offset:#010x}  {e.size:#010x}  {stamp:19}  {name}")
    return lines


def render_json(inventory: InventoryRecord) -> str:
    """JSON indentado y en UTF-8 legible (sin escapar tildes)."""
    return json.dumps(inventory, indent=1, ensure_ascii=False) + "\n"


def _mb(n: int) -> str:
    return f"{n / (1 << 20):.2f}"


def _counts(counter: Mapping[str, int]) -> str:
    return ", ".join(f"{k} {v}" for k, v in counter.items())


def _cell(text: str) -> str:
    """Texto seguro en una celda Markdown: sin controles y con la barra vertical escapada."""
    return printable(text).replace("|", "\\|")


def _header(inv: InventoryRecord) -> list[str]:
    return [
        "# Inventario AFS",
        "",
        f"- Generado: {inv['generated']} con `{inv['tool']}`; raíz `{printable(inv['root'])}`.",
        f"- Entropía sobre los primeros {inv['entropy_window'] // 1024} KB de cada entrada.",
    ]


def _totals_section(s: SummaryRecord) -> list[str]:
    t: TotalsRecord = s["totals"]
    return [
        "## Totales",
        "",
        "| métrica | valor |",
        "|---|---|",
        f"| archivos AFS | {t['archives']} |",
        f"| AFS con tabla de nombres | {t['archives_with_names']} |",
        f"| entradas (nivel 0) | {t['entries']} |",
        f"| entradas (todos los niveles) | {t['entries_all_levels']} |",
        f"| entradas nulas | {t['null_entries']} |",
        f"| entradas con nombre | {t['named_entries']} ({t['pct_named']} %) |",
        f"| bytes en archivos AFS | {t['archive_bytes']} ({_mb(t['archive_bytes'])} MB) |",
        f"| bytes en entradas | {t['entry_bytes']} ({_mb(t['entry_bytes'])} MB) |",
        f"| ubicación del puntero de atributos | {_counts(s['attr_pointer_locations'])} |",
        f"| campo custom de atributos | {_counts(s['custom_field'])} |",
    ]


def _histogram_section(s: SummaryRecord) -> list[str]:
    lines = [
        f"## Histograma de tipos (top {TOP_KINDS})",
        "",
        "| # | tipo | descripción | entradas | MB | H media | bandas | ext. nombres | ejemplos |",
        "|---|---|---|---|---|---|---|---|---|",
    ]
    for rank, k in enumerate(s["magic_histogram"][:TOP_KINDS], 1):
        examples = "<br>".join(
            f"`{_cell(x['ref'])}` {_cell(x['name'] or '')}" for x in k["examples"]
        )
        lines.append(
            f"| {rank} | `{_cell(k['kind'])}` | {k['description']} | {k['entries']} | "
            f"{_mb(k['bytes'])} | {k['entropy_mean']} | {_counts(k['bands'])} | "
            f"{_cell(_counts(k['exts']))} | {examples} |"
        )
    return lines


def _entropy_section(s: SummaryRecord) -> list[str]:
    lines = [
        "## Entropía",
        "",
        f"Bandas: low < {LOW_ENTROPY_BELOW:g}, mid {LOW_ENTROPY_BELOW:g}-{HIGH_ENTROPY_ABOVE:g}, "
        f"high > {HIGH_ENTROPY_ABOVE:g} (posible compresión/cifrado).",
        "",
        "| banda | entradas | MB |",
        "|---|---|---|",
    ]
    lines += [
        f"| {b} | {v['entries']} | {_mb(v['bytes'])} |" for b, v in s["entropy_bands"].items()
    ]
    mostly_high = [k for k in s["magic_histogram"] if k["bands"].get("high", 0) * 2 > k["entries"]]
    lines += ["", "Tipos con mayoría de entradas en banda alta:", ""]
    lines += [
        f"- `{k['kind']}`: {k['bands'].get('high', 0)}/{k['entries']} (H media {k['entropy_mean']})"
        for k in mostly_high
    ] or NONE_ITEM
    return lines


def _nested_section(s: SummaryRecord) -> list[str]:
    items = []
    for n in s["nested_afs"]:
        name = f" {printable(n['name'])}" if n["name"] else ""
        error = f" ({printable(n['error'])})" if n["error"] else ""
        items.append(f"- `{printable(n['ref'])}`{name}: {n['count']} entradas{error}")
    return ["## AFS anidados", "", *(items or NONE_ITEM)]


def _extensions_section(s: SummaryRecord) -> list[str]:
    lines = [
        "## Extensiones de nombres",
        "",
        "| ext | entradas | MB | H media | tipos |",
        "|---|---|---|---|---|",
    ]
    lines += [
        f"| `{_cell(x['ext'])}` | {x['entries']} | {_mb(x['bytes'])} | {x['entropy_mean']} | "
        f"{_cell(_counts(x['kinds']))} |"
        for x in s["name_extensions"]
    ]
    return lines


def _invariants_section(s: SummaryRecord) -> list[str]:
    inv = s["invariants"]
    lines = [
        "## Invariantes",
        "",
        f"- AFS sin incidencias: {inv['archives_ok']}/{s['totals']['archives']}.",
    ]
    lines += [
        f"- `{code}`: {n}. Ej.: " + "; ".join(map(printable, inv["issue_examples"][code]))
        for code, n in inv["issue_counts"].items()
    ]
    return lines


def _files_section(inv: InventoryRecord) -> list[str]:
    lines = [
        "## Archivos",
        "",
        "| archivo | MB | entradas | nombres | puntero attr | incidencias |",
        "|---|---|---|---|---|---|",
    ]
    lines += [
        f"| {_cell(a['label'])} | {_mb(a['size'])} | {a['count']} | "
        f"{'sí' if a['has_names'] else 'no'} | {a['attr']['location']} | {len(a['issues'])} |"
        for a in inv["archives"]
    ]
    return lines


def render_markdown(inventory: InventoryRecord) -> str:
    """Informe legible del inventario, sección a sección."""
    s = inventory["summary"]
    sections = (
        _header(inventory),
        _totals_section(s),
        _histogram_section(s),
        _entropy_section(s),
        _nested_section(s),
        _extensions_section(s),
        _invariants_section(s),
        _files_section(inventory),
    )
    return "\n\n".join("\n".join(lines) for lines in sections) + "\n"
