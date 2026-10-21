"""Guardia de higiene del repo: impide versionar datos del juego.

Lee SIEMPRE del índice de git, nunca del árbol de trabajo: lo que se juzga es el blob que
entraría en el commit. Así no hay bypass (preparar un blob del juego y luego sobrescribir el
archivo en disco) ni fallos por archivos borrados o symlinks colgantes. Los gitlinks
(submódulos) se saltan; un symlink es un blob con la ruta destino y se evalúa como cualquiera.
git corre sin objetos de reemplazo (un `refs/replace/*` no puede disfrazar un blob), sin
descargas perezosas (un objeto ausente es un error, no una espera de red) y con un límite de
tiempo por llamada.

Cada blob pasa cuatro barreras independientes:

1. Ruta bajo `game/`, `extracted/` o `build/` en la raíz: lo mismo que ignora .gitignore,
   pero también si se fuerza con `git add -f`.
2. Extensión prohibida (formatos del juego y comprimidos). El nombre se pliega antes de
   comparar (NFKD sin diacríticos y A-Z → a-z), así que `CMN.İSO` o `x.ＩＳＯ` no escapan.
3. Firma binaria al inicio del blob (o la magia XDVDFS en 0x10000), que caza renombrados.
4. Tamaño mayor que el límite (un dump o un asset que se coló).

Las firmas son heurísticas: cubren los formatos verificados en el disco, no todos los
posibles. Por eso la extensión y el tamaño siguen siendo barreras propias.

Las fuentes `.ttf` no se bloquean por extensión: una fuente libre puede ser legítima. Las
del juego (23 MB) las caza el tamaño. .gitignore sí ignora `*.ttf`, así que una fuente libre
entra con `git add -f`.

Excepciones: `.hygiene-allow` en la raíz (máximo 64 KiB), una ruta exacta por línea y `#`
para comentarios. Una ruta permitida salta SOLO la regla de extensión. Con `--staged` vale
la lista de HEAD, ya commiteada, para que un commit no pueda eximirse a sí mismo: la
excepción entra antes y a la vista. En la CI vale la del índice, que es el árbol de la PR
y pasa por la revisión. Sin HEAD (repo recién creado), la lista está vacía.

Límite conocido: se revisa el árbol (el índice), no la historia. Basta porque las PRs se
fusionan con squash: a `main` solo llega el árbol final de la PR, que la CI revisa entero.

TODO(rendimiento): un proceso `git cat-file blob` por blob (~20 ms en Windows, hoy ~0,3 s
en total). Si el repo llega a miles de archivos, pasar a un único `cat-file --batch`.

Uso:
    python -m ngb_tools.hygiene            # todo el índice (CI)
    python -m ngb_tools.hygiene --staged   # solo lo preparado (hook pre-commit)

Códigos de salida: 0 limpio, 1 hay violaciones, 2 error de git o de configuración.
"""

from __future__ import annotations

import argparse
import contextlib
import os
import shlex
import signal
import string
import subprocess
import sys
import threading
import unicodedata
from collections.abc import Callable, Iterable, Mapping, Sequence
from dataclasses import dataclass
from enum import StrEnum
from functools import partial
from pathlib import Path, PurePosixPath
from typing import Final

# --- Reglas -----------------------------------------------------------------------------

_GAME_DATA: Final = "datos del juego"
_ARCHIVE: Final = "archivo comprimido (podría contener datos del juego)"

# Extensiones prohibidas, agrupadas por origen. Una entrada nueva aquí exige actualizar
# docs/target/adr-03-datos-fuera-del-repo.md.
_DISC_FILES = (".xbe", ".iso", ".xiso", ".bin", ".def", ".ins")
_CRI_MIDDLEWARE = (".afs", ".sfd", ".aix", ".adx")
_XDK_RESOURCES = (".xpr", ".xwb", ".xsb")
_AFS_PAYLOADS = (".kmd", ".spr", ".sty", ".wva", ".mtn", ".rtm", ".sob", ".itm", ".chr", ".stg")
_ARCHIVES = (".zip", ".7z", ".rar", ".tar", ".gz", ".tgz", ".bz2", ".xz", ".zst")
FORBIDDEN_EXTENSIONS: Final[Mapping[str, str]] = {
    **dict.fromkeys((*_DISC_FILES, *_CRI_MIDDLEWARE, *_XDK_RESOURCES, *_AFS_PAYLOADS), _GAME_DATA),
    **dict.fromkeys(_ARCHIVES, _ARCHIVE),
}

# Directorios de primer nivel que .gitignore ignora; aquí también cuentan con `add -f`.
FORBIDDEN_ROOTS: Final[Mapping[str, str]] = {
    "game": "copia del juego",
    "extracted": "datos extraídos del juego",
    "build": "salida generada por las herramientas",
}

_ASCII_LOWER: Final = str.maketrans(string.ascii_uppercase, string.ascii_lowercase)

# Firmas por prefijo exacto, verificadas en los archivos del juego o estándar del formato.
_PREFIX_SIGNATURES: Final[tuple[tuple[bytes, str], ...]] = (
    (b"XBEH", "ejecutable XBE"),
    (b"AFS\x00", "contenedor AFS de CRI"),
    (b"XPR0", "Xbox Packed Resource"),
    (b"KMD\x00", "modelo KMD de Team Ninja"),
    (b"WBND", "wave bank de XACT (.xwb)"),
    (b"SDBK", "sound bank de XACT (.xsb)"),
    (b"AIXF", "audio AIX de CRI"),
    (b"\x00\x00\x01\xba", "MPEG Program Stream (vídeo Sofdec .sfd)"),
    (b"PK\x03\x04", _ARCHIVE),  # zip
    (b"7z\xbc\xaf\x27\x1c", _ARCHIVE),
    (b"Rar!\x1a\x07", _ARCHIVE),  # RAR 4 y 5
    (b"\x1f\x8b", _ARCHIVE),  # gzip
    (b"\xfd7zXZ\x00", _ARCHIVE),  # xz
    (b"\x28\xb5\x2f\xfd", _ARCHIVE),  # zstd
)

HEAD_BYTES: Final = 0x20

# ADX de CRI: `80 00` y, en los bytes 2-3 (big endian), el offset del copyright `(c)CRI`.
_ADX_MAGIC: Final = b"\x80\x00"
_ADX_COPYRIGHT_OFFSETS: Final = range(0x10, 0x200 + 1)

# Cabecera de chunk de Team Ninja (KMD, itm_dat2, chr_dat, stry_dat, rtm_dat... dentro de
# los AFS): tag ASCII NUL-padded de 12 bytes en 0x00 (>= 3 imprimibles), u32 LE en 0x18 en
# {0x20, 0x40, 0x80} y u32 LE en 0x1C = 0x20. Valores observados, no especificados.
_TN_TAG_SIZE: Final = 12
_TN_MIN_TAG_CHARS: Final = 3
_TN_FIELD_18_VALUES: Final = frozenset({0x20, 0x40, 0x80})
_TN_FIELD_1C_VALUE: Final = 0x20

# bzip2: `BZh`, nivel 1-9 y la magia del primer bloque (o la de fin de flujo si va vacío).
_BZIP2_BLOCK_MAGICS: Final = (b"\x31\x41\x59\x26\x53\x59", b"\x17\x72\x45\x38\x50\x90")

# Imagen XISO: magia XDVDFS en el sector 32.
XDVDFS_MAGIC: Final = b"MICROSOFT*XBOX*MEDIA"
XDVDFS_OFFSET: Final = 0x10000
_XDVDFS_END: Final = XDVDFS_OFFSET + len(XDVDFS_MAGIC)

DEFAULT_MAX_BYTES: Final = 1024 * 1024


class Rule(StrEnum):
    """Regla que una ruta ha violado."""

    PATH = "path"
    EXTENSION = "extension"
    SIGNATURE = "signature"
    SIZE = "size"


@dataclass(frozen=True, slots=True)
class Violation:
    """Una ruta que no debe entrar al repo, y el porqué."""

    path: str
    rule: Rule
    detail: str

    def __str__(self) -> str:
        return f"{self.path}: [{self.rule}] {self.detail}"


@dataclass(frozen=True, slots=True)
class Policy:
    """Parámetros de la revisión: límite de tamaño y rutas exentas de la regla de extensión."""

    max_bytes: int = DEFAULT_MAX_BYTES
    allowed: frozenset[str] = frozenset()


DEFAULT_POLICY: Final = Policy()


def _fold(text: str) -> str:
    """Pliega un nombre para compararlo: NFKD sin marcas combinantes y luego A-Z → a-z.

    `str.lower` no basta: 'İ'.lower() es 'i' + U+0307, y `CMN.İSO` no acabaría en `.iso`.
    NFKD también reduce formas de ancho completo ('ＩＳＯ') y el signo Kelvin a ASCII.
    """
    decomposed = unicodedata.normalize("NFKD", text)
    bare = "".join(char for char in decomposed if not unicodedata.combining(char))
    return bare.translate(_ASCII_LOWER)


def forbidden_extension(path: str) -> str | None:
    """Extensión prohibida en la que acaba el nombre de `path`, si hay (`.iso` incluido)."""
    name = _fold(PurePosixPath(path).name)
    return next((ext for ext in FORBIDDEN_EXTENSIONS if name.endswith(ext)), None)


def forbidden_root(path: str) -> str | None:
    """Directorio prohibido de primer nivel que contiene `path`, si hay (plegado)."""
    parts = PurePosixPath(path).parts
    if len(parts) < 2:
        return None
    root = _fold(parts[0])
    return root if root in FORBIDDEN_ROOTS else None


def _match_prefix(head: bytes) -> str | None:
    return next((desc for magic, desc in _PREFIX_SIGNATURES if head.startswith(magic)), None)


def _match_adx(head: bytes) -> str | None:
    if len(head) < 4 or not head.startswith(_ADX_MAGIC):
        return None
    copyright_offset = int.from_bytes(head[2:4], "big")
    return "audio ADX de CRI" if copyright_offset in _ADX_COPYRIGHT_OFFSETS else None


def _match_team_ninja_chunk(head: bytes) -> str | None:
    if len(head) < HEAD_BYTES:
        return None
    tag = head[:_TN_TAG_SIZE].rstrip(b"\0")
    if len(tag) < _TN_MIN_TAG_CHARS or not all(0x20 <= byte <= 0x7E for byte in tag):
        return None
    field_18 = int.from_bytes(head[0x18:0x1C], "little")
    field_1c = int.from_bytes(head[0x1C:0x20], "little")
    if field_18 not in _TN_FIELD_18_VALUES or field_1c != _TN_FIELD_1C_VALUE:
        return None
    return f"chunk de Team Ninja '{tag.decode('ascii')}'"


def _match_bzip2(head: bytes) -> str | None:
    level = head[3:4]
    is_bzip2 = (
        head.startswith(b"BZh")
        and level.isdigit()
        and level != b"0"
        and head[4:10] in _BZIP2_BLOCK_MAGICS
    )
    return _ARCHIVE if is_bzip2 else None


def _match_xdvdfs(prefix: bytes) -> str | None:
    magic = prefix[XDVDFS_OFFSET:_XDVDFS_END]
    return "imagen de disco XDVDFS (XISO)" if magic == XDVDFS_MAGIC else None


_DETECTORS: Final[tuple[Callable[[bytes], str | None], ...]] = (
    _match_prefix,
    _match_adx,
    _match_team_ninja_chunk,
    _match_bzip2,
    _match_xdvdfs,
)


def match_signature(prefix: bytes) -> str | None:
    """Describe la firma del juego (o de comprimido) presente en el prefijo de un blob.

    `prefix` son los primeros bytes del blob: al menos `HEAD_BYTES` (o el blob entero si es
    menor) y hasta pasada la magia XDVDFS para detectar imágenes de disco.
    """
    for detect in _DETECTORS:
        if (description := detect(prefix)) is not None:
            return description
    return None


def _prefix_length(size: int) -> int:
    """Bytes que hay que leer de un blob de `size` bytes: lo justo para todas las firmas."""
    return _XDVDFS_END if size >= _XDVDFS_END else min(size, HEAD_BYTES)


def check_blob(
    path: str, size: int, read_prefix: Callable[[int], bytes], policy: Policy = DEFAULT_POLICY
) -> list[Violation]:
    """Aplica las cuatro reglas a un blob y devuelve sus violaciones.

    `read_prefix(n)` devuelve los primeros `n` bytes del blob (`n` nunca supera `size`).
    """
    violations: list[Violation] = []
    if (root := forbidden_root(path)) is not None:
        detail = f"ruta bajo '{root}/': {FORBIDDEN_ROOTS[root]}"
        violations.append(Violation(path, Rule.PATH, detail))
    if path not in policy.allowed and (ext := forbidden_extension(path)) is not None:
        detail = f"extensión '{ext}': {FORBIDDEN_EXTENSIONS[ext]}"
        violations.append(Violation(path, Rule.EXTENSION, detail))
    if size > policy.max_bytes:
        detail = f"{size} bytes > límite {policy.max_bytes}"
        violations.append(Violation(path, Rule.SIZE, detail))
    if (signature := match_signature(read_prefix(_prefix_length(size)))) is not None:
        violations.append(Violation(path, Rule.SIGNATURE, f"firma de {signature}"))
    return violations


def parse_allowlist(text: str) -> frozenset[str]:
    """Rutas exactas de `.hygiene-allow`: una por línea; vacías y `#...` se ignoran."""
    lines = (line.strip() for line in text.splitlines())
    return frozenset(line for line in lines if line and not line.startswith("#"))


# --- Errores y parseo de la salida de git (puro) -------------------------------------------


class HygieneError(Exception):
    """La revisión no pudo completarse (git o configuración); se informa con código 2."""


class GitError(HygieneError):
    """git devolvió un error o una salida inesperada."""


@dataclass(frozen=True, slots=True)
class IndexEntry:
    """Una entrada del índice de git (`git ls-files -s`)."""

    mode: str
    oid: str
    stage: int
    path: str


def _decode_path(raw: bytes) -> str:
    """Ruta de git (bytes, UTF-8 por convención) a str sin fallar nunca.

    `surrogateescape` conserva los bytes no UTF-8; `os.fsdecode` en Windows es estricto.
    """
    return raw.decode("utf-8", "surrogateescape")


def parse_index_entries(output: bytes) -> list[IndexEntry]:
    """Parsea `git ls-files -s -z`: `<modo> <oid> <etapa>\\t<ruta>\\0` por entrada."""
    entries: list[IndexEntry] = []
    for record in output.split(b"\0"):
        if not record:
            continue
        meta, _, path = record.partition(b"\t")
        mode, oid, stage = meta.decode("ascii").split(" ")
        entries.append(IndexEntry(mode, oid, int(stage), _decode_path(path)))
    return entries


def parse_name_list(output: bytes) -> list[str]:
    """Parsea una lista de rutas separadas por NUL (`-z`)."""
    return [_decode_path(raw) for raw in output.split(b"\0") if raw]


def parse_batch_check(output: bytes) -> dict[str, int]:
    """Parsea `git cat-file --batch-check`: `<oid> <tipo> <tamaño>` por línea."""
    sizes: dict[str, int] = {}
    for line in output.decode("ascii").splitlines():
        fields = line.split(" ")
        if len(fields) != 3 or fields[1] != "blob":
            raise GitError(f"se esperaba un blob en el índice y git dio: {line!r}")
        oid, _, size = fields
        sizes[oid] = int(size)
    return sizes


# --- IO con git: todo lo que toca el repo vive aquí ---------------------------------------

GITLINK_MODE: Final = "160000"
ALLOWLIST_PATH: Final = ".hygiene-allow"

# Lo preparado respecto a HEAD (también con la rama aún sin commits). --no-renames: un
# renombrado sale como A (ruta nueva) sin que git lea blobs para medir parecidos.
# T (cambio de tipo, p. ej. symlink → archivo normal) se colaría con solo ACMR.
_STAGED_DIFF: Final = (
    "diff",
    "--cached",
    "--name-only",
    "-z",
    "--no-renames",
    "--no-color",
    "--diff-filter=AMT",
)


_BATCH_CHECK: Final = ("cat-file", "--batch-check=%(objectname) %(objecttype) %(objectsize)")
ALLOWLIST_MAX_BYTES: Final = 64 * 1024
GIT_TIMEOUT_SECONDS: Final = 60.0

# Para toda llamada a git: un `refs/replace/*` no puede sustituir el blob que se juzga, y en
# un clon parcial un objeto ausente es un error en vez de una descarga que puede colgarse.
_GIT_ENV: Final[Mapping[str, str]] = {"GIT_NO_REPLACE_OBJECTS": "1", "GIT_NO_LAZY_FETCH": "1"}


def _describe_failure(command: Sequence[str], returncode: int | None, stderr: bytes) -> str:
    message = stderr.decode("utf-8", "replace").strip() or "sin mensaje de error"
    return f"`{shlex.join(command)}` falló (código {returncode}): {message}"


def _describe_timeout(command: Sequence[str], timeout: float) -> str:
    return f"`{shlex.join(command)}` no terminó en {timeout:g} s"


def _spawn(command: Sequence[str], cwd: Path, *, stdin: int) -> subprocess.Popen[bytes]:
    """Lanza `command` con `_GIT_ENV`, tubos en stdout/stderr y su propio grupo en POSIX."""
    try:
        return subprocess.Popen(
            command,
            cwd=cwd,
            env={**os.environ, **_GIT_ENV},
            stdin=stdin,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            process_group=None if sys.platform == "win32" else 0,
        )
    except OSError as error:
        raise GitError(f"no se pudo ejecutar `{shlex.join(command)}` en {cwd}: {error}") from error


def _kill_tree(process: subprocess.Popen[bytes]) -> None:
    """Mata el proceso y sus descendientes, que si no retendrían los tubos abiertos.

    En Windows `cmd\\git.exe` es un lanzador: el git real es su hijo y sobrevive a un `kill`
    simple. En POSIX el proceso encabeza su propio grupo (`process_group=0`).
    """
    with contextlib.suppress(OSError, subprocess.SubprocessError):
        if sys.platform == "win32":
            system32 = Path(os.environ.get("SYSTEMROOT", r"C:\Windows"), "System32")
            command = [str(system32 / "taskkill.exe"), "/F", "/T", "/PID", str(process.pid)]
            subprocess.run(command, check=False, capture_output=True, timeout=10)
        else:
            os.killpg(process.pid, signal.SIGKILL)
    process.kill()


def run_command(
    command: Sequence[str], *, cwd: Path, stdin: bytes = b"", timeout: float = GIT_TIMEOUT_SECONDS
) -> subprocess.CompletedProcess[bytes]:
    """Ejecuta `command` con `_GIT_ENV`; no poder lanzarlo o pasar de `timeout` es `GitError`."""
    with _spawn(command, cwd, stdin=subprocess.PIPE) as process:
        try:
            stdout, stderr = process.communicate(stdin, timeout=timeout)
        except subprocess.TimeoutExpired as error:
            _kill_tree(process)
            process.communicate()
            raise GitError(_describe_timeout(command, timeout)) from error
    return subprocess.CompletedProcess(command, process.returncode, stdout, stderr)


def read_command_prefix(
    command: Sequence[str], *, cwd: Path, length: int, timeout: float = GIT_TIMEOUT_SECONDS
) -> bytes:
    """Primeros `length` bytes del stdout de `command`, sin leer el resto.

    Se cierra el tubo en cuanto llega lo necesario: el proceso recibe EPIPE y termina, así
    que un blob de varios GB cuesta lo mismo que uno pequeño. Un temporizador mata el árbol
    de procesos pasado `timeout`: cubre la lectura bloqueada, no solo la espera final.
    """
    if length == 0:
        return b""
    expired = threading.Event()
    with _spawn(command, cwd, stdin=subprocess.DEVNULL) as process:
        assert process.stdout is not None and process.stderr is not None  # PIPE

        def expire() -> None:
            expired.set()
            _kill_tree(process)

        watchdog = threading.Timer(timeout, expire)
        watchdog.start()
        try:
            data = process.stdout.read(length)
            process.stdout.close()
            stderr = process.stderr.read()
            returncode = process.wait()
        finally:
            watchdog.cancel()
    if len(data) < length:
        if expired.is_set():
            raise GitError(_describe_timeout(command, timeout))
        raise GitError(_describe_failure(command, returncode, stderr))
    return data


def _run_git(args: Sequence[str], cwd: Path, stdin: bytes = b"") -> bytes:
    """Ejecuta git y devuelve su stdout; un código distinto de 0 es `GitError`."""
    command = ["git", *args]
    completed = run_command(command, cwd=cwd, stdin=stdin)
    if completed.returncode != 0:
        raise GitError(_describe_failure(command, completed.returncode, completed.stderr))
    return completed.stdout


class GitRepo:
    """Lectura del índice de un repo. git se ejecuta siempre desde la raíz del árbol."""

    def __init__(self, toplevel: Path) -> None:
        self.toplevel = toplevel

    @classmethod
    def discover(cls, start: Path) -> GitRepo:
        """Repo que contiene `start`, que puede ser cualquier ruta dentro del árbol."""
        output = _run_git(["rev-parse", "--show-toplevel"], cwd=start)
        return cls(Path(_decode_path(output.rstrip(b"\r\n"))))

    def _git(self, *args: str, stdin: bytes = b"") -> bytes:
        return _run_git(args, cwd=self.toplevel, stdin=stdin)

    def index_entries(self) -> list[IndexEntry]:
        """Todas las entradas del índice, con rutas relativas a la raíz."""
        return parse_index_entries(self._git("ls-files", "-s", "-z", "--full-name"))

    def staged_paths(self) -> list[str]:
        """Rutas que el índice añade, modifica o cambia de tipo respecto a HEAD."""
        return parse_name_list(self._git(*_STAGED_DIFF))

    def blob_sizes(self, oids: Iterable[str]) -> dict[str, int]:
        """Tamaño de cada blob, en una sola llamada a git."""
        unique = dict.fromkeys(oids)
        if not unique:
            return {}
        request = "".join(f"{oid}\n" for oid in unique).encode("ascii")
        sizes = parse_batch_check(self._git(*_BATCH_CHECK, stdin=request))
        if missing := unique.keys() - sizes.keys():
            raise GitError(f"git no dio el tamaño de {len(missing)} blob(s): {sorted(missing)}")
        return sizes

    def resolve_blob(self, spec: str) -> str | None:
        """oid al que apunta `spec` (`HEAD:ruta`, `:0:ruta`); None si no existe.

        Con `-q --verify`, git sale con 1 tanto si falta la ruta como si HEAD aún no existe.
        """
        command = ["git", "rev-parse", "-q", "--verify", spec]
        completed = run_command(command, cwd=self.toplevel)
        if completed.returncode == 1:
            return None
        if completed.returncode != 0:
            raise GitError(_describe_failure(command, completed.returncode, completed.stderr))
        return completed.stdout.decode("ascii").strip()

    def read_blob_prefix(self, oid: str, length: int) -> bytes:
        """Primeros `length` bytes del blob sin volcar el resto (ver `read_command_prefix`)."""
        command = ["git", "cat-file", "blob", oid]
        return read_command_prefix(command, cwd=self.toplevel, length=length)


@dataclass(frozen=True, slots=True)
class ScanReport:
    """Resultado de una revisión: blobs mirados y violaciones encontradas."""

    checked: int
    violations: tuple[Violation, ...]


def _load_allowlist(repo: GitRepo, spec: str) -> frozenset[str]:
    """Excepciones de `.hygiene-allow` en la versión `spec` de git (nunca la del disco)."""
    oid = repo.resolve_blob(spec)
    if oid is None:
        return frozenset()
    size = repo.blob_sizes([oid])[oid]
    if size > ALLOWLIST_MAX_BYTES:
        raise HygieneError(f"{ALLOWLIST_PATH} ocupa {size} bytes; máximo {ALLOWLIST_MAX_BYTES}")
    try:
        text = repo.read_blob_prefix(oid, size).decode("utf-8-sig")
    except UnicodeDecodeError as error:
        raise HygieneError(f"{ALLOWLIST_PATH} no es UTF-8 válido: {error}") from error
    return parse_allowlist(text)


def scan(repo: GitRepo, *, staged: bool, max_bytes: int = DEFAULT_MAX_BYTES) -> ScanReport:
    """Revisa los blobs del índice: todos o, con `staged`, solo lo preparado.

    La lista de excepciones sale de HEAD con `staged` (un commit no se exime a sí mismo) y
    del índice en la revisión completa (el árbol de la PR).
    """
    allowlist_spec = f"HEAD:{ALLOWLIST_PATH}" if staged else f":0:{ALLOWLIST_PATH}"
    policy = Policy(max_bytes=max_bytes, allowed=_load_allowlist(repo, allowlist_spec))
    entries = repo.index_entries()
    if staged:
        prepared = set(repo.staged_paths())
        entries = [e for e in entries if e.path in prepared]
    blobs = [e for e in entries if e.mode != GITLINK_MODE]
    sizes = repo.blob_sizes(e.oid for e in blobs)
    violations = tuple(
        violation
        for e in blobs
        for violation in check_blob(
            e.path, sizes[e.oid], partial(repo.read_blob_prefix, e.oid), policy
        )
    )
    return ScanReport(checked=len(blobs), violations=violations)


# --- CLI ----------------------------------------------------------------------------------


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="ngb-hygiene", description="Impide versionar datos del juego."
    )
    parser.add_argument(
        "--staged", action="store_true", help="revisar solo lo preparado (hook pre-commit)"
    )
    parser.add_argument(
        "--root", type=Path, default=Path(), help="cualquier ruta dentro del repo (por defecto, .)"
    )
    parser.add_argument(
        "--max-bytes", type=int, default=DEFAULT_MAX_BYTES, help="tamaño máximo por archivo"
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        repo = GitRepo.discover(args.root)
        report = scan(repo, staged=args.staged, max_bytes=args.max_bytes)
    except HygieneError as error:
        print(f"ngb-hygiene: {error}", file=sys.stderr)
        return 2
    for violation in report.violations:
        print(violation, file=sys.stderr)
    if report.violations:
        print(
            f"ngb-hygiene: {len(report.violations)} violación(es); nada del juego entra al "
            "repo. Sácalo del índice con `git rm --cached -- <ruta>`.",
            file=sys.stderr,
        )
        return 1
    print(f"ngb-hygiene: {report.checked} archivo(s) revisados, sin datos del juego.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
