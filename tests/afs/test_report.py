"""Tests of the presentation: `list` listing, Markdown report and JSON."""

from __future__ import annotations

import io
import json
import struct
import tempfile
import unittest
from pathlib import Path

from ngb_tools.afs import open_afs
from ngb_tools.afs.inventory import build_inventory
from ngb_tools.afs.records import InventoryRecord
from ngb_tools.afs.report import format_listing, render_json, render_markdown

from .synthetic import FILES, build_afs


def inventory_of(files: dict[str, bytes]) -> InventoryRecord:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        for name, data in files.items():
            (root / name).write_bytes(data)
        return build_inventory(root)


class ListingTests(unittest.TestCase):
    def test_listing(self) -> None:
        lines = format_listing(open_afs(io.BytesIO(build_afs(FILES)), "test.afs"))
        self.assertIn("3 entries", lines[0])
        self.assertIn("menu.xpr", lines[2])
        self.assertIn("0x00000800", lines[2])

    def test_listing_without_names_uses_dashes(self) -> None:
        lines = format_listing(open_afs(io.BytesIO(build_afs(FILES, "none")), "t.afs"))
        self.assertIn("attributes=none", lines[0])
        self.assertEqual(lines[2].split()[-2:], ["-", "-"])  # no date and no name


class MarkdownTests(unittest.TestCase):
    def test_all_sections_are_rendered(self) -> None:
        md = render_markdown(inventory_of({"a.afs": build_afs(FILES)}))
        for heading in (
            "# AFS inventory",
            "## Totals",
            "## Type histogram (top 40)",
            "## Entropy",
            "## Nested AFS files",
            "## Name extensions",
            "## Invariants",
            "## Files",
        ):
            with self.subTest(heading=heading):
                self.assertIn(f"\n{heading}\n", f"\n{md}")
        self.assertIn("- none", md)
        self.assertIn("| a.afs | 0.01 | 3 | yes | after_toc | 0 |", md)

    def test_issues_are_listed_with_examples(self) -> None:
        buf = bytearray(build_afs(FILES, "none"))
        struct.pack_into("<II", buf, 8 + 8 * 1, 0x1810, 0x10)
        md = render_markdown(inventory_of({"bad.afs": bytes(buf)}))
        self.assertIn("- AFS files without issues: 0/1.", md)
        self.assertIn("- `misaligned`: 1. E.g.: bad.afs#1: offset 0x1810", md)


CONTROL_NAME = "a\x01b\x7fc|d.bin"  # entry name with control characters and a pipe


def has_raw_controls(text: str) -> bool:
    return any((c < " " and c != "\n") or c == "\x7f" for c in text)


class ControlCharacterTests(unittest.TestCase):
    def test_listing_escapes_control_characters(self) -> None:
        data = build_afs([(CONTROL_NAME, b"plain text here\n")])
        lines = format_listing(open_afs(io.BytesIO(data), "t\x1b.afs"))
        self.assertIn("t\\x1b.afs", lines[0])
        self.assertTrue(lines[2].endswith("a\\x01b\\x7fc|d.bin"))
        self.assertFalse(has_raw_controls("\n".join(lines)))

    def test_markdown_escapes_control_characters_and_pipes(self) -> None:
        md = render_markdown(inventory_of({"a.afs": build_afs([(CONTROL_NAME, b"plain text\n")])}))
        self.assertIn("a\\x01b\\x7fc\\|d.bin", md)
        self.assertFalse(has_raw_controls(md))


class JsonTests(unittest.TestCase):
    def test_json_round_trips_and_keeps_accents(self) -> None:
        inv = inventory_of({"a.afs": build_afs([("readme", b"plain text here\n")])})
        inv["root"] = "caf\u00e9"
        text = render_json(inv)
        self.assertTrue(text.endswith("\n"))
        self.assertIn("caf\u00e9", text)
        self.assertEqual(json.loads(text), inv)


if __name__ == "__main__":
    unittest.main()
