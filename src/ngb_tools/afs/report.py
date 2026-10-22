"""Text presentation: the `list` listing and the inventory as Markdown or JSON.

It only formats; the data is already computed by `archive` and `inventory`.
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
NONE_ITEM: Final = ("- none",)
_CONTROL_CHARS: Final = re.compile(r"[\x00-\x1f\x7f]")


def printable(text: str) -> str:
    """Escape control characters (0x00-0x1f and 0x7f) as `\\xNN`.

    The same as `backslashreplace` does with non-ASCII bytes: a game name cannot move the
    console cursor or split a report line.
    """
    return _CONTROL_CHARS.sub(lambda m: f"\\x{ord(m.group()):02x}", text)


def format_listing(archive: AfsArchive) -> list[str]:
    """`list` lines: file summary, column header and one row per entry."""
    ptr = archive.attr_ptr
    where = "" if ptr.location is AttrLocation.NONE else f" @{ptr.offset:#x} ({ptr.size:#x} B)"
    lines = [
        f"{printable(archive.label)}: {archive.limit} bytes, magic {archive.magic!r}, "
        f"{archive.count} entries, attributes={ptr.location}{where}",
        f"{'idx':>5}  {'offset':>10}  {'size':>10}  {'date':19}  name",
    ]
    for e in archive.entries:
        stamp = (e.attr.timestamp if e.attr is not None else None) or "-"
        name = printable(e.name or "-")
        lines.append(f"{e.index:5d}  {e.rel_offset:#010x}  {e.size:#010x}  {stamp:19}  {name}")
    return lines


def render_json(inventory: InventoryRecord) -> str:
    """Indented JSON, readable UTF-8 (accents not escaped)."""
    return json.dumps(inventory, indent=1, ensure_ascii=False) + "\n"


def _mb(n: int) -> str:
    return f"{n / (1 << 20):.2f}"


def _counts(counter: Mapping[str, int]) -> str:
    return ", ".join(f"{k} {v}" for k, v in counter.items())


def _cell(text: str) -> str:
    """Text that is safe in a Markdown cell: no control characters and the pipe escaped."""
    return printable(text).replace("|", "\\|")


def _header(inv: InventoryRecord) -> list[str]:
    return [
        "# AFS inventory",
        "",
        f"- Generated: {inv['generated']} with `{inv['tool']}`; root `{printable(inv['root'])}`.",
        f"- Entropy over the first {inv['entropy_window'] // 1024} KB of each entry.",
    ]


def _totals_section(s: SummaryRecord) -> list[str]:
    t: TotalsRecord = s["totals"]
    return [
        "## Totals",
        "",
        "| metric | value |",
        "|---|---|",
        f"| AFS files | {t['archives']} |",
        f"| AFS files with a name table | {t['archives_with_names']} |",
        f"| entries (level 0) | {t['entries']} |",
        f"| entries (all levels) | {t['entries_all_levels']} |",
        f"| null entries | {t['null_entries']} |",
        f"| named entries | {t['named_entries']} ({t['pct_named']} %) |",
        f"| bytes in AFS files | {t['archive_bytes']} ({_mb(t['archive_bytes'])} MB) |",
        f"| bytes in entries | {t['entry_bytes']} ({_mb(t['entry_bytes'])} MB) |",
        f"| attribute pointer location | {_counts(s['attr_pointer_locations'])} |",
        f"| attribute custom field | {_counts(s['custom_field'])} |",
    ]


def _histogram_section(s: SummaryRecord) -> list[str]:
    lines = [
        f"## Type histogram (top {TOP_KINDS})",
        "",
        "| # | type | description | entries | MB | mean H | bands | name ext. | examples |",
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
        "## Entropy",
        "",
        f"Bands: low < {LOW_ENTROPY_BELOW:g}, mid {LOW_ENTROPY_BELOW:g}-{HIGH_ENTROPY_ABOVE:g}, "
        f"high > {HIGH_ENTROPY_ABOVE:g} (possible compression/encryption).",
        "",
        "| band | entries | MB |",
        "|---|---|---|",
    ]
    lines += [
        f"| {b} | {v['entries']} | {_mb(v['bytes'])} |" for b, v in s["entropy_bands"].items()
    ]
    mostly_high = [k for k in s["magic_histogram"] if k["bands"].get("high", 0) * 2 > k["entries"]]
    lines += ["", "Types with most entries in the high band:", ""]
    lines += [
        f"- `{k['kind']}`: {k['bands'].get('high', 0)}/{k['entries']} (mean H {k['entropy_mean']})"
        for k in mostly_high
    ] or NONE_ITEM
    return lines


def _nested_section(s: SummaryRecord) -> list[str]:
    items = []
    for n in s["nested_afs"]:
        name = f" {printable(n['name'])}" if n["name"] else ""
        error = f" ({printable(n['error'])})" if n["error"] else ""
        items.append(f"- `{printable(n['ref'])}`{name}: {n['count']} entries{error}")
    return ["## Nested AFS files", "", *(items or NONE_ITEM)]


def _extensions_section(s: SummaryRecord) -> list[str]:
    lines = [
        "## Name extensions",
        "",
        "| ext | entries | MB | mean H | types |",
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
        "## Invariants",
        "",
        f"- AFS files without issues: {inv['archives_ok']}/{s['totals']['archives']}.",
    ]
    lines += [
        f"- `{code}`: {n}. E.g.: " + "; ".join(map(printable, inv["issue_examples"][code]))
        for code, n in inv["issue_counts"].items()
    ]
    return lines


def _files_section(inv: InventoryRecord) -> list[str]:
    lines = [
        "## Files",
        "",
        "| file | MB | entries | names | attr pointer | issues |",
        "|---|---|---|---|---|---|",
    ]
    lines += [
        f"| {_cell(a['label'])} | {_mb(a['size'])} | {a['count']} | "
        f"{'yes' if a['has_names'] else 'no'} | {a['attr']['location']} | {len(a['issues'])} |"
        for a in inv["archives"]
    ]
    return lines


def render_markdown(inventory: InventoryRecord) -> str:
    """Readable inventory report, section by section."""
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
