"""Full analysis: structures located by VA starting from the header."""

from __future__ import annotations

import hashlib
import struct
import unittest

from ngb_tools.xbe.analysis import XbeAnalysis, analyze, guess_xdk_build
from ngb_tools.xbe.errors import ImageLayoutError
from ngb_tools.xbe.image import XbeImage
from ngb_tools.xbe.structs import LibraryVersion

from .synthetic import (
    BASE,
    CERT_OFF,
    CODEVIEW_VA,
    DEBUG_PATH,
    SECTION_RAW_SIZE,
    SECTION_VA,
    SIZE_OF_HEADERS,
    THUNK,
    THUNK_ORDINALS,
    XOR_KEYS,
    build_synthetic_xbe,
    with_u32,
)

END_OF_TEXT = SECTION_VA + SECTION_RAW_SIZE  # .text ends exactly at the end of the file


def analyze_bytes(data: bytes, *, with_strings: bool = False) -> XbeAnalysis:
    return analyze(XbeImage.from_bytes(data), with_strings=with_strings)


def library(name: str, build: int) -> LibraryVersion:
    return LibraryVersion(name=name, major=1, minor=0, build=build, flags=0)


class HeaderPointerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.analysis = analyze_bytes(build_synthetic_xbe())

    def test_debug_strings(self) -> None:
        dbg = self.analysis.debug_strings
        self.assertEqual((dbg.pathname, dbg.pathname_encoding), (DEBUG_PATH, "ascii"))
        self.assertEqual(dbg.filename, "Game.exe")
        self.assertEqual(dbg.unicode_filename, "Game.exe")

    def test_null_debug_pointers_give_none(self) -> None:
        data = build_synthetic_xbe()
        for field in (0x14C, 0x150, 0x154):
            data = with_u32(data, field, 0)
        dbg = analyze_bytes(data).debug_strings
        self.assertEqual(
            (dbg.pathname, dbg.pathname_encoding, dbg.filename, dbg.unicode_filename),
            (None, None, None, None),
        )

    def test_codeview_record(self) -> None:
        codeview = self.analysis.codeview
        assert codeview is not None
        self.assertEqual(self.analysis.image.header.debug_info_addr, CODEVIEW_VA)
        self.assertEqual(codeview.guid, "BAAFA588-83BB-468F-9250-0560D06EC3A6")
        self.assertEqual((codeview.age, codeview.pdb_path), (1, "c:\\Proj\\Game.pdb"))
        self.assertEqual(codeview.pdb_id, "BAAFA58883BB468F92500560D06EC3A61")

    def test_certificate_is_located_by_va(self) -> None:
        self.assertEqual(self.analysis.certificate.title_id_str, "TC-013")

    def test_file_digest_covers_whole_file(self) -> None:
        data = build_synthetic_xbe()
        self.assertEqual(analyze_bytes(data).sha256, hashlib.sha256(data).hexdigest())

    def test_tls_directory_when_present(self) -> None:
        data = bytearray(with_u32(build_synthetic_xbe(), 0x12C, BASE + 0x600))
        struct.pack_into("<6I", data, 0x600, 0, 0, 0x00012345, 0, 0x90, 0)
        tls = analyze_bytes(bytes(data)).tls
        assert tls is not None
        self.assertEqual((tls.index_addr, tls.size_of_zero_fill), (0x00012345, 0x90))
        self.assertIsNone(self.analysis.tls)


class CertificateLayoutTests(unittest.TestCase):
    def test_certificate_outside_headers_raises(self) -> None:
        # A backed VA is not enough: the certificate lives in the headers region.
        data = with_u32(build_synthetic_xbe(), 0x118, SECTION_VA)
        with self.assertRaises(ImageLayoutError):
            analyze_bytes(data)

    def test_certificate_crossing_end_of_headers_raises(self) -> None:
        data = with_u32(build_synthetic_xbe(), 0x118, BASE + SIZE_OF_HEADERS - 0x100)
        with self.assertRaises(ImageLayoutError):
            analyze_bytes(data)

    def test_declared_certificate_size_must_fit_in_headers(self) -> None:
        data = with_u32(build_synthetic_xbe(), CERT_OFF, 0x00100000)
        with self.assertRaises(ImageLayoutError):
            analyze_bytes(data)


class OptionalPointerTests(unittest.TestCase):
    def test_tls_near_end_of_file_is_absent(self) -> None:
        data = with_u32(build_synthetic_xbe(), 0x12C, END_OF_TEXT - 8)  # 16 bytes short
        self.assertIsNone(analyze_bytes(data).tls)

    def test_library_version_near_end_of_file_is_absent(self) -> None:
        data = with_u32(build_synthetic_xbe(), 0x168, END_OF_TEXT - 8)  # needs 16
        self.assertIsNone(analyze_bytes(data).kernel_library_version)

    def test_codeview_near_end_of_file_is_absent(self) -> None:
        data = bytearray(with_u32(build_synthetic_xbe(), 0x180, END_OF_TEXT - 8))
        data[-8:-4] = b"RSDS"
        self.assertIsNone(analyze_bytes(bytes(data)).codeview)

    def test_codeview_path_stops_at_end_of_region(self) -> None:
        tail = b"RSDS" + bytes(16) + bytes(4) + b"c:\\cut.pdb"  # no NUL: ends with the section
        data = build_synthetic_xbe(text_tail=tail, inserted_file=True)  # the file continues
        data = with_u32(data, 0x180, END_OF_TEXT - len(tail))
        codeview = analyze_bytes(data).codeview
        assert codeview is not None
        self.assertEqual(codeview.pdb_path, "c:\\cut.pdb")

    def test_kernel_thunk_in_bss_keeps_the_rest_of_the_analysis(self) -> None:
        bss_va = END_OF_TEXT + 0x20
        data = with_u32(build_synthetic_xbe(), 0x158, bss_va ^ XOR_KEYS["retail"][THUNK])
        analysis = analyze_bytes(data)
        self.assertIsNone(analysis.kernel)
        self.assertEqual(analysis.certificate.title_id_str, "TC-013")


class LibraryTests(unittest.TestCase):
    def test_library_version(self) -> None:
        analysis = analyze_bytes(build_synthetic_xbe())
        (lib,) = analysis.libraries
        self.assertEqual((lib.name, lib.version), ("XAPILIB", "1.0.5849"))
        self.assertEqual(analysis.xdk_build_guess, 5849)
        assert analysis.xapi_library_version is not None
        self.assertEqual(analysis.xapi_library_version.name, "XAPILIB")
        self.assertIsNone(analysis.kernel_library_version)

    def test_xdk_guess_prefers_xapilib_then_most_common(self) -> None:
        libs = [library("D3D8", 5788), library("XAPILIB", 5849), library("DSOUND", 5788)]
        self.assertEqual(guess_xdk_build(libs), 5849)
        self.assertEqual(guess_xdk_build(libs[:1] + libs[2:] + [library("XNET", 5849)]), 5788)
        self.assertIsNone(guess_xdk_build([]))

    def test_library_array_outside_headers_raises(self) -> None:
        data = with_u32(build_synthetic_xbe(), 0x164, 0x80000000)
        with self.assertRaises(ImageLayoutError):
            analyze_bytes(data)

    def test_huge_library_count_fails_fast(self) -> None:
        with self.assertRaises(ImageLayoutError):
            analyze_bytes(with_u32(build_synthetic_xbe(), 0x160, 0xFFFFFFFF))


class KernelAndStringsTests(unittest.TestCase):
    def test_kernel_imports_and_entry_point_section(self) -> None:
        analysis = analyze_bytes(build_synthetic_xbe())
        assert analysis.kernel is not None
        self.assertEqual([imp.ordinal for imp in analysis.kernel.imports], list(THUNK_ORDINALS))
        self.assertEqual(analysis.entry_point_section, ".text")

    def test_unknown_xor_keys_skip_kernel_imports(self) -> None:
        analysis = analyze_bytes(with_u32(build_synthetic_xbe(), 0x128, 0x12345678))
        self.assertIsNone(analysis.kernel)
        self.assertIsNone(analysis.entry_point_section)

    def test_strings_only_when_requested(self) -> None:
        self.assertIsNone(analyze_bytes(build_synthetic_xbe()).strings)
        summary = analyze_bytes(build_synthetic_xbe(), with_strings=True).strings
        assert summary is not None
        self.assertEqual(summary.nul_terminated, summary.total - 1)


if __name__ == "__main__":
    unittest.main()
