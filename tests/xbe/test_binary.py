"""Primitive reads: bounds-checked little-endian integers and text decoding."""

from __future__ import annotations

import unittest

from ngb_tools.xbe.binary import (
    decode_build_text,
    decode_utf16z,
    hex32,
    read_c_bytes,
    read_u16s,
    read_u32,
    read_u32s,
)
from ngb_tools.xbe.errors import TruncatedDataError, XbeFormatError


class IntegerReadTests(unittest.TestCase):
    def test_u32_is_little_endian(self) -> None:
        self.assertEqual(read_u32(b"\x00\x78\x56\x34\x12", 1), 0x12345678)

    def test_arrays_are_little_endian(self) -> None:
        self.assertEqual(read_u16s(b"\x01\x00\x34\x12", 0, 2), (1, 0x1234))
        self.assertEqual(read_u32s(b"\x02\x00\x00\x00\xff\xff\xff\xff", 0, 2), (2, 0xFFFFFFFF))

    def test_read_past_end_raises_truncated(self) -> None:
        with self.assertRaises(TruncatedDataError) as caught:
            read_u32(b"\x00" * 6, 4)
        self.assertEqual((caught.exception.offset, caught.exception.size), (4, 4))
        self.assertIsInstance(caught.exception, XbeFormatError)

    def test_negative_offset_raises_truncated(self) -> None:
        with self.assertRaises(TruncatedDataError):
            read_u32(b"\x00" * 8, -1)

    def test_c_bytes_stop_at_nul_or_limit(self) -> None:
        self.assertEqual(read_c_bytes(b"xxabc\0def", 2), b"abc")
        self.assertEqual(read_c_bytes(b"abcdef", 0, max_len=4), b"abcd")


class TextDecodingTests(unittest.TestCase):
    def test_shift_jis_build_path(self) -> None:
        raw = b"e:\\prog\\" + "修正版".encode("cp932") + b"\\a.exe"
        self.assertEqual(decode_build_text(raw), ("e:\\prog\\修正版\\a.exe", "cp932"))
        self.assertEqual(decode_build_text(b"c:\\Kunai")[1], "ascii")

    def test_undecodable_build_text_is_escaped(self) -> None:
        # 0x81 opens a Shift-JIS double-byte character that is never completed.
        self.assertEqual(decode_build_text(b"a\x81"), ("a\\x81", "ascii+escapes"))

    def test_utf16_stops_at_terminator_and_ignores_odd_byte(self) -> None:
        self.assertEqual(decode_utf16z("Kunai".encode("utf-16-le") + b"\0\0zz"), "Kunai")
        self.assertEqual(decode_utf16z("ab".encode("utf-16-le") + b"c"), "ab")

    def test_hex32_pads_to_eight_digits(self) -> None:
        self.assertEqual(hex32(0x10000), "0x00010000")
        self.assertEqual(hex32(0xA8FC57AB), "0xA8FC57AB")


if __name__ == "__main__":
    unittest.main()
