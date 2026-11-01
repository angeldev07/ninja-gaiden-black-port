"""Checkpoint library: finding saves, copying one, preparing a sandbox and the CLI."""

from __future__ import annotations

import contextlib
import io
import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from ngb_tools.checkpoint import library
from ngb_tools.checkpoint.cli import main
from ngb_tools.checkpoint.library import CheckpointError

TITLE = "5443000d"


def make_save(data: Path, folder: str, name: str, mtime: float) -> Path:
    udata = data / "runs" / "hdd" / "partition1" / "UDATA" / TITLE
    save = udata / folder
    save.mkdir(parents=True)
    (save / "save000.dat").write_bytes(b"\x00" * 64)
    (save / "SaveMeta.xbx").write_bytes(f"﻿Name={name}\r\nNoCopy=1\r\n".encode("utf-16-le"))
    (udata / "TitleMeta.xbx").write_bytes(b"meta")
    os.utime(save / "save000.dat", (mtime, mtime))
    return save


class LibraryTestCase(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.data = self.root / "data"
        self.lib = self.root / "lib"
        make_save(self.data, "AAAA", "1. CHAPTER 1 000:05", 1000)
        make_save(self.data, "BBBB", "5. CHAPTER 4 000:41", 2000)
        tdata = self.data / "runs" / "hdd" / "partition1" / "TDATA" / TITLE
        tdata.mkdir(parents=True)
        (tdata / "system.dat").write_bytes(b"profile")
        self.game = self.root / "game"
        self.game.mkdir()
        (self.game / "default.xbe").write_bytes(b"XBEH")

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def test_find_saves_newest_first_with_names(self) -> None:
        saves = library.find_saves(self.data)
        self.assertEqual([s.folder.name for s in saves], ["BBBB", "AAAA"])
        self.assertEqual(saves[0].name, "5. CHAPTER 4 000:41")
        self.assertEqual(library.find_saves(self.root / "nothing"), [])

    def test_add_copies_one_save_the_metadata_and_the_profile(self) -> None:
        cp = library.add("ch04", self.data, note="first fight", library=self.lib)
        udata = cp.path / "hdd" / "partition1" / "UDATA" / TITLE
        self.assertEqual(sorted(p.name for p in udata.iterdir()), ["BBBB", "TitleMeta.xbx"])
        self.assertTrue((cp.path / "hdd" / "partition1" / "TDATA" / TITLE / "system.dat").is_file())
        manifest = json.loads((cp.path / "checkpoint.json").read_text(encoding="utf-8"))
        self.assertEqual(manifest["save_name"], "5. CHAPTER 4 000:41")
        self.assertEqual(manifest["note"], "first fight")
        picked = library.add("ch01", self.data, save="aaaa", library=self.lib)
        self.assertEqual(picked.save_name, "1. CHAPTER 1 000:05")
        self.assertEqual([c.name for c in library.list_checkpoints(self.lib)], ["ch01", "ch04"])

    def test_add_refuses_bad_names_duplicates_and_missing_saves(self) -> None:
        library.add("ch04", self.data, library=self.lib)
        with self.assertRaises(CheckpointError):
            library.add("ch04", self.data, library=self.lib)
        library.add("ch04", self.data, library=self.lib, replace=True)
        for bad in ("../x", "", "a b"):
            with self.assertRaises(CheckpointError):
                library.add(bad, self.data, library=self.lib)
        with self.assertRaises(CheckpointError):
            library.add("x", self.data, save="CCCC", library=self.lib)

    def test_prepare_starts_fresh_with_only_that_save(self) -> None:
        library.add("ch04", self.data, library=self.lib)
        play = library.prepare("ch04", self.game, library=self.lib)
        saves = library.find_saves(play)
        self.assertEqual([s.folder.name for s in saves], ["BBBB"])
        self.assertTrue((play / "game" / "default.xbe").is_file())
        (saves[0].folder / "save000.dat").write_bytes(b"changed by a run")
        (play / "achievements.json").write_text("{}", encoding="utf-8")
        again = library.prepare("ch04", self.game, library=self.lib)
        self.assertEqual(
            (library.find_saves(again)[0].folder / "save000.dat").read_bytes(), b"\x00" * 64
        )
        self.assertFalse((again / "achievements.json").exists())
        self.assertTrue((self.game / "default.xbe").is_file())  # the link went, never the game
        with self.assertRaises(CheckpointError):
            library.prepare("ch04", self.root / "nogame", library=self.lib)

    def test_load_script_reaches_the_save(self) -> None:
        self.assertTrue(library.LOAD_SCRIPT.startswith("t=200p:START,"))
        self.assertIn("t=3000p:A,t=3500p:Y", library.LOAD_SCRIPT)
        last = max(int(item[2:].split("p:")[0]) for item in library.LOAD_SCRIPT.split(","))
        self.assertLess(last, library.GAMEPLAY_POLL)


class CliTestCase(LibraryTestCase):
    def run_cli(self, *argv: str) -> tuple[int, str, str]:
        out, err = io.StringIO(), io.StringIO()
        with (
            mock.patch.dict(os.environ, {"NGB_DATA": str(self.root / "ngb")}),
            contextlib.redirect_stdout(out),
            contextlib.redirect_stderr(err),
        ):
            code = main(list(argv))
        return code, out.getvalue(), err.getvalue()

    def test_saves_add_list_prepare(self) -> None:
        code, out, _ = self.run_cli("saves", "--data", str(self.data))
        self.assertEqual(code, 0)
        self.assertIn("BBBB  5443000d  5. CHAPTER 4 000:41", out)
        with mock.patch.object(library, "library_root", return_value=self.lib):
            self.assertEqual(
                self.run_cli("add", "ch04", "--data", str(self.data), "--note", "n")[0], 0
            )
            code, out, _ = self.run_cli("list")
            self.assertIn("ch04", out)
            code, out, _ = self.run_cli("prepare", "ch04", "--game", str(self.game))
            self.assertEqual(code, 0)
            self.assertIn("--input-script", out)
            code, _, err = self.run_cli("prepare", "nope", "--game", str(self.game))
            self.assertEqual(code, 2)
            self.assertIn("no checkpoint nope", err)

    def test_run_without_a_loader_is_a_usage_error(self) -> None:
        with mock.patch.object(library, "library_root", return_value=self.lib):
            self.run_cli("add", "ch04", "--data", str(self.data))
            code, _, err = self.run_cli(
                "run", "ch04", "--game", str(self.game), "--loader", str(self.root / "missing.exe")
            )
        self.assertEqual(code, 2)
        self.assertIn("no loader", err)


if __name__ == "__main__":
    unittest.main()
