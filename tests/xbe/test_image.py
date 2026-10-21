"""Imagen cargada: mapa VA -> archivo, claves XOR, digests y validación."""

from __future__ import annotations

import tempfile
import time
import unittest
from pathlib import Path

from ngb_tools.xbe.errors import ImageLayoutError, UnmappedAddressError, XbeFormatError
from ngb_tools.xbe.image import MAX_SECTIONS, BuildKind, XbeImage

from .synthetic import (
    BASE,
    CERT_OFF,
    DEBUG_UFILE_OFF,
    ENTRY,
    ENTRY_VA,
    INSERTED_TEXT,
    SECTION_HEADERS_OFF,
    SECTION_RAW,
    SECTION_RAW_SIZE,
    SECTION_VA,
    SECTION_VSIZE,
    SIZE_OF_HEADERS,
    SIZE_OF_IMAGE,
    THUNK,
    THUNK_OFF,
    THUNK_VA,
    XOR_KEYS,
    ExtraSection,
    alias_under,
    build_synthetic_xbe,
    corrupt_text_section,
    with_u32,
)

WHOLE_ADDRESS_SPACE = 0x100000000 - BASE  # size_of_image que llega justo a 2^32


class XorDecodingTests(unittest.TestCase):
    def check_kind(self, kind: str) -> None:
        xor = XbeImage.from_bytes(build_synthetic_xbe(kind)).xor
        self.assertEqual(xor.build_kind, kind)
        self.assertEqual(xor.entry_point, ENTRY_VA)
        self.assertEqual(xor.kernel_thunk_addr, THUNK_VA)
        self.assertFalse(xor.ambiguous)

    def test_retail(self) -> None:
        self.check_kind("retail")

    def test_debug(self) -> None:
        self.check_kind("debug")

    def test_unknown_keys(self) -> None:
        data = with_u32(with_u32(build_synthetic_xbe(), 0x128, 0x12345678), 0x158, 0x9ABCDEF0)
        xor = XbeImage.from_bytes(data).xor
        self.assertEqual(xor.build_kind, BuildKind.UNKNOWN)
        self.assertIsNone(xor.entry_point)
        self.assertIsNone(xor.kernel_thunk_addr)

    def test_two_plausible_key_sets_give_unknown(self) -> None:
        # Secciones en las VA que daría la clave debug: ya no hay una sola candidata.
        debug_entry = alias_under("debug", ENTRY_VA, ENTRY)
        debug_thunk = alias_under("debug", THUNK_VA, THUNK)
        extras = (
            ExtraSection(b"ALTENT", debug_entry, 0x10, SECTION_RAW, 0x10, flags=0x04),
            ExtraSection(b"ALTTHK", debug_thunk, 0x10, THUNK_OFF, 0x10, flags=0x00),
        )
        data = with_u32(build_synthetic_xbe(extra_sections=extras), 0x10C, WHOLE_ADDRESS_SPACE)
        image = XbeImage.from_bytes(data)
        self.assertTrue(image.xor.ambiguous)
        self.assertEqual(image.xor.build_kind, BuildKind.UNKNOWN)
        self.assertIsNone(image.xor.entry_point)
        (issue,) = image.validation_issues()
        self.assertIn("ambigu", issue)

    def test_entry_point_must_be_executable(self) -> None:
        image = XbeImage.from_bytes(build_synthetic_xbe(text_flags=0x02))  # PRELOAD solo
        self.assertEqual(image.xor.build_kind, BuildKind.UNKNOWN)
        (issue,) = image.validation_issues()
        self.assertIn("XOR", issue)

    def test_kernel_thunk_in_bss_is_rejected(self) -> None:
        bss_va = SECTION_VA + SECTION_RAW_SIZE + 0x20
        data = with_u32(build_synthetic_xbe(), 0x158, bss_va ^ XOR_KEYS["retail"][THUNK])
        image = XbeImage.from_bytes(data)
        self.assertEqual(image.xor.build_kind, BuildKind.UNKNOWN)
        self.assertEqual(len(image.validation_issues()), 1)


class AddressMapTests(unittest.TestCase):
    def setUp(self) -> None:
        self.amap = XbeImage.from_bytes(build_synthetic_xbe()).address_map

    def test_header_region(self) -> None:
        self.assertEqual(self.amap.to_offset(BASE + CERT_OFF), CERT_OFF)

    def test_section_region(self) -> None:
        self.assertEqual(self.amap.to_offset(THUNK_VA), THUNK_OFF)

    def test_bss_not_backed(self) -> None:
        self.assertIsNone(self.amap.to_offset(SECTION_VA + SECTION_RAW_SIZE + 4))
        self.assertEqual(self.amap.section_name(SECTION_VA + SECTION_RAW_SIZE + 4), ".text")

    def test_outside_image(self) -> None:
        self.assertIsNone(self.amap.to_offset(0x80000000))
        with self.assertRaises(UnmappedAddressError) as caught:
            self.amap.require_offset(0x80000000, "prueba")
        self.assertEqual(caught.exception.va, 0x80000000)

    def test_bytes_past_end_of_file_are_not_backed(self) -> None:
        image = XbeImage.from_bytes(build_synthetic_xbe()[: SECTION_RAW + 0x180])
        self.assertIsNone(image.address_map.to_offset(SECTION_VA + 0x180))
        self.assertIsNone(image.c_bytes_at(SECTION_VA + 0x1A0))
        self.assertEqual(image.address_map.backed_span(SECTION_VA + 0x170), (0x1170, 0x1180))

    def test_section_with_zero_virtual_size_backs_nothing(self) -> None:
        extra = ExtraSection(b"EMPTY", SECTION_VA + SECTION_VSIZE, 0, SECTION_RAW, 0x40)
        image = XbeImage.from_bytes(build_synthetic_xbe(extra_sections=(extra,)))
        self.assertIsNone(image.address_map.to_offset(SECTION_VA + SECTION_VSIZE))
        self.assertEqual(image.validation_issues(), ())


class SectionTests(unittest.TestCase):
    def test_section(self) -> None:
        (section,) = XbeImage.from_bytes(build_synthetic_xbe()).sections
        self.assertEqual(section.name, ".text")
        self.assertEqual(section.header.flags_decoded, ["PRELOAD", "EXECUTABLE"])
        self.assertEqual(
            (section.header.virtual_addr, section.header.raw_addr), (SECTION_VA, SECTION_RAW)
        )
        self.assertTrue(section.digest_ok)

    def test_tampered_section_fails_digest(self) -> None:
        (section,) = XbeImage.from_bytes(corrupt_text_section(build_synthetic_xbe())).sections
        self.assertIs(section.digest_ok, False)

    def test_section_beyond_end_of_file_is_unverifiable(self) -> None:
        truncated = build_synthetic_xbe()[: SECTION_RAW + SECTION_RAW_SIZE - 0x10]
        (section,) = XbeImage.from_bytes(truncated).sections
        self.assertIsNone(section.digest_ok)

    def test_unmapped_section_name_is_placeholder(self) -> None:
        data = with_u32(build_synthetic_xbe(), SECTION_HEADERS_OFF + 0x14, 0x80000000)
        self.assertEqual(XbeImage.from_bytes(data).sections[0].name, "?")


class SectionTableLayoutTests(unittest.TestCase):
    def test_section_table_outside_headers_raises(self) -> None:
        data = with_u32(build_synthetic_xbe(), 0x120, SECTION_VA)
        with self.assertRaises(ImageLayoutError):
            XbeImage.from_bytes(data)

    def test_section_table_must_end_inside_headers(self) -> None:
        # Empieza dentro de la cabecera, pero su única entrada cruza el final.
        data = with_u32(build_synthetic_xbe(), 0x120, BASE + SIZE_OF_HEADERS - 0x10)
        with self.assertRaises(ImageLayoutError):
            XbeImage.from_bytes(data)

    def test_huge_section_count_fails_fast(self) -> None:
        start = time.perf_counter()
        with self.assertRaises(ImageLayoutError):
            XbeImage.from_bytes(with_u32(build_synthetic_xbe(), 0x11C, 0xFFFFFFFF))
        self.assertLess(time.perf_counter() - start, 1.0)

    def test_section_count_cap(self) -> None:
        with self.assertRaises(ImageLayoutError):
            XbeImage.from_bytes(with_u32(build_synthetic_xbe(), 0x11C, MAX_SECTIONS + 1))

    def test_section_past_end_of_image_raises(self) -> None:
        extra = ExtraSection(b"PAST", BASE + SIZE_OF_IMAGE - 0x10, 0x20, SECTION_RAW, 0x10)
        with self.assertRaises(ImageLayoutError):
            XbeImage.from_bytes(build_synthetic_xbe(extra_sections=(extra,)))

    def test_section_below_base_raises(self) -> None:
        extra = ExtraSection(b"LOW", BASE - 0x1000, 0x10, SECTION_RAW, 0x10)
        with self.assertRaises(ImageLayoutError):
            XbeImage.from_bytes(build_synthetic_xbe(extra_sections=(extra,)))

    def test_section_wrapping_past_32_bits_raises(self) -> None:
        extra = ExtraSection(b"WRAP", 0xFFFFFF00, 0x200, SECTION_RAW, 0x10)
        data = with_u32(build_synthetic_xbe(extra_sections=(extra,)), 0x10C, WHOLE_ADDRESS_SPACE)
        with self.assertRaisesRegex(ImageLayoutError, "32 bits"):
            XbeImage.from_bytes(data)

    def test_image_wrapping_past_32_bits_raises(self) -> None:
        with self.assertRaises(ImageLayoutError):
            XbeImage.from_bytes(with_u32(build_synthetic_xbe(), 0x10C, 0xFFFFFFFF))

    def test_layout_errors_are_format_errors(self) -> None:
        self.assertTrue(issubclass(ImageLayoutError, XbeFormatError))


class VirtualReadTests(unittest.TestCase):
    def test_c_string_stops_at_end_of_backed_region(self) -> None:
        # Sin NUL al final de .text, la lectura no debe invadir la sección siguiente
        # del archivo: en memoria, tras los datos crudos solo hay BSS (ceros).
        image = XbeImage.from_bytes(build_synthetic_xbe(text_tail=b"TAILTEXT", inserted_file=True))
        tail_va = SECTION_VA + SECTION_RAW_SIZE - 8
        self.assertEqual(image.c_bytes_at(tail_va), b"TAILTEXT")
        self.assertNotIn(INSERTED_TEXT, image.c_bytes_at(tail_va) or b"")

    def test_unbacked_reads_return_none(self) -> None:
        image = XbeImage.from_bytes(build_synthetic_xbe())
        self.assertIsNone(image.c_bytes_at(SECTION_VA + SECTION_RAW_SIZE + 4))
        self.assertIsNone(image.utf16_at(0x80000000))

    def test_utf16_string_at_va(self) -> None:
        image = XbeImage.from_bytes(build_synthetic_xbe())
        self.assertEqual(image.utf16_at(BASE + DEBUG_UFILE_OFF), "Game.exe")


class ImageLoadingTests(unittest.TestCase):
    def test_load_names_image_after_file(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "synthetic.xbe"
            path.write_bytes(build_synthetic_xbe())
            image = XbeImage.load(path)
        self.assertEqual(image.name, "synthetic.xbe")
        self.assertEqual(len(image.data), SECTION_RAW + SECTION_RAW_SIZE)


class ValidationTests(unittest.TestCase):
    def test_valid_image_has_no_issues(self) -> None:
        self.assertEqual(XbeImage.from_bytes(build_synthetic_xbe()).validation_issues(), ())

    def test_digest_mismatch_is_an_issue(self) -> None:
        image = XbeImage.from_bytes(corrupt_text_section(build_synthetic_xbe()))
        (issue,) = image.validation_issues()
        self.assertIn(".text", issue)
        self.assertIn("SHA-1", issue)

    def test_unverifiable_digest_is_an_issue(self) -> None:
        image = XbeImage.from_bytes(build_synthetic_xbe()[: SECTION_RAW + 0x180])
        (issue,) = image.validation_issues()
        self.assertIn(".text", issue)

    def test_unknown_xor_keys_are_an_issue(self) -> None:
        image = XbeImage.from_bytes(with_u32(build_synthetic_xbe(), 0x128, 0x12345678))
        (issue,) = image.validation_issues()
        self.assertIn("XOR", issue)

    def test_overlapping_sections_are_an_issue(self) -> None:
        extra = ExtraSection(b"OVER", SECTION_VA + 0x100, 0x40, SECTION_RAW, 0x40, flags=0)
        image = XbeImage.from_bytes(build_synthetic_xbe(extra_sections=(extra,)))
        (issue,) = image.validation_issues()
        self.assertIn("'.text'", issue)
        self.assertIn("'OVER'", issue)

    def test_section_overlapping_headers_is_an_issue(self) -> None:
        extra = ExtraSection(b"HDR", BASE + 0xF00, 0x100, SECTION_RAW, 0x40, flags=0)
        image = XbeImage.from_bytes(build_synthetic_xbe(extra_sections=(extra,)))
        (issue,) = image.validation_issues()
        self.assertIn("cabeceras", issue)

    def test_adjacent_sections_do_not_overlap(self) -> None:
        extra = ExtraSection(b"NEXT", SECTION_VA + SECTION_VSIZE, 0x40, SECTION_RAW, 0x40)
        image = XbeImage.from_bytes(build_synthetic_xbe(extra_sections=(extra,)))
        self.assertEqual(image.validation_issues(), ())


if __name__ == "__main__":
    unittest.main()
