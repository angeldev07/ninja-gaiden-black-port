"""Informes de la galería: `gallery.md` (tabla para leer) y `gallery.json` (para máquinas).

Viven junto a los GLB, en `$NGB_DATA` (fuera de cualquier repo). La tabla resume cada
personaje: KMD, objetos y triángulos exportados, motion del idle o por qué no la hay,
estado y motivo del fallo. Debajo, los fallos agrupados por causa.
"""

from __future__ import annotations

import json
from collections import defaultdict
from collections.abc import Sequence
from dataclasses import asdict

from ngb_tools.gallery.build import CharacterResult, Status


def _cell(value: object) -> str:
    text = "" if value is None else str(value)
    return text.replace("|", "\\|").replace("\n", " ")


def failure_cause(reason: str | None) -> str:
    """La causa sin el detalle: lo que va antes del primer `:`."""
    return (reason or "desconocida").split(":")[0].strip()


def summary(results: Sequence[CharacterResult]) -> tuple[int, int, int]:
    """(personajes, exportados, con animación)."""
    ok = [r for r in results if r.entry.status is Status.OK]
    return len(results), len(ok), sum(1 for r in ok if r.entry.motion)


def markdown(results: Sequence[CharacterResult]) -> str:
    total, ok, animated = summary(results)
    lines = [
        "# Galería de personajes",
        "",
        f"{ok} de {total} exportados a GLB, {animated} con idle animado. "
        "Generada por `python -m ngb_tools.gallery build`.",
        "",
        "| chr | KMD | objetos | triángulos | motion | estado | motivo |",
        "|---|---|---:|---:|---|---|---|",
    ]
    for result in results:
        e = result.entry
        motion = e.motion or (f"no ({e.motion_note})" if e.status is Status.OK else "")
        row = (e.archive, e.kmd, e.objects, e.triangles, motion, e.status.value, e.reason)
        lines.append("| " + " | ".join(_cell(v) for v in row) + " |")
    groups: dict[str, list[str]] = defaultdict(list)
    for result in results:
        if result.entry.status is Status.FAILED:
            groups[failure_cause(result.entry.reason)].append(result.entry.character)
    if groups:
        lines += ["", "## Fallos por causa", ""]
        for cause, names in sorted(groups.items(), key=lambda item: -len(item[1])):
            lines.append(f"- **{cause}** ({len(names)}): {', '.join(names)}")
    return "\n".join(lines) + "\n"


def manifest(results: Sequence[CharacterResult]) -> str:
    total, ok, animated = summary(results)
    rows = [{**asdict(r.entry), "status": r.entry.status.value} for r in results]
    doc = {"characters": total, "exported": ok, "animated": animated, "entries": rows}
    return json.dumps(doc, indent=2, ensure_ascii=False) + "\n"
