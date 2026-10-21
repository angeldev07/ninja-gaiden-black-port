"""Las partes de una zona: los KMD `<zona>_NN` de sus AFS, sin repetir variantes idénticas.

Una zona (`boff`, `tln_a`...) son los AFS `<zona>_NN.afs` del disco más `<zona>_cmn.afs`
(VERIFIED: 17 zonas en `game/`; `hell`, `pyrmd` y `stdim` solo tienen `_cmn`). Cada
`<zona>_NN.afs` trae su escenario `<zona>_NN.kmd` (entrada 0, salvo `boff_05.afs`, que la
lleva en la 2) y a veces props. `<zona>_cmn.afs` trae los props comunes y otros KMD con
nombre de parte que también están en coordenadas de mundo (`boff_99.kmd`: cielo y fondo).

Una parte es toda entrada `<zona>_<dígitos>.kmd` de esos AFS. Los props (`bf_door1.kmd`...)
no entran: sus vértices están en local y su colocación vive en los scripts `.sty` (sin
descifrar).

**Variantes.** Algunas partes son el mismo archivo con otro número: `tomb_01` = `tomb_04`,
`tomb_02` = `tomb_05`, `tomb_03` = `tomb_06` y `tln_b_04` = `tln_b_05` (sha256 idéntico).
Se exporta la primera y las demás se anotan como duplicadas. Otras son variantes distintas
del mismo sitio (`tln_a_01` y `tln_a_11`) y se solapan si se piden juntas.
"""

from __future__ import annotations

import hashlib
import re
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Final

from ngb_tools.afs import AfsEntry, iter_entry_chunks, open_afs
from ngb_tools.stage.errors import ZoneNotFoundError

COMMON_SUFFIX: Final = "cmn"
ZONE_PATTERN: Final = re.compile(r"[a-z][a-z0-9]*(?:_[a-z][a-z0-9]*)*")
_NUMBER: Final = r"(\d{1,3})"


@dataclass(frozen=True, slots=True)
class ZonePart:
    """Un KMD de escenario dentro de un AFS de la zona."""

    zone: str
    number: str
    afs: Path
    entry: AfsEntry
    common: bool

    @property
    def name(self) -> str:
        return f"{self.zone}_{self.number}"


@dataclass(frozen=True, slots=True)
class LoadedPart:
    """Una parte con sus bytes; `duplicate_of` es la parte idéntica ya cargada, si la hay."""

    part: ZonePart
    data: bytes
    sha256: str
    duplicate_of: str | None = None


def check_zone_name(zone: str) -> str:
    """El nombre en minúsculas, o `ZoneNotFoundError` si no es un nombre de zona válido.

    Solo letras, dígitos y `_` entre palabras: el nombre acaba en rutas de salida, así que
    nada de separadores, `..` ni unidades.
    """
    name = zone.lower()
    if not ZONE_PATTERN.fullmatch(name):
        raise ZoneNotFoundError(f"nombre de zona inválido: {zone!r}")
    return name


def _zone_afs(game_dir: Path, zone: str) -> list[tuple[Path, bool]]:
    pattern = re.compile(rf"{re.escape(zone)}_(?:{_NUMBER}|{COMMON_SUFFIX})\.afs", re.IGNORECASE)
    found = []
    for path in sorted(game_dir.iterdir()):
        match = pattern.fullmatch(path.name)
        if match is not None and path.is_file():
            found.append((path, match.group(1) is None))
    return found


def find_zone_parts(game_dir: Path, zone: str) -> list[ZonePart]:
    """Las partes de la zona en `game_dir`, por número (las de `_cmn` detrás si empatan)."""
    name = check_zone_name(zone)
    archives = _zone_afs(game_dir, name)
    if not archives:
        raise ZoneNotFoundError(f"no hay AFS de la zona '{name}' en {game_dir}")
    pattern = re.compile(rf"{re.escape(name)}_{_NUMBER}\.kmd", re.IGNORECASE)
    parts: list[ZonePart] = []
    for path, common in archives:
        with path.open("rb") as handle:
            archive = open_afs(handle, path.name)
        for entry in archive.data_entries():
            match = pattern.fullmatch(entry.name or "")
            if match is not None:
                parts.append(ZonePart(name, match.group(1), path, entry, common))
    if not parts:
        raise ZoneNotFoundError(f"los AFS de '{name}' no traen ningún {name}_NN.kmd")
    return sorted(parts, key=lambda p: (int(p.number), p.common, p.afs.name, p.entry.index))


def select_parts(parts: Sequence[ZonePart], wanted: Sequence[str] | None) -> list[ZonePart]:
    """Las partes pedidas por número (`01`, `1` o `boff_01`); `None` = todas."""
    if wanted is None:
        return list(parts)
    chosen: list[ZonePart] = []
    for item in wanted:
        key = item.lower().rsplit("_", 1)[-1]
        if not key.isdigit():
            raise ZoneNotFoundError(f"parte inválida: {item!r} (se espera un número, p. ej. 01)")
        matches = [p for p in parts if int(p.number) == int(key) and p not in chosen]
        if not matches:
            available = ", ".join(p.number for p in parts)
            raise ZoneNotFoundError(f"no hay parte {item!r}; hay {available}")
        chosen += matches
    return chosen


def read_part(part: ZonePart) -> bytes:
    with part.afs.open("rb") as handle:
        return b"".join(iter_entry_chunks(handle, part.entry))


def load_parts(parts: Sequence[ZonePart]) -> list[LoadedPart]:
    """Lee cada parte y marca las que repiten byte a byte una anterior."""
    seen: dict[str, str] = {}
    loaded: list[LoadedPart] = []
    for part in parts:
        data = read_part(part)
        digest = hashlib.sha256(data).hexdigest()
        loaded.append(LoadedPart(part, data, digest, seen.get(digest)))
        seen.setdefault(digest, part.name)
    return loaded
