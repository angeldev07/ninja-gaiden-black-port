"""Checkpoints: named copies of single game saves, and sandboxes that load them.

A checkpoint is one save of the game (a `UDATA/<title>/<save>` folder) together with the
title's metadata files and its profile (`TDATA/<title>`), copied from a data folder into the
library, `$NGB_DATA/checkpoints/<name>/`:

    checkpoint.json              name, the save's in-game name, title id, note, source
    hdd/partition1/UDATA/<title>/{*.xbx, <save>/}
    hdd/partition1/TDATA/<title>/                    the profile (system.dat)

`prepare` turns it into a data folder of its own (`<name>/play/`): a link `game` to the game
folder and a fresh copy of `hdd` in `runs/hdd`, so the save the game finds is the only one and
whatever a run writes is thrown away next time. With one save, the load menu opens on it, and
`LOAD_SCRIPT` (gamepad polls) goes from the title screen to that save loaded.

The saves are the player's own data, not the game's; they stay outside the repo like the rest
of `NGB_DATA` (`ensure_outside_repos`).
"""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Final

from ngb_tools._paths import data_root, ensure_outside_repos

LIBRARY_SUBDIR: Final = "checkpoints"
MANIFEST: Final = "checkpoint.json"
PLAY_SUBDIR: Final = "play"
SAVE_FILE: Final = "save000.dat"
SAVE_META: Final = "SaveMeta.xbx"
_PARTITION: Final = ("runs", "hdd", "partition1")
_NAME_RE: Final = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}")

# Title screen to the load menu (START five times), A through the menu to the only save and its
# confirmation, then X: the story text shown while the chapter loads waits for "Proceed" (X) once it
# is done. The load takes 9,000 to 13,000 polls (the game reads the pad less often while loading),
# so X keeps coming until poll 16000; in gameplay it is only a light attack. No A once the save is
# loaded: Ryu stands at the save statue, where A opens its menu. Gamepad polls (`p`), the units of
# the loader's --input-script; scripted moves should start at GAMEPLAY_POLL.
LOAD_SCRIPT: Final = ",".join(
    [f"t={t}p:START" for t in (200, 350, 500, 650, 800)]
    + [f"t={t}p:A" for t in range(1000, 3001, 250)]
    + [f"t={t}p:X" for t in range(3500, 16001, 250)]
)
GAMEPLAY_POLL: Final = 16500


class CheckpointError(Exception):
    """A missing save, library entry or folder, or a bad name."""


@dataclass(frozen=True)
class Save:
    title: str  # title id folder, e.g. "5443000d"
    folder: Path  # the save's own folder (holds save000.dat)
    name: str  # the in-game name from SaveMeta.xbx, e.g. "12. CHAPTER 10 002:52"
    mtime: float


@dataclass(frozen=True)
class Checkpoint:
    name: str
    save_name: str
    title: str
    note: str
    path: Path


def library_root(root: Path | None = None) -> Path:
    return root if root is not None else data_root() / LIBRARY_SUBDIR


def _save_name(folder: Path) -> str:
    try:
        text = (folder / SAVE_META).read_bytes().decode("utf-16-le", "replace")
    except OSError:
        return ""
    match = re.search(r"Name=([^\r\n]*)", text)
    return match.group(1) if match else ""


def find_saves(data: Path) -> list[Save]:
    """The game saves in a data folder (`runs/hdd/partition1/UDATA`), newest first."""
    udata = data.joinpath(*_PARTITION, "UDATA")
    saves = []
    if udata.is_dir():
        for title in sorted(p for p in udata.iterdir() if p.is_dir()):
            for folder in sorted(p for p in title.iterdir() if p.is_dir()):
                save = folder / SAVE_FILE
                if save.is_file():
                    saves.append(Save(title.name, folder, _save_name(folder), save.stat().st_mtime))
    return sorted(saves, key=lambda s: s.mtime, reverse=True)


def _check_name(name: str) -> None:
    if not _NAME_RE.fullmatch(name):
        raise CheckpointError(f"bad checkpoint name {name!r}: letters, digits, '.', '_' and '-'")


def add(
    name: str,
    data: Path,
    *,
    save: str | None = None,
    note: str = "",
    library: Path | None = None,
    replace: bool = False,
) -> Checkpoint:
    """Copy one save of `data` (its folder name, or the newest) into the library as `name`."""
    _check_name(name)
    saves = find_saves(data)
    if save is not None:
        saves = [s for s in saves if s.folder.name.lower() == save.lower()]
    if not saves:
        raise CheckpointError(f"no save {save or ''} in {data}".replace("  ", " "))
    chosen = saves[0]
    dest = ensure_outside_repos(library_root(library) / name)
    if dest.exists():
        if not replace:
            raise CheckpointError(f"checkpoint {name} already exists (use --replace)")
        shutil.rmtree(dest)
    udata = dest.joinpath("hdd", "partition1", "UDATA", chosen.title)
    udata.mkdir(parents=True)
    for meta in chosen.folder.parent.glob("*.xbx"):
        shutil.copy2(meta, udata)
    shutil.copytree(chosen.folder, udata / chosen.folder.name)
    tdata = data.joinpath(*_PARTITION, "TDATA", chosen.title)
    if tdata.is_dir():
        shutil.copytree(tdata, dest.joinpath("hdd", "partition1", "TDATA", chosen.title))
    manifest = {
        "name": name,
        "save_name": chosen.name,
        "title": chosen.title,
        "save": chosen.folder.name,
        "note": note,
        "source": str(data),
    }
    (dest / MANIFEST).write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return Checkpoint(name, chosen.name, chosen.title, note, dest)


def list_checkpoints(library: Path | None = None) -> list[Checkpoint]:
    root = library_root(library)
    found = []
    if root.is_dir():
        for entry in sorted(root.iterdir()):
            try:
                manifest = json.loads((entry / MANIFEST).read_text(encoding="utf-8"))
            except (OSError, ValueError):
                continue
            found.append(
                Checkpoint(
                    entry.name,
                    str(manifest.get("save_name", "")),
                    str(manifest.get("title", "")),
                    str(manifest.get("note", "")),
                    entry,
                )
            )
    return found


def get(name: str, library: Path | None = None) -> Checkpoint:
    _check_name(name)
    for checkpoint in list_checkpoints(library):
        if checkpoint.name == name:
            return checkpoint
    raise CheckpointError(f"no checkpoint {name} in {library_root(library)}")


def _link_dir(link: Path, target: Path) -> None:
    """A directory link: a junction on Windows (no admin rights needed), a symlink elsewhere."""
    if os.name == "nt":
        subprocess.run(
            ["cmd", "/c", "mklink", "/J", str(link), str(target)], check=True, capture_output=True
        )
    else:
        link.symlink_to(target, target_is_directory=True)


def _remove_link(link: Path) -> None:
    """Removes the link only, never the game behind it."""
    if not (link.is_junction() or link.is_symlink()):
        raise CheckpointError(f"{link} is a real folder, not a link to a game")
    if os.name == "nt":
        link.rmdir()  # a junction: removes the link, not the folder it points to
    else:
        link.unlink()


def prepare(name: str, game: Path, *, library: Path | None = None) -> Path:
    """A data folder that holds only this checkpoint's save, with `game` linked in. Each call
    starts again from the checkpoint, so earlier runs leave nothing behind."""
    checkpoint = get(name, library)
    if not (game / "default.xbe").is_file():
        raise CheckpointError(f"no default.xbe in {game}")
    play = ensure_outside_repos(checkpoint.path / PLAY_SUBDIR)
    play.mkdir(exist_ok=True)
    link = play / "game"
    if os.path.lexists(link):
        _remove_link(link)
    _link_dir(link, game.resolve())
    runs = play / "runs"
    if runs.exists():
        shutil.rmtree(runs)
    shutil.copytree(checkpoint.path / "hdd", runs / "hdd")
    for leftover in (play / "achievements.json",):
        leftover.unlink(missing_ok=True)
    return play
