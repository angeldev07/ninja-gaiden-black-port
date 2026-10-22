"""Tests of opening an XPR0: standalone or inside a KMD chunk."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from ngb_tools.xpr.errors import XprFormatError, XprInputError
from ngb_tools.xpr.source import Container, open_source
from teamninja.synthetic import build_chunk

from .synthetic import build_xpr, format_word, texture_header

XPR = build_xpr([texture_header(0, format_word(0x0C, 2, 2))], bytes(8))
OBJINFO = b"OBJINFO\0" + bytes(24)


class OpenSourceTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def write(self, name: str, data: bytes) -> Path:
        path = self.root / name
        path.write_bytes(data)
        return path

    def test_loose_xpr_starts_at_offset_zero(self) -> None:
        source = open_source(self.write("a.xpr", XPR), kmd=False)
        self.assertEqual((source.container, source.offset, source.slot), (Container.XPR, 0, None))
        self.assertEqual(len(source.xpr.textures), 1)

    def test_kmd_finds_the_xpr0_slot(self) -> None:
        kmd = build_chunk(b"KMD", [OBJINFO, XPR, None], table_pos=0x80)
        source = open_source(self.write("a.kmd", kmd), kmd=True)
        self.assertEqual(source.container, Container.KMD)
        self.assertEqual(source.slot, 1)
        self.assertEqual(kmd[source.offset : source.offset + 4], b"XPR0")
        self.assertEqual(source.xpr.total_size, len(XPR))

    def test_chunk_without_kmd_flag_suggests_it(self) -> None:
        kmd = build_chunk(b"KMD", [OBJINFO, XPR])
        with self.assertRaisesRegex(XprInputError, "--kmd"):
            open_source(self.write("a.kmd", kmd), kmd=False)

    def test_random_bytes_are_an_input_error(self) -> None:
        with self.assertRaisesRegex(XprInputError, "XPR0"):
            open_source(self.write("a.bin", b"\x00\x01garbage" * 4), kmd=False)

    def test_kmd_flag_on_something_that_is_not_a_chunk(self) -> None:
        with self.assertRaises(XprInputError):
            open_source(self.write("a.bin", b"garbage"), kmd=True)

    def test_broken_xpr0_is_a_format_error_not_an_input_error(self) -> None:
        broken = bytearray(XPR)
        broken[8:12] = (0xFFFFFFFF).to_bytes(4, "little")  # impossible header_size
        with self.assertRaises(XprFormatError) as raised:
            open_source(self.write("a.xpr", bytes(broken)), kmd=False)
        self.assertNotIsInstance(raised.exception, XprInputError)

    def test_kmd_without_xpr0_is_an_input_error(self) -> None:
        with self.assertRaisesRegex(XprInputError, "XPR0"):
            open_source(self.write("a.kmd", build_chunk(b"KMD", [OBJINFO])), kmd=True)


if __name__ == "__main__":
    unittest.main()
