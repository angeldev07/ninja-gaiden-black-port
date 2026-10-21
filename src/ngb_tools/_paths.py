"""Rutas compartidas por las herramientas: datos del juego, repos protegidos y escritura atómica.

Los datos del juego tienen copyright: viven en `NGB_DATA` (por defecto `~/ngb-data`, con
`game/` inmutable y `extracted/`) y nunca deben caer dentro de un repo git. Toda
herramienta que escriba algo derivado del juego pasa su ruta de salida por
`ensure_outside_repos` y escribe con `atomic_write_*`, así que un archivo con su nombre
final siempre está completo.

Un repo se reconoce por su `.git`: una carpeta (clon normal) o un archivo que empieza por
`gitdir:` (worktree o submódulo). Cualquier otro `.git` no cuenta.
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
    """Se pidió escribir dentro de un repositorio git (fuera de las carpetas permitidas)."""


def data_root(environ: Mapping[str, str] | None = None) -> Path:
    """Raíz de los datos del juego: `$NGB_DATA`, o `~/ngb-data` si no está definida o vacía.

    Si `~` es a su vez un repo git (dotfiles), `~/ngb-data` cae dentro de él y la guarda
    rechazará escribir ahí: en ese caso `NGB_DATA` tiene que apuntar fuera del repo.
    """
    env = os.environ if environ is None else environ
    value = env.get(DATA_ENV_VAR)
    return Path(value) if value else Path.home() / DEFAULT_DATA_DIRNAME


def is_repo_marker(marker: Path) -> bool:
    """Indica si `marker` (un `.git`) delata un repo: carpeta, o archivo `gitdir: ...`.

    Un `.git` que existe pero no se puede leer cuenta como repo: ante la duda, se protege.
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
    """Todos los repos que contienen `start`, del más cercano al más lejano.

    `start` no tiene por qué existir: así se puede comprobar una salida antes de crearla.
    """
    return tuple(c for c in (start, *start.parents) if is_repo_marker(c / ".git"))


def find_repo_root(start: Path) -> Path | None:
    """El repo más cercano que contiene `start`, o None."""
    return next((c for c in (start, *start.parents) if is_repo_marker(c / ".git")), None)


def package_repo_root() -> Path | None:
    """Repo que contiene el código de `ngb_tools`, o None si el paquete está instalado aparte."""
    return find_repo_root(Path(__file__).resolve().parent)


def protected_roots() -> tuple[Path, ...]:
    """Repos que se protegen aunque la salida no esté bajo ellos: el del paquete y el del cwd."""
    candidates = (package_repo_root(), find_repo_root(Path.cwd().resolve()))
    return tuple(sorted({root for root in candidates if root is not None}))


def report_dirs() -> tuple[Path, ...]:
    """Carpetas del repo donde sí se pueden escribir informes: `build/` del repo del paquete.

    `build/` está en .gitignore y la guardia de higiene la bloquea aunque se fuerce un
    `git add -f`, así que lo que se escribe ahí no puede acabar versionado.
    """
    root = package_repo_root()
    return () if root is None else (root / BUILD_SUBDIR,)


def _is_exempt(resolved: Path, repos: Sequence[Path], allow: Iterable[Path]) -> bool:
    """La excepción de `allow` solo vale dentro del repo del paquete.

    Hace falta que el repo más cercano a la ruta ya resuelta sea el del paquete (no un clon
    anidado en build/, ni otro repo al que apunte un symlink o una unión) y que la carpeta
    permitida, también resuelta, sea una subcarpeta de ese repo. La carpeta permitida no
    puede ser un enlace: si `build/` fuera una unión hacia `src/` del mismo repo, la ruta
    resuelta seguiría dentro del repo del paquete pero fuera de la zona ignorada.
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
    """Devuelve `path` resuelto, o lanza `UnsafeOutputError` si cae dentro de un repo git.

    `resolve()` sigue symlinks y uniones y normaliza `..` aunque la ruta no exista. Se
    rechaza si la ruta resuelta está bajo cualquiera de los repos que la contienen (todos
    los ancestros con `.git`, no solo el más cercano) o bajo `protected_roots()`. La única
    excepción es `allow`, con las condiciones de `_is_exempt`.
    """
    resolved = path.resolve()
    repos = find_repo_roots(resolved)
    if _is_exempt(resolved, repos, allow):
        return resolved
    for repo in (*repos, *protected_roots()):
        if resolved.is_relative_to(repo):
            raise UnsafeOutputError(f"salida dentro del repo {repo} prohibida: {resolved}")
    return resolved


def partial_path(path: Path) -> Path:
    """Nombre temporal (`<nombre>.part`) mientras se escribe `path`."""
    return path.with_name(path.name + PARTIAL_SUFFIX)


def _fsync_dir(directory: Path) -> None:
    """En POSIX el renombrado solo es durable tras un fsync de la carpeta (Windows no lo admite)."""
    if os.name != "posix":
        return
    fd = os.open(directory, os.O_RDONLY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def atomic_write_chunks(path: Path, chunks: Iterable[bytes], *, fsync: bool = False) -> None:
    """Escribe `chunks` en `path` sin dejar nunca un archivo a medias con el nombre final.

    Atómico frente a fallos del proceso (una excepción, un kill): queda el archivo completo o
    el anterior. Durable ante cortes de luz solo con `fsync=True`.

    Borra un `.part` que haya dejado un corte previo (si es un symlink, se borra el enlace,
    no su destino), lo crea en modo exclusivo ("xb"), escribe, hace `fsync` si se pide y lo
    renombra con `os.replace` (vía `Path.replace`). Si algo falla, el `.part` se borra solo
    si lo creó esta llamada: si "xb" falla porque otro escritor lo recreó, no se toca. La
    carpeta de destino debe existir. No está pensado para varios escritores simultáneos sobre
    la misma ruta: el borrado inicial del `.part` asume un único escritor (las herramientas
    son de un solo proceso).
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
        partial.replace(path)  # os.replace: atómico en el mismo volumen
        owned = False
        if fsync:
            _fsync_dir(path.parent)
    finally:
        if owned:
            partial.unlink(missing_ok=True)


def atomic_write_bytes(path: Path, data: bytes, *, fsync: bool = False) -> None:
    """`atomic_write_chunks` con un único bloque."""
    atomic_write_chunks(path, (data,), fsync=fsync)
