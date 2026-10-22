"""Gallery tests over a synthetic game: traversal, motions, failures and outputs."""

from __future__ import annotations

import contextlib
import io
import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from afs.synthetic import build_afs
from kmd.gltfcheck import load_glb, validate
from kmd.synthetic import build_kmd, parent_and_child
from kmd.synthetic_motion import build_bank, chr_dat, humanoid, standing_motion
from ngb_tools.gallery import build as build_module
from ngb_tools.gallery.build import Status, build_character, idle_id
from ngb_tools.gallery.cli import ExitCode, main
from ngb_tools.gallery.font import GLYPHS, draw_text, text_width
from ngb_tools.gallery.raster import Triangle, render_front
from ngb_tools.gallery.sources import (
    EntryRef,
    MotionIndex,
    list_entries,
    main_kmd,
    motion_candidates,
)
from ngb_tools.kmd.motion import parse_motion_bank

REPO = Path(__file__).resolve().parents[2]
TILE = (32, 40)
# Bank with a filler id 0 (4 frames) and the idle at id 1.
BANK = chr_dat(build_bank([standing_motion(4), standing_motion(2)], [0, 1]))


def write_game(root: Path) -> Path:
    game = root / "game"
    game.mkdir()
    files = {
        "chr_hero.afs": [("hero.kmd", build_kmd(humanoid())), ("hero.mtn", BANK)],
        "chr_twin.afs": [("notes.bin", b"x" * 8), ("other.kmd", build_kmd(parent_and_child()))],
        "chr_rigless.afs": [("rigless.kmd", build_kmd(parent_and_child())), ("rigless.mtn", BANK)],
        "chr_broken.afs": [("broken.kmd", b"garbage!" * 8)],
        "chr_empty.afs": [("readme.txt", b"nothing here")],
        "chr_ryuz.afs": [("ryuz.kmd", build_kmd(humanoid()))],
        "cmn.afs": [("ryu.mtn", BANK)],
    }
    for name, entries in files.items():
        (game / name).write_bytes(build_afs(entries))
    whole = build_afs([("trunc.kmd", build_kmd(humanoid()))])
    (game / "chr_trunc.afs").write_bytes(whole[: len(whole) // 3])
    return game


class SourceTests(unittest.TestCase):
    def ref(self, name: str, index: int) -> EntryRef:
        return EntryRef(Path("x.afs"), index, name, 0, 0)

    def test_main_kmd_prefers_the_character_name(self) -> None:
        entries = [self.ref("a.kmd", 0), self.ref("hero.kmd", 3), self.ref("hero.mtn", 4)]
        self.assertEqual(main_kmd(entries, "hero"), entries[1])
        self.assertEqual(main_kmd(entries, "nobody"), entries[0])
        self.assertIsNone(main_kmd([self.ref("x.mtn", 0)], "x"))

    def test_motion_candidates_cover_ryu_costumes(self) -> None:
        self.assertEqual(motion_candidates("ryua", "ryua"), ["ryua", "ryu"])
        self.assertEqual(motion_candidates("heri", "heli"), ["heri", "heli"])

    def test_idle_is_id_1_unless_it_is_the_filler(self) -> None:
        self.assertEqual(idle_id(parse_motion_bank(BANK)), 1)
        motions = [standing_motion(4), standing_motion(2)]
        self.assertEqual(idle_id(parse_motion_bank(build_bank(motions, [0, 0, 1]))), 2)
        self.assertEqual(idle_id(parse_motion_bank(build_bank(motions[:1], [0, 0]))), 0)


class GameCase(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.game = write_game(self.root)
        self.out = self.root / "out"

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def run_cli(self, *argv: str) -> tuple[int, str, str]:
        out, err = io.StringIO(), io.StringIO()
        with (
            mock.patch.dict(os.environ, {"NGB_DATA": str(self.root / "data")}),
            contextlib.redirect_stdout(out),
            contextlib.redirect_stderr(err),
        ):
            code = main(list(argv))
        return code, out.getvalue(), err.getvalue()


class MotionIndexTests(GameCase):
    def test_local_bank_first_then_common(self) -> None:
        index = MotionIndex([self.game / "cmn.afs"])
        hero = list_entries(self.game / "chr_hero.afs")
        self.assertEqual(index.find(hero, ["hero"]).archive.name, "chr_hero.afs")  # type: ignore[union-attr]
        found = index.find(list_entries(self.game / "chr_ryuz.afs"), ["ryuz", "ryu"])
        self.assertEqual((found.archive.name, found.name), ("cmn.afs", "ryu.mtn"))  # type: ignore[union-attr]
        self.assertIsNone(index.find([], ["nobody"]))


class BuildTests(GameCase):
    def setUp(self) -> None:
        super().setUp()
        self.code, self.stdout, _ = self.run_cli(
            "build", "--game", str(self.game), "--out", str(self.out)
        )
        self.doc = json.loads((self.out / "gallery.json").read_text(encoding="utf-8"))
        self.rows = {row["character"]: row for row in self.doc["entries"]}

    def test_failures_do_not_abort_and_the_exit_code_reports_them(self) -> None:
        self.assertEqual(self.code, ExitCode.FINDINGS)
        self.assertEqual((self.doc["characters"], self.doc["exported"]), (7, 4))
        self.assertEqual(self.doc["animated"], 2)

    def test_each_failure_keeps_its_reason(self) -> None:
        self.assertTrue(self.rows["broken"]["reason"].startswith("KmdFormatError"))
        self.assertEqual(self.rows["empty"]["reason"], "no KMD in the AFS")
        self.assertTrue(self.rows["trunc"]["reason"].startswith("unreadable AFS"))

    def test_animated_glb_with_the_idle(self) -> None:
        hero = self.rows["hero"]
        self.assertEqual(hero["status"], "ok")
        self.assertIn("hero.mtn id 1", hero["motion"])
        gltf, binary = load_glb((self.out / "hero.glb").read_bytes())
        self.assertEqual(validate(gltf, binary), [])
        self.assertEqual([a["name"] for a in gltf["animations"]], ["hero#1"])

    def test_costume_finds_the_common_bank(self) -> None:
        self.assertIn("cmn.afs", self.rows["ryuz"]["motion"])

    def test_without_bank_or_rig_the_glb_has_no_animation(self) -> None:
        self.assertEqual(self.rows["twin"]["kmd"], "other.kmd (#1)")
        self.assertEqual(self.rows["twin"]["motion_note"], "no motion bank")
        self.assertTrue(self.rows["rigless"]["motion_note"].startswith("no animation"))
        gltf, _ = load_glb((self.out / "rigless.glb").read_bytes())
        self.assertNotIn("animations", gltf)

    def test_contact_sheet_and_markdown(self) -> None:
        png = (self.out / "gallery.png").read_bytes()
        self.assertTrue(png.startswith(b"\x89PNG"))
        text = (self.out / "gallery.md").read_text(encoding="utf-8")
        self.assertIn("4 of 7 exported to GLB, 2 with animated idle", text)
        self.assertIn("| chr_hero.afs | hero.kmd (#0) |", text)
        self.assertIn("## Failures by cause", text)
        self.assertIn("**no KMD in the AFS** (1): empty", text)


class RobustnessTests(GameCase):
    def test_unexpected_exception_is_recorded_as_internal_error(self) -> None:
        def boom(_: bytes) -> None:
            raise RuntimeError("bug")

        with mock.patch.object(build_module, "parse_kmd", boom):
            result = build_character(self.game / "chr_hero.afs", MotionIndex([]), self.out, TILE)
        self.assertIs(result.entry.status, Status.FAILED)
        self.assertEqual(result.entry.reason, "internal error RuntimeError: bug")
        self.assertIsNone(result.tile)

    def test_only_selects_and_unknown_names_are_usage_errors(self) -> None:
        code, out, _ = self.run_cli(
            "build", "--game", str(self.game), "--out", str(self.out), "--only", "chr_hero"
        )
        self.assertEqual(code, ExitCode.OK, out)
        self.assertIn("1 of 1 exported", out)
        code, _, err = self.run_cli(
            "build", "--game", str(self.game), "--out", str(self.out), "--only", "nobody"
        )
        self.assertEqual(code, ExitCode.ERROR)
        self.assertIn("nobody", err)

    def test_output_inside_the_repo_and_missing_game_are_errors(self) -> None:
        code, _, _ = self.run_cli("build", "--game", str(self.game), "--out", str(REPO / "galx"))
        self.assertEqual(code, ExitCode.ERROR)
        self.assertFalse((REPO / "galx").exists())
        code, _, _ = self.run_cli(
            "build", "--game", str(self.root / "nope"), "--out", str(self.out)
        )
        self.assertEqual(code, ExitCode.ERROR)


class DrawingTests(unittest.TestCase):
    def test_text_width_and_glyph_pixels(self) -> None:
        self.assertEqual(text_width("AB"), 11)
        self.assertEqual(text_width("AB", 2), 22)
        rgb = bytearray(3 * 6 * 7)
        draw_text(rgb, 6, 0, 0, "a", color=(255, 0, 0))
        painted = sum(1 for i in range(0, len(rgb), 3) if rgb[i] == 255)
        self.assertEqual(painted, sum(bin(row).count("1") for row in GLYPHS["A"]))

    def test_unknown_characters_use_the_question_mark(self) -> None:
        a, b = bytearray(3 * 6 * 7), bytearray(3 * 6 * 7)
        draw_text(a, 6, 0, 0, "\u00f1", color=(9, 9, 9))
        draw_text(b, 6, 0, 0, "?", color=(9, 9, 9))
        self.assertEqual(a, b)

    def test_render_front_fills_the_triangle_and_keeps_the_background(self) -> None:
        tri = Triangle(((0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0)), None, -1)
        rgb = render_front([tri], {}, 20, 20, (1, 2, 3))
        background = sum(1 for i in range(0, len(rgb), 3) if rgb[i : i + 3] == b"\x01\x02\x03")
        self.assertTrue(0 < background < 400)
        self.assertEqual(render_front([], {}, 4, 4, (1, 2, 3)), b"\x01\x02\x03" * 16)


if __name__ == "__main__":
    unittest.main()
