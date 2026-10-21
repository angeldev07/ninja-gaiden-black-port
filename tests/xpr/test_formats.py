"""Tests de la tabla de formatos de textura de Xbox (valores de Cxbx-Reloaded y xemu)."""

from __future__ import annotations

import unittest

from ngb_tools.xpr.formats import FORMATS, Codec, Field, Storage, lookup_format


class LookupTests(unittest.TestCase):
    def test_formats_seen_in_the_game(self) -> None:
        expected = {
            0x04: ("A4R4G4B4", 16, Storage.SWIZZLED, Codec.PACKED),
            0x0B: ("P8", 8, Storage.SWIZZLED, Codec.PALETTE),
            0x0C: ("DXT1", 4, Storage.COMPRESSED, Codec.BC1),
            0x0E: ("DXT3", 8, Storage.COMPRESSED, Codec.BC2),
            0x0F: ("DXT5", 8, Storage.COMPRESSED, Codec.BC3),
            0x19: ("A8", 8, Storage.SWIZZLED, Codec.PACKED),
        }
        for code, (name, bpp, storage, codec) in expected.items():
            with self.subTest(code=hex(code)):
                fmt = lookup_format(code)
                assert fmt is not None
                self.assertEqual((fmt.name, fmt.bits_per_pixel), (name, bpp))
                self.assertEqual((fmt.storage, fmt.codec), (storage, codec))

    def test_linear_variants_have_their_own_codes(self) -> None:
        fmt = lookup_format(0x12)
        assert fmt is not None
        self.assertEqual(fmt.name, "LIN_A8R8G8B8")
        self.assertEqual(fmt.storage, Storage.LINEAR)

    def test_aliases_are_recorded(self) -> None:
        dxt3, dxt5 = lookup_format(0x0E), lookup_format(0x0F)
        assert dxt3 is not None and dxt5 is not None
        self.assertIn("DXT2", dxt3.aliases)
        self.assertIn("DXT4", dxt5.aliases)

    def test_unknown_code_is_none(self) -> None:
        self.assertIsNone(lookup_format(0x08))
        self.assertIsNone(lookup_format(0xFF))

    def test_depth_formats_are_known_but_not_decodable(self) -> None:
        fmt = lookup_format(0x2A)
        assert fmt is not None
        self.assertEqual(fmt.name, "D24S8")
        self.assertFalse(fmt.is_decodable)

    def test_block_size_of_compressed_formats(self) -> None:
        sizes = {code: FORMATS[code].block_bytes for code in (0x0C, 0x0E, 0x0F)}
        self.assertEqual(sizes, {0x0C: 8, 0x0E: 16, 0x0F: 16})


class TableInvariantTests(unittest.TestCase):
    def test_keys_match_codes(self) -> None:
        for code, fmt in FORMATS.items():
            self.assertEqual(code, fmt.code)

    def test_packed_fields_fit_in_the_pixel(self) -> None:
        for fmt in FORMATS.values():
            if fmt.layout is None:
                continue
            channels = (fmt.layout.red, fmt.layout.green, fmt.layout.blue, fmt.layout.alpha)
            for channel in channels:
                if isinstance(channel, Field):
                    with self.subTest(fmt=fmt.name):
                        self.assertLessEqual(channel.shift + channel.bits, fmt.bits_per_pixel)

    def test_every_packed_format_has_a_layout(self) -> None:
        for fmt in FORMATS.values():
            with self.subTest(fmt=fmt.name):
                self.assertEqual(fmt.codec is Codec.PACKED, fmt.layout is not None)


if __name__ == "__main__":
    unittest.main()
