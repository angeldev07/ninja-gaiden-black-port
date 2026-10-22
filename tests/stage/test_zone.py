"""Tests of zone parts: lookup in the AFS files, selection and identical variants."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from afs.synthetic import build_afs
from ngb_tools.stage.errors import ZoneNotFoundError
from ngb_tools.stage.zone import check_zone_name, find_zone_parts, load_parts, select_parts

from .synthetic import SynthStageObject, build_stage_kmd, two_part_object

PART_A = build_stage_kmd([two_part_object()])
PART_B = build_stage_kmd([SynthStageObject()])
PART_C = build_stage_kmd([SynthStageObject(), SynthStageObject()])
PROP = build_stage_kmd([SynthStageObject()], collision=False)


def write_game(root: Path) -> Path:
    """`boff` with 3 part AFS files (03 equals 01), a `_cmn` and look-alike neighbors."""
    game = root / "game"
    game.mkdir()
    files = {
        "boff_01.afs": [("boff_01.kmd", PART_A), ("bf_pole.kmd", PROP), ("bf_pole.sob", b"x")],
        "boff_02.afs": [("boff_02.kmd", PART_B)],
        "boff_03.afs": [("boff_03.kmd", PART_A)],
        "boff_cmn.afs": [("bf_door.kmd", PROP), ("boff_99.kmd", PART_C)],
        "boffx_01.afs": [("boffx_01.kmd", PART_B)],
        "boff_a_01.afs": [("boff_a_01.kmd", PART_B)],
    }
    for name, entries in files.items():
        (game / name).write_bytes(build_afs(entries))
    return game


class ZoneTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.game = write_game(Path(self.tmp.name))

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def test_parts_are_the_numbered_kmd_of_the_zone(self) -> None:
        parts = find_zone_parts(self.game, "BOFF")
        self.assertEqual([p.name for p in parts], ["boff_01", "boff_02", "boff_03", "boff_99"])
        self.assertEqual([p.common for p in parts], [False, False, False, True])
        self.assertEqual(parts[3].entry.index, 1)

    def test_selection_by_number(self) -> None:
        parts = find_zone_parts(self.game, "boff")
        self.assertEqual(
            [p.name for p in select_parts(parts, ["2", "boff_99"])], ["boff_02", "boff_99"]
        )
        self.assertEqual(len(select_parts(parts, None)), 4)
        with self.assertRaisesRegex(ZoneNotFoundError, "no part"):
            select_parts(parts, ["07"])
        with self.assertRaisesRegex(ZoneNotFoundError, "invalid part"):
            select_parts(parts, ["one"])

    def test_identical_parts_are_marked_as_duplicates(self) -> None:
        loaded = load_parts(find_zone_parts(self.game, "boff"))
        self.assertEqual([item.duplicate_of for item in loaded], [None, None, "boff_01", None])
        self.assertEqual(loaded[0].data, PART_A)

    def test_unknown_zone(self) -> None:
        with self.assertRaisesRegex(ZoneNotFoundError, "no AFS"):
            find_zone_parts(self.game, "tomb")

    def test_zone_name_cannot_be_a_path(self) -> None:
        for bad in ("../boff", "boff/..", "c:boff", "boff_", ""):
            with self.subTest(bad=bad), self.assertRaises(ZoneNotFoundError):
                check_zone_name(bad)
        self.assertEqual(check_zone_name("Tln_A"), "tln_a")


if __name__ == "__main__":
    unittest.main()
