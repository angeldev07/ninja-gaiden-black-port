"""Tests de la extracción por streaming, su manifiesto y su ruta por defecto."""

from __future__ import annotations

import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from ngb_tools._paths import PARTIAL_SUFFIX
from ngb_tools.afs import AfsFormatError, AfsTruncatedError, AfsUsageError, UnsafeOutputError
from ngb_tools.afs.extract import MANIFEST_NAME, default_extract_dir, extract_archive

from .synthetic import FILES, build_afs

REPO = Path(__file__).resolve().parents[2]


class ExtractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.afs_path = self.root / "sample.afs"
        self.afs_path.write_bytes(build_afs(FILES))

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def test_extract_all(self) -> None:
        out = self.root / "out"
        manifest = extract_archive(self.afs_path, out)
        names = sorted(p.name for p in out.iterdir())
        expected = ["0000_menu.xpr", "0001_song.zlib", "0002_table.bin", MANIFEST_NAME]
        self.assertEqual(names, expected)
        self.assertEqual((out / "0001_song.zlib").read_bytes(), FILES[1][1])
        self.assertEqual(len(manifest["entries"]), 3)
        meta = json.loads((out / MANIFEST_NAME).read_text(encoding="utf-8"))
        self.assertEqual(meta["entries"][0]["kind"], "XPR0")
        self.assertEqual(meta, manifest)

    def test_extract_single_index_without_names(self) -> None:
        self.afs_path.write_bytes(build_afs(FILES, "none"))
        out = self.root / "one"
        extract_archive(self.afs_path, out, [0])
        self.assertTrue((out / "0000_sin_nombre.xpr").exists())
        self.assertFalse((out / "0001_sin_nombre.zlib").exists())

    def test_extract_refuses_repo(self) -> None:
        target = REPO / "build" / "nope"
        with self.assertRaises(UnsafeOutputError):
            extract_archive(self.afs_path, target)
        self.assertFalse(target.exists())

    def test_repeated_indices_are_extracted_once(self) -> None:
        manifest = extract_archive(self.afs_path, self.root / "out", [2, 0, 2])
        self.assertEqual([m["index"] for m in manifest["entries"]], [2, 0])

    def test_unknown_index_is_a_usage_error_and_writes_nothing(self) -> None:
        out = self.root / "out"
        for index in (3, -1):
            with self.subTest(index=index), self.assertRaises(AfsUsageError):
                extract_archive(self.afs_path, out, [index])
        self.assertFalse(out.exists())

    def test_invalid_archive_writes_nothing(self) -> None:
        self.afs_path.write_bytes(b"NOPE" + bytes(12))
        out = self.root / "out"
        with self.assertRaises(AfsFormatError):
            extract_archive(self.afs_path, out)
        self.assertFalse(out.exists())

    def test_truncated_entry_fails_before_writing(self) -> None:
        data = build_afs(FILES, "none")
        self.afs_path.write_bytes(data[: 0x1800 + 10])  # corta la entrada #2
        out = self.root / "out"
        with self.assertRaisesRegex(AfsTruncatedError, "#2"):
            extract_archive(self.afs_path, out)
        self.assertFalse(out.exists())


class OutputGuardTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.afs_path = self.root / "sample.afs"
        self.afs_path.write_bytes(build_afs(FILES))

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def test_extract_refuses_any_other_repo(self) -> None:
        other = self.root / "other"
        (other / ".git").mkdir(parents=True)
        with self.assertRaises(UnsafeOutputError):
            extract_archive(self.afs_path, other / "extracted")
        self.assertFalse((other / "extracted").exists())

    def test_manifest_is_written_durably(self) -> None:
        with mock.patch("ngb_tools._paths.os.fsync", wraps=os.fsync) as fsync:
            extract_archive(self.afs_path, self.root / "out", [0])
        self.assertTrue(fsync.called)

    def test_stale_partial_from_a_previous_crash_is_replaced(self) -> None:
        out = self.root / "out"
        out.mkdir()
        stale = out / ("0000_menu.xpr" + PARTIAL_SUFFIX)
        stale.write_bytes(b"corte anterior")
        extract_archive(self.afs_path, out, [0])
        self.assertEqual((out / "0000_menu.xpr").read_bytes(), FILES[0][1])
        self.assertFalse(stale.exists())


class DefaultDirTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.data = Path(self.tmp.name) / "ngb"
        self.env = {"NGB_DATA": str(self.data)}

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def test_top_level_afs_goes_to_extracted_stem(self) -> None:
        afs = self.data / "game" / "cmn.afs"
        self.assertEqual(default_extract_dir(afs, self.env), self.data / "extracted" / "cmn")

    def test_same_stem_in_subfolders_keeps_the_relative_path(self) -> None:
        dirs = {
            default_extract_dir(self.data / "game" / sub / "cmn.afs", self.env)
            for sub in ("Media", "movie")
        }
        expected = {self.data / "extracted" / sub / "cmn" for sub in ("Media", "movie")}
        self.assertEqual(dirs, expected)

    def test_afs_outside_the_data_root_uses_its_stem(self) -> None:
        afs = Path(self.tmp.name) / "elsewhere" / "cmn.afs"
        self.assertEqual(default_extract_dir(afs, self.env), self.data / "extracted" / "cmn")

    def test_data_root_defaults_to_home(self) -> None:
        expected = Path.home() / "ngb-data" / "extracted" / "cmn"
        self.assertEqual(default_extract_dir(Path("x") / "cmn.afs", {}), expected)


if __name__ == "__main__":
    unittest.main()
