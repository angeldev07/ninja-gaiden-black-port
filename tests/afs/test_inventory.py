"""Tests de las fichas por archivo y de los agregados del inventario."""

from __future__ import annotations

import datetime as dt
import io
import struct
import tempfile
import unittest
from pathlib import Path

from ngb_tools.afs import AfsUsageError, open_afs
from ngb_tools.afs.inventory import (
    aggregate,
    build_inventory,
    inventory_archive,
    inventory_file,
    iter_flat_entries,
    name_extension,
)
from ngb_tools.afs.records import ArchiveRecord

from .synthetic import FILES, build_afs

INNER = build_afs([("a.xpr", b"XPR0" + bytes(28)), ("b.txt", b"plain text file\n" * 4)])
OUTER = build_afs([("inner.afs", INNER), ("raw", b"\x01\x00\x00\x00" * 8)], "before_data")


def fan_out(copies: int) -> bytes:
    """AFS cuya entrada #0 es INNER y las `copies` siguientes apuntan al mismo hijo."""
    files = [("inner.afs", INNER), *((f"dup{i}", b"x") for i in range(copies))]
    buf = bytearray(build_afs(files, "none"))
    toc0 = buf[8:16]
    for i in range(1, copies + 1):
        buf[8 + 8 * i : 16 + 8 * i] = toc0
    return bytes(buf)


def self_cycle() -> bytes:
    """AFS cuya entrada #0 cubre el archivo entero desde el offset 0: se contiene a sí mismo."""
    buf = bytearray(build_afs(FILES, "none"))
    struct.pack_into("<II", buf, 8, 0, len(buf))
    return bytes(buf)


def inventory_bytes(data: bytes, **limits: int) -> ArchiveRecord:
    """Ficha de un AFS en memoria; `limits` son los presupuestos (`max_records`, ...)."""
    source = io.BytesIO(data)
    return inventory_archive(source, open_afs(source, "t.afs"), **limits)


def with_null_tail(nulls: int) -> bytes:
    """FILES más `nulls` entradas nulas (0, 0) al final de la TOC."""
    files = [*FILES, *((f"n{i}", b"x") for i in range(nulls))]
    buf = bytearray(build_afs(files, "none"))
    for i in range(len(FILES), len(files)):
        struct.pack_into("<II", buf, 8 + 8 * i, 0, 0)
    return bytes(buf)


INNER_COST = 8 + 2 * 8 + 2 * (8 + 0x30)  # cabecera + 2 punteros + 2 x (TOC + atributos)


def all_refs(record: ArchiveRecord) -> list[str]:
    return [flat.ref for flat in iter_flat_entries([record])]


def issue_codes(record: ArchiveRecord) -> list[str]:
    return [issue["code"] for issue in record["issues"]]


class InventoryTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def test_inventory_with_nested(self) -> None:
        (self.root / "outer.afs").write_bytes(OUTER)
        (self.root / "plain.afs").write_bytes(build_afs(FILES, "none"))
        s = build_inventory(self.root)["summary"]
        self.assertEqual(s["totals"]["archives"], 2)
        self.assertEqual(s["totals"]["entries"], 5)
        self.assertEqual(s["totals"]["entries_all_levels"], 7)
        self.assertEqual(s["totals"]["named_entries"], 2)
        self.assertEqual(len(s["nested_afs"]), 1)
        self.assertEqual(s["nested_afs"][0]["count"], 2)
        kinds = {k["kind"]: k["entries"] for k in s["magic_histogram"]}
        self.assertEqual(kinds["XPR0"], 2)
        self.assertEqual(kinds["AFS"], 1)
        self.assertEqual(s["invariants"]["archives_ok"], 2)

    def test_nested_entries_follow_their_container(self) -> None:
        (self.root / "outer.afs").write_bytes(OUTER)
        archives = build_inventory(self.root)["archives"]
        refs = [(flat.ref, flat.depth) for flat in iter_flat_entries(archives)]
        expected = [("outer.afs#0", 0), ("outer.afs#0#0", 1), ("outer.afs#0#1", 1)]
        self.assertEqual(refs, [*expected, ("outer.afs#1", 0)])

    def test_unreadable_archive_is_recorded_not_raised(self) -> None:
        (self.root / "broken.afs").write_bytes(b"NOPE" + bytes(12))
        (self.root / "plain.afs").write_bytes(build_afs(FILES))
        inv = build_inventory(self.root)
        broken = inv["archives"][0]
        self.assertIn("magia", broken.get("error", ""))
        self.assertEqual([i["code"] for i in broken["issues"]], ["unreadable"])
        invariants = inv["summary"]["invariants"]
        self.assertEqual(invariants["archives_ok"], 1)
        self.assertEqual(invariants["issue_counts"], {"unreadable": 1})
        self.assertEqual(inv["summary"]["attr_pointer_locations"], {"error": 1, "after_toc": 1})
        (example,) = invariants["issue_examples"]["unreadable"]
        self.assertTrue(example.startswith("broken.afs: magia"), example)  # sin "#None"

    def test_record_carries_metadata_and_injected_clock(self) -> None:
        (self.root / "plain.afs").write_bytes(build_afs(FILES))
        inv = build_inventory(self.root, generated=dt.datetime(2026, 10, 20, 12, 0, 0))
        self.assertEqual(inv["generated"], "2026-10-20T12:00:00")
        self.assertEqual(inv["tool"], "ngb_tools.afs")
        self.assertEqual(inv["archives"][0]["file"], str(self.root / "plain.afs"))

    def test_kind_entropy_mean_averages_rounded_entropies(self) -> None:
        path = self.root / "plain.afs"
        path.write_bytes(build_afs([("a", b"ab" * 64), ("b", b"abcd" * 64)]))
        entries = inventory_file(path)["entries"]
        self.assertEqual([e["entropy"] for e in entries], [1.0, 2.0])
        (kind,) = build_inventory(self.root)["summary"]["magic_histogram"]
        self.assertEqual((kind["kind"], kind["entropy_mean"]), ("text", 1.5))

    def test_missing_root_is_a_usage_error(self) -> None:
        with self.assertRaises(AfsUsageError):
            build_inventory(self.root / "nope")


class NestingBudgetTests(unittest.TestCase):
    def test_child_shared_by_several_entries_is_inventoried_once(self) -> None:
        record = inventory_bytes(fan_out(copies=5))
        self.assertEqual(len(all_refs(record)), 6 + 2)
        self.assertEqual(sum("nested" in e for e in record["entries"]), 1)
        self.assertEqual(issue_codes(record).count("nested_revisit"), 5)
        self.assertIn("ya inventariado", record["entries"][3].get("nested_error", ""))

    def test_archive_that_contains_itself_is_not_reopened(self) -> None:
        record = inventory_bytes(self_cycle())
        self.assertEqual(all_refs(record), ["t.afs#0", "t.afs#1", "t.afs#2"])
        self.assertIn("nested_revisit", issue_codes(record))

    def test_record_cap_cuts_the_walk_and_reports_it_once(self) -> None:
        record = inventory_bytes(OUTER, max_records=3)
        self.assertEqual(all_refs(record), ["t.afs#0", "t.afs#0#0", "t.afs#0#1"])
        self.assertEqual(issue_codes(record).count("entry_budget"), 1)

    def test_cap_is_per_file_not_per_archive(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for name in ("a.afs", "b.afs"):
                (root / name).write_bytes(build_afs(FILES))
            records = [inventory_file(root / n, max_records=3) for n in ("a.afs", "b.afs")]
        self.assertEqual([len(r["entries"]) for r in records], [3, 3])
        self.assertEqual([r["issues"] for r in records], [[], []])

    def test_complete_walk_is_not_marked_truncated(self) -> None:
        self.assertNotIn("truncated", inventory_bytes(with_null_tail(2)))

    def test_cut_archive_is_truncated_and_counts_only_what_it_lists(self) -> None:
        record = inventory_bytes(with_null_tail(2), max_records=4)  # #0-#2 y una nula
        self.assertTrue(record.get("truncated"))
        self.assertEqual(record["null_entries"], 1)
        self.assertEqual(record["entries_bytes"], sum(len(data) for _, data in FILES))
        totals = aggregate([record])["totals"]
        self.assertEqual(totals["null_entries"], record["null_entries"])
        self.assertEqual(totals["entry_bytes"], record["entries_bytes"])


class MetadataBudgetTests(unittest.TestCase):
    def test_child_beyond_the_metadata_budget_is_not_opened(self) -> None:
        record = inventory_bytes(OUTER, max_metadata=INNER_COST - 1)
        self.assertNotIn("nested", record["entries"][0])
        self.assertIn("presupuesto", record["entries"][0].get("nested_error", ""))
        self.assertEqual(issue_codes(record).count("metadata_budget"), 1)
        self.assertTrue(record.get("truncated"))

    def test_budget_is_shared_by_all_children_of_the_file(self) -> None:
        two = build_afs([("a.afs", INNER), ("b.afs", INNER), ("c.afs", INNER)])
        record = inventory_bytes(two, max_metadata=INNER_COST * 3 // 2)
        self.assertEqual(["nested" in e for e in record["entries"]], [True, False, False])
        self.assertEqual(issue_codes(record).count("metadata_budget"), 1)

    def test_hostile_child_count_does_not_spend_the_budget(self) -> None:
        # Un hijo que declara 16M entradas en 64 bytes: su TOC no cabe y no se cobra, así que el
        # hijo real que viene detrás todavía se abre.
        hostile = b"AFS\x00" + (0xFFFFFF).to_bytes(4, "little") + bytes(56)
        outer = build_afs([("h.afs", hostile), ("b.afs", INNER)])
        record = inventory_bytes(outer, max_metadata=INNER_COST)
        self.assertNotIn("nested", record["entries"][0])
        self.assertIn("nested_error", record["entries"][0])
        self.assertIn("nested", record["entries"][1])
        self.assertNotIn("metadata_budget", issue_codes(record))

    def test_exact_budget_is_enough(self) -> None:
        record = inventory_bytes(OUTER, max_metadata=INNER_COST)
        self.assertIn("nested", record["entries"][0])
        self.assertNotIn("truncated", record)


class IoErrorTests(unittest.TestCase):
    def test_file_that_cannot_be_opened_is_recorded_as_unreadable(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            gone = Path(tmp) / "gone.afs"
            record = inventory_file(gone)
        self.assertEqual(issue_codes(record), ["unreadable"])
        self.assertEqual((record["size"], record["file"]), (0, str(gone)))
        self.assertIn("error", record)


class NameExtensionTests(unittest.TestCase):
    def test_extension_buckets(self) -> None:
        self.assertEqual(name_extension(None), "(sin nombre)")
        self.assertEqual(name_extension("readme"), "(sin extensión)")
        self.assertEqual(name_extension("BOFF_01.KMD"), ".kmd")


if __name__ == "__main__":
    unittest.main()
