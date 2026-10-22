"""Reports: typed JSON (addresses in hex) and Markdown summary."""

from __future__ import annotations

import json
import unittest

from ngb_tools.xbe.analysis import XbeAnalysis, analyze
from ngb_tools.xbe.image import XbeImage
from ngb_tools.xbe.markdown import md_code, render_markdown
from ngb_tools.xbe.report import build_report, render_json

from .synthetic import build_synthetic_xbe, corrupt_text_section, with_u32

REPORT_KEYS = {
    "file",
    "image_header",
    "debug_strings",
    "xor",
    "certificate",
    "sections",
    "libraries",
    "xdk_build_guess",
    "kernel_library_version",
    "xapi_library_version",
    "tls",
    "kernel_imports",
    "codeview",
    "kernel_thunk",
    "entry_point_section",
}


def analysis_of(data: bytes, *, with_strings: bool = False) -> XbeAnalysis:
    return analyze(XbeImage.from_bytes(data, "synthetic.xbe"), with_strings=with_strings)


class JsonReportTests(unittest.TestCase):
    def test_addresses_are_hex_and_sizes_are_ints(self) -> None:
        report = build_report(analysis_of(build_synthetic_xbe()))
        header, section = report["image_header"], report["sections"][0]
        self.assertEqual(header["base_address"], "0x00010000")
        self.assertEqual(header["size_of_headers"], 0x1000)
        self.assertEqual((section["flags"], section["virtual_size"]), ("0x00000006", 0x400))
        self.assertEqual(report["kernel_imports"][0]["slot_va"], "0x00011100")
        self.assertEqual(
            report["kernel_thunk"],
            {"va": "0x00011100", "file_offset": "0x00001100", "section": ".text"},
        )

    def test_top_level_keys(self) -> None:
        self.assertEqual(set(build_report(analysis_of(build_synthetic_xbe()))), REPORT_KEYS)
        with_strings = build_report(analysis_of(build_synthetic_xbe(), with_strings=True))
        self.assertEqual(set(with_strings), REPORT_KEYS | {"strings"})

    def test_counts_keyed_by_free_text_stay_ints(self) -> None:
        # An identifier prefix ending in "va" is not an address.
        data = build_synthetic_xbe(extra_strings=(b"nova_ring_01",))
        strings = build_report(analysis_of(data, with_strings=True))["strings"]
        self.assertEqual(strings["identifier_prefixes"], {"STG": 1, "nova": 1})
        self.assertEqual(strings["category_counts"]["engine_identifiers"], 2)

    def test_only_rtti_items_carry_demangled_name(self) -> None:
        strings = build_report(analysis_of(build_synthetic_xbe(), with_strings=True))["strings"]
        (rtti,) = strings["categories"]["rtti_types"]
        self.assertEqual(rtti["demangled"], "Game::CPlayer")
        self.assertEqual(rtti["va"][:2], "0x")
        for item in strings["categories"]["asset_names"]:
            self.assertNotIn("demangled", item)

    def test_unknown_xor_keys_give_nulls(self) -> None:
        data = with_u32(build_synthetic_xbe(), 0x128, 0x12345678)
        report = build_report(analysis_of(data))
        self.assertEqual(report["xor"]["build_kind"], "unknown")
        self.assertIsNone(report["xor"]["entry_point"])
        self.assertEqual(report["kernel_imports"], [])
        self.assertIsNone(report["kernel_thunk"])
        self.assertIsNone(report["entry_point_section"])

    def test_render_json_round_trips_with_unicode(self) -> None:
        text = render_json(analysis_of(build_synthetic_xbe()))
        self.assertTrue(text.endswith("}\n"))
        self.assertIn('"approved_text": "yes"', text)
        self.assertEqual(json.loads(text)["file"]["name"], "synthetic.xbe")


class MarkdownTests(unittest.TestCase):
    def test_md_code_escapes_table_breakers(self) -> None:
        self.assertEqual(md_code("a|b\nc"), "`a\\|b\\nc`")
        self.assertEqual(md_code("x`y"), "`` x`y ``")
        self.assertEqual(md_code(None), "—")
        self.assertEqual(md_code(""), "—")

    def test_summary_contents(self) -> None:
        markdown = render_markdown(analysis_of(build_synthetic_xbe()))
        self.assertTrue(markdown.startswith("# XBE: synthetic.xbe\n"))
        self.assertIn("**TC-013**", markdown)
        self.assertIn("`DbgPrint`", markdown)
        self.assertIn("| 0 | `.text` | PRELOAD EXECUTABLE |", markdown)
        self.assertIn("(encoded 0xA8FD47AB, section `.text`) |", markdown)
        self.assertNotIn("## Strings", markdown)

    def test_tampered_section_is_flagged(self) -> None:
        markdown = render_markdown(analysis_of(corrupt_text_section(build_synthetic_xbe())))
        self.assertIn("| 0x200 | BAD |", markdown)

    def test_strings_section_when_requested(self) -> None:
        markdown = render_markdown(analysis_of(build_synthetic_xbe(), with_strings=True))
        self.assertIn("## Strings", markdown)
        self.assertIn("`Game::CPlayer`", markdown)

    def test_unknown_xor_keys_render(self) -> None:
        data = with_u32(build_synthetic_xbe(), 0x128, 0x12345678)
        markdown = render_markdown(analysis_of(data))
        self.assertIn("| Build type (XOR) | **unknown** |", markdown)
        self.assertIn("## Kernel imports (0)", markdown)


if __name__ == "__main__":
    unittest.main()
