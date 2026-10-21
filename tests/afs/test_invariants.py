"""Tests de la auditoría de invariantes y de la semántica del campo `custom`."""

from __future__ import annotations

import io
import struct
import unittest

from ngb_tools.afs import open_afs
from ngb_tools.afs.invariants import (
    CustomFieldStats,
    IssueCode,
    custom_field_semantics,
    validate_archive,
)

from .synthetic import FILES, build_afs


def codes_of(data: bytes) -> set[IssueCode]:
    return {issue.code for issue in validate_archive(open_afs(io.BytesIO(data), "t.afs"))}


class ValidationTests(unittest.TestCase):
    def test_misaligned_overlap_and_bounds(self) -> None:
        buf = bytearray(build_afs(FILES, "none"))
        struct.pack_into("<II", buf, 8 + 8 * 1, 0x810, 0x100)  # desalineada (y pisa #0)
        struct.pack_into("<II", buf, 8 + 8 * 2, 0x800, len(buf))  # pisa #0 y se sale del archivo
        codes = codes_of(bytes(buf))
        self.assertLessEqual(
            {IssueCode.MISALIGNED, IssueCode.OVERLAP, IssueCode.OUT_OF_BOUNDS}, codes
        )

    def test_entry_containing_several_others_overlaps_each_of_them(self) -> None:
        buf = bytearray(build_afs(FILES, "none"))
        struct.pack_into("<II", buf, 8, 0x800, 0x2000)  # #0 [0x800,0x2800) envuelve a #1 y #2
        issues = validate_archive(open_afs(io.BytesIO(bytes(buf)), "t.afs"))
        overlaps = {i.index: i.detail for i in issues if i.code is IssueCode.OVERLAP}
        self.assertEqual(set(overlaps), {1, 2})
        self.assertTrue(overlaps[2].startswith("0 [0x800,0x2800) solapa con 2"))

    def test_well_formed_archive_has_no_issues(self) -> None:
        for mode in ("after_toc", "before_data", "none"):
            with self.subTest(mode=mode):
                self.assertEqual(codes_of(build_afs(FILES, mode)), set())

    def test_variant_magic_is_reported(self) -> None:
        buf = bytearray(build_afs(FILES))
        buf[3] = 0x20
        self.assertEqual(codes_of(bytes(buf)), {IssueCode.MAGIC_VARIANT})

    def test_entry_inside_header_is_reported(self) -> None:
        buf = bytearray(build_afs(FILES, "none"))
        struct.pack_into("<II", buf, 8, 0x10, 4)
        self.assertIn(IssueCode.INSIDE_HEADER, codes_of(bytes(buf)))

    def test_attr_table_overlapping_data_is_reported_without_index(self) -> None:
        buf = bytearray(build_afs(FILES, "after_toc"))
        third_offset = 0x800 * 3
        struct.pack_into("<II", buf, 8 + 8 * 3, third_offset + 0x10, 3 * 0x30 + 1)
        arc = open_afs(io.BytesIO(bytes(buf)), "t.afs")
        issues = validate_archive(arc)
        self.assertLessEqual(
            {IssueCode.ATTR_SIZE, IssueCode.ATTR_MISALIGNED, IssueCode.OVERLAP},
            {issue.code for issue in issues},
        )
        overlap = next(i for i in issues if i.code is IssueCode.OVERLAP)
        self.assertIsNone(overlap.index)
        self.assertIn("attr", overlap.detail)

    def test_impossible_date_is_reported(self) -> None:
        buf = bytearray(build_afs(FILES, "after_toc", date=(2004, 13, 40, 0, 0, 0)))
        self.assertEqual(codes_of(bytes(buf)), {IssueCode.BAD_DATE})

    def test_issue_renders_code_index_and_detail(self) -> None:
        buf = bytearray(build_afs(FILES, "none"))
        struct.pack_into("<II", buf, 8 + 8 * 1, 0x1810, 0x10)
        issues = validate_archive(open_afs(io.BytesIO(bytes(buf)), "t.afs"))
        self.assertEqual(str(issues[0]), "misaligned #1: offset 0x1810 no múltiplo de 0x800")


class CustomFieldTests(unittest.TestCase):
    def test_custom_field_semantics(self) -> None:
        data = build_afs(FILES)
        source = io.BytesIO(data)
        stats = custom_field_semantics(source, open_afs(source, "t.afs"))
        self.assertEqual(stats.equals_header_u32, 3)
        self.assertEqual(stats.neither, 0)

    def test_without_names_nothing_is_counted(self) -> None:
        source = io.BytesIO(build_afs(FILES, "none"))
        stats = custom_field_semantics(source, open_afs(source, "t.afs"))
        self.assertEqual(stats, CustomFieldStats(0, 0, 0))


if __name__ == "__main__":
    unittest.main()
