"""De dónde salen los personajes: los `chr_*.afs`, su KMD principal y su banco de motions.

- **KMD principal** de `chr_<nombre>.afs`: la entrada `<nombre>.kmd` y, si no la hay, el
  primer KMD por índice (`chr_heri` trae `heli.kmd`; `chr_arub`, `arub.kmd` en la #4). Hay
  AFS sin ningún KMD (`chr_dragn`, cuyo dragón vive en `chr_s_mur`, y `chr_e_ryu`).
- **Banco de motions**: se busca por nombre, `<nombre>.mtn` (o `.mot`), primero en el propio
  AFS del personaje, luego en `cmn.afs` y luego en los `*_cmn.afs`. Los trajes de Ryu
  (`ryua`…`ryug`) usan `ryu.mtn` de `cmn.afs`. Si el KMD tiene otro nombre, se prueba
  también ese.

Los AFS se abren con `ngb_tools.afs.archive`: solo se leen la cabecera y la tabla, y de cada
entrada, los bytes que se piden.
"""

from __future__ import annotations

import os
from collections.abc import Iterable, Sequence
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Final

from ngb_tools.afs.archive import AfsArchive, AfsEntry, open_afs, read_at
from ngb_tools.afs.errors import AfsTruncatedError

CHARACTER_PREFIX: Final = "chr_"
COMMON_ARCHIVE: Final = "cmn.afs"
COMMON_SUFFIX: Final = "_cmn.afs"
MOTION_SUFFIXES: Final = (".mtn", ".mot")
RYU_COSTUME_PREFIX: Final = "ryu"


@dataclass(frozen=True, slots=True)
class EntryRef:
    """Una entrada de un AFS: archivo, índice, nombre y extensión en bytes."""

    archive: Path
    index: int
    name: str
    offset: int
    size: int

    def read(self) -> bytes:
        with self.archive.open("rb") as handle:
            data = read_at(handle, self.offset, self.size)
        if len(data) != self.size:
            raise OSError(f"{self.archive.name}#{self.index}: entrada truncada")
        return data

    @property
    def stem(self) -> str:
        return PurePosixPath(self.name).stem

    @property
    def suffix(self) -> str:
        return PurePosixPath(self.name).suffix.lower()

    def __str__(self) -> str:
        return f"{self.archive.name}#{self.index} {self.name}"


def list_entries(path: Path) -> list[EntryRef]:
    """Entradas con nombre y datos de un AFS; si alguna no cabe en el archivo, está truncado.

    Sin esa comprobación, un AFS cortado pierde la tabla de nombres (va al final) y parece
    un AFS sin KMD.
    """
    with path.open("rb") as handle:
        archive: AfsArchive = open_afs(handle, path.name)
        size = handle.seek(0, os.SEEK_END)
    for entry in archive.data_entries():
        if entry.offset + entry.size > size:
            raise AfsTruncatedError(
                f"entrada {entry.index} acaba en {entry.offset + entry.size:#x}, "
                f"el archivo mide {size:#x}"
            )
    return [_ref(path, entry) for entry in archive.data_entries() if entry.name]


def _ref(path: Path, entry: AfsEntry) -> EntryRef:
    return EntryRef(path, entry.index, entry.name or "", entry.offset, entry.size)


def character_name(path: Path) -> str:
    """`chr_ryu.afs` -> `ryu`."""
    return path.stem.removeprefix(CHARACTER_PREFIX)


def character_archives(game_dir: Path) -> list[Path]:
    return sorted(game_dir.glob(f"{CHARACTER_PREFIX}*.afs"))


def common_archives(game_dir: Path) -> list[Path]:
    """`cmn.afs` primero y después los `*_cmn.afs`, por nombre."""
    common = [game_dir / COMMON_ARCHIVE] if (game_dir / COMMON_ARCHIVE).is_file() else []
    return common + sorted(p for p in game_dir.glob(f"*{COMMON_SUFFIX}") if p.is_file())


def main_kmd(entries: Sequence[EntryRef], name: str) -> EntryRef | None:
    """El KMD que se llama como el personaje o, si no, el primero por índice."""
    kmds = [e for e in entries if e.suffix == ".kmd"]
    named = [e for e in kmds if e.stem.lower() == name.lower()]
    if named:
        return named[0]
    return kmds[0] if kmds else None


def motion_candidates(name: str, kmd_stem: str | None) -> list[str]:
    """Nombres de banco a probar, en orden."""
    names = [name, kmd_stem or name]
    if name.startswith(RYU_COSTUME_PREFIX):
        names.append(RYU_COSTUME_PREFIX)
    return list(dict.fromkeys(n.lower() for n in names))


class MotionIndex:
    """Bancos de motions por nombre en los AFS comunes (se leen bajo demanda)."""

    def __init__(self, archives: Iterable[Path]) -> None:
        self._archives = list(archives)
        self._index: dict[str, EntryRef] | None = None

    def _build(self) -> dict[str, EntryRef]:
        index: dict[str, EntryRef] = {}
        for archive in self._archives:
            for entry in list_entries(archive):
                if entry.suffix in MOTION_SUFFIXES:
                    index.setdefault(entry.stem.lower(), entry)
        return index

    def find(self, own: Sequence[EntryRef], candidates: Sequence[str]) -> EntryRef | None:
        """El primer candidato en el propio AFS y, si no está, en los comunes."""
        local = {e.stem.lower(): e for e in own if e.suffix in MOTION_SUFFIXES}
        for name in candidates:
            if name in local:
                return local[name]
        if self._index is None:
            self._index = self._build()
        for name in candidates:
            if name in self._index:
                return self._index[name]
        return None
