"""Tests of release packaging: version/tag checks, zip contents, manifest, determinism."""

from __future__ import annotations

import hashlib
import io
import tempfile
import unittest
import zipfile
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

from ngb_tools.release import (
    BINARIES,
    MANIFEST_NAME,
    ReleaseError,
    check_hygiene,
    check_tag,
    main,
    package,
    read_version,
    zip_name,
)

EXPECTED_ENTRIES = {
    "ngb-launcher.exe",
    "ngb-loader.exe",
    "ngb-core.dll",
    "LICENSE",
    "THIRD_PARTY.md",
    "README.txt",
}
VERSION_H = '#pragma once\n\n#define NGB_VERSION_STR "{}"\n#define NGB_VERSION_NUM 0, 3, 0, 0\n'


class Workspace(unittest.TestCase):
    """A temp repo root with fake binaries, so no real build is needed."""

    def setUp(self) -> None:
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name) / "repo"
        self.build = self.root / "build" / "port"
        self.out = self.root / "build" / "release"
        self.build.mkdir(parents=True)
        for name in BINARIES:
            (self.build / name).write_bytes(b"MZ" + name.encode() + bytes(2000))
        (self.root / "LICENSE").write_bytes(b"license text\n")
        (self.root / "THIRD_PARTY.md").write_bytes(b"# Third party\n")
        self.set_version("0.3.0")

    def set_version(self, version: str) -> None:
        version_h = self.root / "port" / "res" / "version.h"
        version_h.parent.mkdir(parents=True, exist_ok=True)
        version_h.write_text(VERSION_H.format(version), encoding="utf-8")


class VersionTest(Workspace):
    def test_read_version(self) -> None:
        self.assertEqual(read_version(self.root / "port" / "res" / "version.h"), "0.3.0")

    def test_read_version_keeps_dev_suffix(self) -> None:
        self.set_version("0.3.0-dev")
        self.assertEqual(read_version(self.root / "port" / "res" / "version.h"), "0.3.0-dev")

    def test_read_version_missing_define(self) -> None:
        path = self.root / "port" / "res" / "version.h"
        path.write_text("#pragma once\n", encoding="utf-8")
        with self.assertRaises(ReleaseError):
            read_version(path)

    def test_read_version_missing_file(self) -> None:
        with self.assertRaises(ReleaseError):
            read_version(self.root / "nope.h")


class TagTest(unittest.TestCase):
    def test_matching_tag_is_accepted(self) -> None:
        check_tag("0.3.0", "v0.3.0")

    def test_mismatch_is_refused(self) -> None:
        for tag in ("v0.3.1", "0.3.0", "v0.3.0-rc1", "V0.3.0", ""):
            with self.subTest(tag=tag), self.assertRaises(ReleaseError):
                check_tag("0.3.0", tag)

    def test_dev_version_is_refused_even_with_matching_tag(self) -> None:
        with self.assertRaisesRegex(ReleaseError, "development"):
            check_tag("0.3.0-dev", "v0.3.0-dev")

    def test_non_semver_is_refused(self) -> None:
        with self.assertRaises(ReleaseError):
            check_tag("0.3", "v0.3")


class PackageTest(Workspace):
    def run_package(self) -> tuple[Path, Path]:
        result = package(self.build, self.root, self.out, "0.3.0")
        return result.zip_path, result.manifest_path

    def test_zip_has_exactly_the_six_entries_at_the_root(self) -> None:
        zip_path, _ = self.run_package()
        self.assertEqual(zip_path.name, zip_name("0.3.0"))
        self.assertEqual(zip_path.name, "ngb-port-v0.3.0-win32.zip")
        with zipfile.ZipFile(zip_path) as archive:
            self.assertEqual(
                archive.namelist(), [*BINARIES, "LICENSE", "THIRD_PARTY.md", "README.txt"]
            )
            self.assertEqual(set(archive.namelist()), EXPECTED_ENTRIES)
            self.assertIsNone(archive.testzip())
            self.assertTrue(
                all(i.compress_type == zipfile.ZIP_DEFLATED for i in archive.infolist())
            )
            self.assertEqual(
                archive.read("ngb-core.dll"), (self.build / "ngb-core.dll").read_bytes()
            )
            self.assertEqual(archive.read("LICENSE"), b"license text\n")

    def test_readme_covers_the_player_essentials(self) -> None:
        zip_path, _ = self.run_package()
        with zipfile.ZipFile(zip_path) as archive:
            text = archive.read("README.txt").decode("ascii")
        for needle in (
            "v0.3.0",
            "ngb-launcher.exe",
            "Import game",
            "PLAY",
            "SmartScreen",
            "Run anyway",
            "Advanced",
            "github.com/angeldev07/ninja-gaiden-black-port",
            "CONTRIBUTING",
            "issues",
            "Koei Tecmo",
            "Microsoft",
            "non-commercial",
        ):
            with self.subTest(needle=needle):
                self.assertIn(needle, text)

    def test_manifest_matches_the_zip(self) -> None:
        zip_path, manifest_path = self.run_package()
        raw = manifest_path.read_bytes()
        self.assertEqual(manifest_path.name, MANIFEST_NAME)
        self.assertTrue(raw.isascii())
        self.assertNotIn(b"\r", raw)
        self.assertTrue(raw.endswith(b"\n"))
        fields = dict(line.split("=", 1) for line in raw.decode("ascii").splitlines())
        data = zip_path.read_bytes()
        self.assertEqual(list(fields), ["version", "zip", "size", "sha256", "notes"])
        self.assertEqual(fields["version"], "0.3.0")
        self.assertEqual(fields["zip"], zip_path.name)
        self.assertEqual(fields["size"], str(len(data)))
        self.assertEqual(fields["sha256"], hashlib.sha256(data).hexdigest())
        self.assertEqual(fields["sha256"], fields["sha256"].lower())
        self.assertEqual(
            fields["notes"],
            "https://github.com/angeldev07/ninja-gaiden-black-port/releases/tag/v0.3.0",
        )

    def test_zip_bytes_are_deterministic(self) -> None:
        first, _ = self.run_package()
        first_bytes = first.read_bytes()
        # Different file mtimes and a different output folder must not change the bytes.
        for path in (*(self.build / n for n in BINARIES), self.root / "LICENSE"):
            path.touch()
        self.out = self.root / "build" / "release2"
        second, _ = self.run_package()
        self.assertEqual(second.read_bytes(), first_bytes)

    def test_entries_have_a_fixed_timestamp(self) -> None:
        zip_path, _ = self.run_package()
        with zipfile.ZipFile(zip_path) as archive:
            self.assertEqual({i.date_time for i in archive.infolist()}, {(1980, 1, 1, 0, 0, 0)})

    def test_game_signature_in_a_binary_fails_the_hygiene_guard(self) -> None:
        (self.build / "ngb-core.dll").write_bytes(b"XBEH" + bytes(100))
        with self.assertRaisesRegex(ReleaseError, "hygiene"):
            self.run_package()
        self.assertFalse((self.out / zip_name("0.3.0")).exists())

    def test_game_extension_fails_the_hygiene_guard(self) -> None:
        with self.assertRaisesRegex(ReleaseError, "extension"):
            check_hygiene([("payload.afs", bytes(64))])

    def test_missing_binary_is_refused(self) -> None:
        (self.build / "ngb-loader.exe").unlink()
        with self.assertRaisesRegex(ReleaseError, "ngb-loader.exe"):
            self.run_package()

    def test_dev_version_cannot_be_packaged(self) -> None:
        with self.assertRaises(ReleaseError):
            package(self.build, self.root, self.out, "0.3.0-dev")


class CliTest(Workspace):
    def run_cli(self, tag: str | None) -> tuple[int, str, str]:
        argv = ["--tag", tag] if tag else ["--dry-run"]
        argv += [
            "--root",
            str(self.root),
            "--build",
            str(self.build),
            "--out",
            str(self.out),
        ]
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            code = main(argv)
        return code, out.getvalue(), err.getvalue()

    def test_ok_prints_paths_and_sha256(self) -> None:
        code, out, _ = self.run_cli("v0.3.0")
        self.assertEqual(code, 0)
        data = (self.out / zip_name("0.3.0")).read_bytes()
        self.assertIn(hashlib.sha256(data).hexdigest(), out)
        self.assertIn("ngb-port-v0.3.0-win32.zip", out)

    def test_wrong_tag_exits_1_and_writes_nothing(self) -> None:
        code, _, err = self.run_cli("v0.3.1")
        self.assertEqual(code, 1)
        self.assertIn("v0.3.0", err)
        self.assertFalse(self.out.exists())

    def test_dev_version_h_exits_1(self) -> None:
        self.set_version("0.3.0-dev")
        code, _, _ = self.run_cli("v0.3.0-dev")
        self.assertEqual(code, 1)

    def test_dry_run_packages_a_dev_version_without_a_tag(self) -> None:
        self.set_version("0.3.0-dev")
        code, out, _ = self.run_cli(None)
        self.assertEqual(code, 0)
        self.assertIn("ngb-port-v0.3.0-dev-win32.zip", out)
        self.assertTrue((self.out / MANIFEST_NAME).exists())


if __name__ == "__main__":
    unittest.main()
