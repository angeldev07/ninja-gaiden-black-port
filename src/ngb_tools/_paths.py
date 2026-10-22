"""Shared tool paths: game data, protected repos and atomic writes.

The game data is copyrighted: it lives in `NGB_DATA` (default `~/ngb-data`, with an
immutable `game/` and `extracted/`) and must never end up inside a git repo. Every tool
that writes something derived from the game passes its output path through
`ensure_outside_repos` and writes with `atomic_write_*`, so a file with its final name is
always complete.

A repo is recognized by its `.git`: a folder (normal clone) or a file starting with
`gitdir:` (worktree or submodule). Any other `.git` does not count.
"""

from __future__ import annotations

import os
from collections.abc import Iterable, Mapping, Sequence
from pathlib import Path
from typing import Final

DATA_ENV_VAR: Final = "NGB_DATA"
DEFAULT_DATA_DIRNAME: Final = "ngb-data"
GAME_SUBDIR: Final = "game"
EXTRACTED_SUBDIR: Final = "extracted"
BUILD_SUBDIR: Final = "build"
PARTIAL_SUFFIX: Final = ".part"
_GITDIR_PREFIX: Final = b"gitdir:"


class UnsafeOutputError(Exception):
    """Writing inside a git repository (outside the allowed folders) was requested."""


def data_root(environ: Mapping[str, str] | None = None) -> Path:
    """Game data root: `$NGB_DATA`, or `~/ngb-data` if it is unset or empty.

    If `~` is itself a git repo (dotfiles), `~/ngb-data` falls inside it and the guard
    will refuse to write there: in that case `NGB_DATA` must point outside the repo.
    """
    env = os.environ if environ is None else environ
    value = env.get(DATA_ENV_VAR)
    return Path(value) if value else Path.home() / DEFAULT_DATA_DIRNAME


def is_repo_marker(marker: Path) -> bool:
    """Whether `marker` (a `.git`) betrays a repo: a folder, or a `gitdir: ...` file.

    A `.git` that exists but cannot be read counts as a repo: when in doubt, protect.
    """
    try:
        if marker.is_dir():
            return True
        if not marker.is_file():
            return False
        with marker.open("rb") as handle:
            return handle.read(len(_GITDIR_PREFIX)) == _GITDIR_PREFIX
    except OSError:
        return True


def find_repo_roots(start: Path) -> tuple[Path, ...]:
    """All the repos that contain `start`, from the nearest to the farthest.

    `start` does not need to exist, so an output can be checked before it is created.
    """
    return tuple(c for c in (start, *start.parents) if is_repo_marker(c / ".git"))


def find_repo_root(start: Path) -> Path | None:
    """The nearest repo that contains `start`, or None."""
    return next((c for c in (start, *start.parents) if is_repo_marker(c / ".git")), None)


def package_repo_root() -> Path | None:
    """Repo that contains the `ngb_tools` code, or None if the package is installed elsewhere."""
    return find_repo_root(Path(__file__).resolve().parent)


def protected_roots() -> tuple[Path, ...]:
    """Repos protected even if the output is not under them: the package's and the cwd's."""
    candidates = (package_repo_root(), find_repo_root(Path.cwd().resolve()))
    return tuple(sorted({root for root in candidates if root is not None}))


def report_dirs() -> tuple[Path, ...]:
    """Repo folders where reports may be written: the package repo's `build/`.

    `build/` is in .gitignore and the hygiene guard blocks it even if someone forces a
    `git add -f`, so whatever is written there cannot end up versioned.
    """
    root = package_repo_root()
    return () if root is None else (root / BUILD_SUBDIR,)


def _is_exempt(resolved: Path, repos: Sequence[Path], allow: Iterable[Path]) -> bool:
    """The `allow` exception only holds inside the package repo.

    The repo nearest to the already-resolved path must be the package's (not a clone nested
    in build/, nor another repo that a symlink or junction points to) and the allowed
    folder, also resolved, must be a subfolder of that repo. The allowed folder cannot be a
    link: if `build/` were a junction to `src/` of the same repo, the resolved path would
    still be inside the package repo but outside the ignored area.
    """
    package = package_repo_root()
    if package is None or not repos or repos[0] != package:
        return False
    for exempt in allow:
        target = exempt.resolve()
        is_link = target != exempt.parent.resolve() / exempt.name
        if is_link or target == package or not target.is_relative_to(package):
            continue
        if resolved.is_relative_to(target):
            return True
    return False


def ensure_outside_repos(path: Path, *, allow: Iterable[Path] = ()) -> Path:
    """Return `path` resolved, or raise `UnsafeOutputError` if it falls inside a git repo.

    `resolve()` follows symlinks and junctions and normalizes `..` even if the path does not
    exist. The path is rejected if the resolved path is under any of the repos that contain
    it (all ancestors with `.git`, not just the nearest) or under `protected_roots()`. The
    only exception is `allow`, under the conditions of `_is_exempt`.
    """
    resolved = path.resolve()
    repos = find_repo_roots(resolved)
    if _is_exempt(resolved, repos, allow):
        return resolved
    for repo in (*repos, *protected_roots()):
        if resolved.is_relative_to(repo):
            raise UnsafeOutputError(f"output inside repo {repo} is forbidden: {resolved}")
    return resolved


def partial_path(path: Path) -> Path:
    """Temporary name (`<name>.part`) used while `path` is being written."""
    return path.with_name(path.name + PARTIAL_SUFFIX)


def _fsync_dir(directory: Path) -> None:
    """On POSIX a rename is only durable after an fsync of the folder (Windows lacks it)."""
    if os.name != "posix":
        return
    fd = os.open(directory, os.O_RDONLY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def atomic_write_chunks(path: Path, chunks: Iterable[bytes], *, fsync: bool = False) -> None:
    """Write `chunks` to `path` without ever leaving a half-written file under the final name.

    Atomic against process failures (an exception, a kill): either the complete file or the
    previous one remains. Durable against power cuts only with `fsync=True`.

    Deletes a `.part` left by an earlier interruption (if it is a symlink, the link is
    deleted, not its target), creates it in exclusive mode ("xb"), writes, calls `fsync` if
    requested and renames it with `os.replace` (via `Path.replace`). If anything fails, the
    `.part` is deleted only if this call created it: if "xb" fails because another writer
    recreated it, it is left alone. The destination folder must exist. Not meant for several
    simultaneous writers on the same path: the initial deletion of the `.part` assumes a
    single writer (the tools are single-process).
    """
    partial = partial_path(path)
    partial.unlink(missing_ok=True)
    owned = False
    try:
        with partial.open("xb") as out:
            owned = True
            for chunk in chunks:
                out.write(chunk)
            if fsync:
                out.flush()
                os.fsync(out.fileno())
        partial.replace(path)  # os.replace: atomic on the same volume
        owned = False
        if fsync:
            _fsync_dir(path.parent)
    finally:
        if owned:
            partial.unlink(missing_ok=True)


def atomic_write_bytes(path: Path, data: bytes, *, fsync: bool = False) -> None:
    """`atomic_write_chunks` with a single block."""
    atomic_write_chunks(path, (data,), fsync=fsync)
