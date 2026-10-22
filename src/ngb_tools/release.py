"""Release packaging: the player zip and the update manifest (ADR-07).

Run by `.github/workflows/release.yml` after `port\\build.cmd`:

    python -m ngb_tools.release --tag v0.3.0 --build build/port --out build/release

It verifies that the tag matches `port/res/version.h` (a `-dev` version never ships), then
writes two files into `--out`:

- `ngb-port-vX.Y.Z-win32.zip`: `ngb-launcher.exe`, `ngb-loader.exe`, `ngb-core.dll`,
  `LICENSE`, `THIRD_PARTY.md` and `README.txt`, all at the zip root and nothing else. The
  files go through the hygiene guard first (the same rules as the repo), so game data cannot
  ride along. Entries have a fixed timestamp and mode, so the same inputs give the same bytes.
- `ngb-update.txt`: the manifest the launcher downloads (`key=value` lines, LF, ASCII).

Exit codes: 0 done, 1 refused (bad tag, missing file, hygiene violation).
"""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
import zipfile
from collections.abc import Callable, Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Final

from ngb_tools.hygiene import Policy, check_blob

REPO_URL: Final = "https://github.com/angeldev07/ninja-gaiden-black-port"
VERSION_H: Final = Path("port") / "res" / "version.h"
MANIFEST_NAME: Final = "ngb-update.txt"

BINARIES: Final = ("ngb-launcher.exe", "ngb-loader.exe", "ngb-core.dll")
REPO_FILES: Final = ("LICENSE", "THIRD_PARTY.md")
README_NAME: Final = "README.txt"

# Zip entries are stored with a fixed timestamp (the zip epoch) and mode, never the files'.
ZIP_EPOCH: Final = (1980, 1, 1, 0, 0, 0)
_ZIP_MODE: Final = 0o100644 << 16
_ZIP_CREATE_SYSTEM_UNIX: Final = 3

# The guard's default limit (1 MiB) is for source files; ngb-core.dll is bigger than that.
# Extension, signature and path rules apply in full; this only bounds a runaway file.
MAX_FILE_BYTES: Final = 64 * 1024 * 1024

_VERSION_RE: Final = re.compile(r'^\s*#\s*define\s+NGB_VERSION_STR\s+"([^"]+)"', re.MULTILINE)
_RELEASE_VERSION_RE: Final = re.compile(r"^\d+\.\d+\.\d+$")

README_TEMPLATE: Final = """\
Ninja Gaiden Black - native PC port v{version}
================================================

This is an unofficial, fan-made port of Ninja Gaiden Black (Xbox) to Windows. It does not
contain the game: you need your own copy of the disc.

How to play
-----------
1. Unzip this folder anywhere you can write to (not Program Files).
2. Run ngb-launcher.exe.
3. Click "Import game" and pick the image of your own disc (the Xbox ISO / XISO).
4. Click PLAY.

Windows SmartScreen
-------------------
The programs are not code-signed, so Windows SmartScreen may warn the first time you run
them. Click "More info" and then "Run anyway". The full source code is public, and you can
read it or build it yourself from the repository below.

Updates
-------
The launcher checks once a day whether a newer version exists and offers to install it. You
can turn the check off in the launcher, under Advanced, "Check for updates".

Links
-----
Source and releases: {repo}
How to contribute:   {repo}/blob/main/CONTRIBUTING.md
Report a problem:    {repo}/issues

Legal notice
------------
Ninja Gaiden and Ninja Gaiden Black are trademarks of Koei Tecmo Holdings Co., Ltd.
Xbox and Windows are trademarks of Microsoft Corporation. This project is not affiliated
with, endorsed by or sponsored by Koei Tecmo, Team Ninja or Microsoft. It contains no game
data, and you must own a copy of the game to use it. It is a non-commercial project made
by fans. See LICENSE and THIRD_PARTY.md for the license and the credits.
"""


class ReleaseError(Exception):
    """The release cannot be produced; reported with exit code 1."""


@dataclass(frozen=True, slots=True)
class Packaged:
    """Files written by `package` and the digest of the zip."""

    zip_path: Path
    manifest_path: Path
    sha256: str


def read_version(version_h: Path) -> str:
    """`NGB_VERSION_STR` from `version.h`."""
    try:
        text = version_h.read_text(encoding="utf-8")
    except OSError as error:
        raise ReleaseError(f"cannot read {version_h}: {error}") from error
    match = _VERSION_RE.search(text)
    if match is None:
        raise ReleaseError(f"no NGB_VERSION_STR in {version_h}")
    return match.group(1)


def check_release_version(version: str) -> None:
    """Refuse anything that is not a plain X.Y.Z (`-dev` and other suffixes included)."""
    if version.endswith("-dev"):
        raise ReleaseError(f"version {version!r} is a development version; it cannot be released")
    if _RELEASE_VERSION_RE.match(version) is None:
        raise ReleaseError(f"version {version!r} is not X.Y.Z")


def check_tag(version: str, tag: str) -> None:
    """The tag must be `v` + the version in `version.h`, exactly, and a release version."""
    check_release_version(version)
    if tag != f"v{version}":
        raise ReleaseError(f"tag {tag!r} does not match version.h: expected 'v{version}'")


def render_readme(version: str) -> str:
    return README_TEMPLATE.format(version=version, repo=REPO_URL)


def release_notes_url(version: str) -> str:
    return f"{REPO_URL}/releases/tag/v{version}"


def zip_name(version: str) -> str:
    return f"ngb-port-v{version}-win32.zip"


def render_manifest(version: str, name: str, size: int, sha256: str) -> str:
    """The `ngb-update.txt` text: `key=value` lines, LF endings, ASCII."""
    lines = (
        f"version={version}",
        f"zip={name}",
        f"size={size}",
        f"sha256={sha256}",
        f"notes={release_notes_url(version)}",
    )
    return "".join(f"{line}\n" for line in lines)


def _collect(build_dir: Path, repo_root: Path, version: str) -> list[tuple[str, bytes]]:
    """Name and content of each zip entry, in the order they are stored."""
    sources = [(name, build_dir / name) for name in BINARIES]
    sources += [(name, repo_root / name) for name in REPO_FILES]
    entries: list[tuple[str, bytes]] = []
    for name, path in sources:
        try:
            entries.append((name, path.read_bytes()))
        except OSError as error:
            raise ReleaseError(f"cannot read {path}: {error}") from error
    entries.append((README_NAME, render_readme(version).replace("\n", "\r\n").encode("ascii")))
    return entries


def _prefix_reader(content: bytes) -> Callable[[int], bytes]:
    return lambda length: content[:length]


def check_hygiene(entries: Sequence[tuple[str, bytes]]) -> None:
    """Run the game-data guard over what is about to be zipped."""
    policy = Policy(max_bytes=MAX_FILE_BYTES)
    violations = [
        violation
        for name, content in entries
        for violation in check_blob(name, len(content), _prefix_reader(content), policy)
    ]
    if violations:
        details = "\n".join(f"  {violation}" for violation in violations)
        raise ReleaseError(f"hygiene guard refused the release files:\n{details}")


def _write_zip(zip_path: Path, entries: Sequence[tuple[str, bytes]]) -> None:
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, content in entries:
            info = zipfile.ZipInfo(name, ZIP_EPOCH)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = _ZIP_CREATE_SYSTEM_UNIX
            info.external_attr = _ZIP_MODE
            archive.writestr(info, content, zipfile.ZIP_DEFLATED, 9)


def package(
    build_dir: Path, repo_root: Path, out_dir: Path, version: str, *, rehearsal: bool = False
) -> Packaged:
    """Write the release zip and its manifest into `out_dir` (`rehearsal`: any version)."""
    if not rehearsal:
        check_release_version(version)
    entries = _collect(build_dir, repo_root, version)
    check_hygiene(entries)
    out_dir.mkdir(parents=True, exist_ok=True)
    name = zip_name(version)
    zip_path = out_dir / name
    _write_zip(zip_path, entries)
    data = zip_path.read_bytes()
    sha256 = hashlib.sha256(data).hexdigest()
    manifest = render_manifest(version, name, len(data), sha256)
    manifest_path = out_dir / MANIFEST_NAME
    manifest_path.write_bytes(manifest.encode("ascii"))
    return Packaged(zip_path, manifest_path, sha256)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="ngb-release", description="Builds the release zip and the update manifest."
    )
    parser.add_argument("--tag", help="release tag, e.g. v0.3.0 (required unless --dry-run)")
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="package version.h as it is (a -dev version too), without a tag: CI rehearsal",
    )
    parser.add_argument(
        "--build", type=Path, default=Path("build/port"), help="folder with the built binaries"
    )
    parser.add_argument(
        "--out", type=Path, default=Path("build/release"), help="output folder (created)"
    )
    parser.add_argument(
        "--root", type=Path, default=Path(), help="repo root (default: current directory)"
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        version = read_version(args.root / VERSION_H)
        if not args.dry_run:
            if not args.tag:
                raise ReleaseError("--tag is required (or --dry-run)")
            check_tag(version, args.tag)
        result = package(args.build, args.root, args.out, version, rehearsal=args.dry_run)
    except ReleaseError as error:
        print(f"ngb-release: {error}", file=sys.stderr)
        return 1
    print(f"zip:      {result.zip_path}")
    print(f"manifest: {result.manifest_path}")
    print(f"sha256:   {result.sha256}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
