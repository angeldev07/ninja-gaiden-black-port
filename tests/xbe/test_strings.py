"""Extracción de cadenas ASCII de las secciones y su clasificación heurística."""

from __future__ import annotations

import time
import unittest

from ngb_tools.xbe.image import XbeImage
from ngb_tools.xbe.strings import (
    MAX_CLASSIFIED_LEN,
    FoundString,
    StringCategory,
    StringSummary,
    classify_string,
    extract_strings,
    is_debug_message,
    is_path_like,
    looks_like_text,
    summarize_strings,
)

from .synthetic import (
    INSERTED_TEXT,
    SECTION_RAW,
    SECTION_VA,
    SECTION_VSIZE,
    UNTERMINATED_STRING,
    ExtraSection,
    build_synthetic_xbe,
)

# Racha hostil: texto plausible (pasa looks_like_text) sin separadores ni puntos.
HOSTILE_RUN = "abcdefghij" * 20_000


def summary_of(data: bytes) -> StringSummary:
    return summarize_strings(extract_strings(XbeImage.from_bytes(data)))


class ExtractionTests(unittest.TestCase):
    def test_strings(self) -> None:
        summary = summary_of(build_synthetic_xbe())
        texts = {name: [item.text for item in items] for name, items in summary.categories.items()}
        self.assertIn("src\\engine\\player.cpp", texts[StringCategory.SOURCE_PATHS])
        self.assertIn("assert failed: hp >= 0", texts[StringCategory.ASSERTS])
        self.assertIn("chr_ryu.afs", texts[StringCategory.ASSET_NAMES])
        self.assertIn("STG_TOWER_05", texts[StringCategory.ENGINE_IDENTIFIERS])
        self.assertTrue(any("Ver.9.50" in t for t in texts[StringCategory.VERSION_STAMPS]))
        self.assertEqual(
            summary.categories[StringCategory.RTTI_TYPES][0].demangled, "Game::CPlayer"
        )
        self.assertEqual(summary.identifier_prefixes, {"STG": 1})

    def test_unterminated_strings_are_counted_but_not_classified(self) -> None:
        summary = summary_of(build_synthetic_xbe())
        junk = UNTERMINATED_STRING.decode("ascii")
        self.assertEqual(summary.total - summary.nul_terminated, 1)
        for items in summary.categories.values():
            self.assertNotIn(junk, [item.text for item in items])

    def test_inserted_file_sections_are_skipped(self) -> None:
        found = extract_strings(XbeImage.from_bytes(build_synthetic_xbe(inserted_file=True)))
        self.assertNotIn(INSERTED_TEXT.decode("ascii"), [item.text for item in found])
        self.assertEqual({item.section for item in found}, {".text"})

    def test_min_len_filters_short_runs(self) -> None:
        image = XbeImage.from_bytes(build_synthetic_xbe())
        texts = [item.text for item in extract_strings(image, min_len=13)]
        self.assertNotIn("chr_ryu.afs", texts)  # 11 caracteres
        self.assertIn("src\\engine\\player.cpp", texts)

    def test_min_len_must_be_positive(self) -> None:
        # Con 0, la expresión regular aceptaría la cadena vacía en cada byte.
        with self.assertRaises(ValueError):
            extract_strings(XbeImage.from_bytes(build_synthetic_xbe()), min_len=0)

    def test_run_reaching_end_of_section_is_not_terminated(self) -> None:
        image = XbeImage.from_bytes(build_synthetic_xbe(text_tail=b"TAIL_RUN_NO_NUL"))
        (tail,) = [item for item in extract_strings(image) if item.text == "TAIL_RUN_NO_NUL"]
        self.assertFalse(tail.terminated)

    def test_section_with_zero_virtual_size_is_not_scanned(self) -> None:
        # Sin tamaño virtual no se carga nada, aunque declare datos crudos.
        extra = ExtraSection(b"EMPTY", SECTION_VA + SECTION_VSIZE, 0, SECTION_RAW, 0x40)
        found = extract_strings(XbeImage.from_bytes(build_synthetic_xbe(extra_sections=(extra,))))
        self.assertNotIn("EMPTY", {item.section for item in found})

    def test_va_is_section_relative(self) -> None:
        found = extract_strings(XbeImage.from_bytes(build_synthetic_xbe()))
        first = next(item for item in found if item.text == "src\\engine\\player.cpp")
        self.assertEqual(first.va, SECTION_VA + 0x10)


class SummaryTests(unittest.TestCase):
    def test_categories_are_deduplicated_by_text(self) -> None:
        found = [
            FoundString(".rdata", 0x100, "chr_ryu.afs", terminated=True),
            FoundString(".data", 0x200, "chr_ryu.afs", terminated=True),
        ]
        summary = summarize_strings(found)
        (item,) = summary.categories[StringCategory.ASSET_NAMES]
        self.assertEqual((item.section, item.va), (".rdata", 0x100))
        self.assertEqual((summary.total, summary.unique), (2, 1))
        self.assertEqual(summary.by_section, {".rdata": 1, ".data": 1})

    def test_by_section_is_read_only(self) -> None:
        summary = summarize_strings([FoundString(".rdata", 0, "chr_ryu.afs", terminated=True)])
        with self.assertRaises(TypeError):
            summary.by_section[".rdata"] = 2

    def test_hostile_run_is_counted_but_not_classified_quickly(self) -> None:
        start = time.perf_counter()
        summary = summarize_strings([FoundString(".rdata", 0, HOSTILE_RUN, terminated=True)])
        self.assertLess(time.perf_counter() - start, 1.0)
        self.assertEqual(summary.total, 1)
        self.assertEqual(sum(summary.category_counts.values()), 0)

    def test_classification_length_cap(self) -> None:
        long_message = "error in module " * 100  # 1600 caracteres: texto real largo
        too_long = "error in module " * (MAX_CLASSIFIED_LEN // 16 + 1)
        found = [
            FoundString(".rdata", 0x10, long_message, terminated=True),
            FoundString(".rdata", 0x20, too_long, terminated=True),
        ]
        (item,) = summarize_strings(found).categories[StringCategory.DEBUG_MESSAGES]
        self.assertEqual(item.va, 0x10)

    def test_identifier_prefixes_are_sorted_by_frequency(self) -> None:
        found = [
            FoundString(".rdata", 0x10 * i, text, terminated=True)
            for i, text in enumerate(("ITEM_SWORD", "STG_TOWER_05", "STG_HELL_01"))
        ]
        prefixes = summarize_strings(found).identifier_prefixes
        self.assertEqual(list(prefixes.items()), [("STG", 2), ("ITEM", 1)])


class HeuristicTests(unittest.TestCase):
    def test_x86_junk_is_not_a_path(self) -> None:
        for junk in (
            "D$\\PQR",
            "T$\\RPQ",
            "Active/d",
            "On/Off switch for the window",
            "gwv\\gwv_",
            "UTESE\\ESESE",
            "HTTP/1.1",
        ):
            self.assertFalse(is_path_like(junk), junk)
        for real in (
            "movie\\%s",
            "%s\\live00.afs",
            "t:\\$k\\msg\\",
            "/xstats/statpost.ashx",
            "media\\xbg\\cellwall.xbg",
        ):
            self.assertTrue(is_path_like(real), real)

    def test_path_regexes_are_linear_on_long_runs(self) -> None:
        start = time.perf_counter()
        self.assertFalse(is_path_like(HOSTILE_RUN))
        self.assertEqual(classify_string(HOSTILE_RUN[:MAX_CLASSIFIED_LEN]), [])
        self.assertLess(time.perf_counter() - start, 1.0)

    def test_path_matches_may_start_mid_token(self) -> None:
        # Tras 'ab\c', la ruta '\dev\file' empieza en el separador; y 'c:' tras una letra.
        self.assertTrue(is_path_like("ab\\c\\dev\\file"))
        self.assertTrue(is_path_like("xc:\\foo\\bar"))

    def test_graphics_junk_is_not_text(self) -> None:
        for junk in ("UUU\\UeU\\", "eWV\\eWV\\eWV\\eWV", "DDDDDDDD333/33/", "ffff\\gfUwwww"):
            self.assertFalse(looks_like_text(junk), junk)
        for real in ("\\Device\\Harddisk0\\Partition1\\", "panel20.xbx", "/xstats/statpost.ashx"):
            self.assertTrue(looks_like_text(real), real)

    def test_debug_message_vs_ui_text(self) -> None:
        self.assertTrue(is_debug_message("createfile error - st=%08x"))
        self.assertTrue(is_debug_message("E8101202 adxt_StartAfs: can't open "))
        self.assertFalse(is_debug_message("I can't kill my own sister..."))
        self.assertFalse(is_debug_message("Save process failed. Press @BA to continue."))


if __name__ == "__main__":
    unittest.main()
