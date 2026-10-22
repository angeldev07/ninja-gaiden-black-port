"""Tests of signed bump textures (V8U8, L6V5U5), with hand-computed bytes.

Mapping (`ngb_tools.xpr.bump`): R/G = U/V in offset binary (8 bits: +128; 5 bits: +16 and
widened by replicating bits), B = floor((z + 1) * 127.5 + 0.5) with z = sqrt(1 - u² - v²),
A = 255 (V8U8) or L widened from 6 to 8 bits (L6V5U5).
"""

from __future__ import annotations

import struct
import tempfile
import unittest
from pathlib import Path

from ngb_tools.xpr.bump import decode_bump, l6v5u5_texel, signed, v8u8_texel
from ngb_tools.xpr.export import export_textures
from ngb_tools.xpr.formats import FORMATS
from ngb_tools.xpr.resources import parse_xpr
from ngb_tools.xpr.source import open_source
from ngb_tools.xpr.texture import AlphaMode, alpha_mode, decode_texture

from .pngread import read_png
from .synthetic import build_xpr, format_word, solid_dxt1_block, texture_header

L6V5U5, V8U8, LIN_L6V5U5, DXT1 = 0x27, 0x28, 0x37, 0x0C


def v8u8(u: int, v: int) -> int:
    """V8U8 word: U in the low byte, V in the high byte, in two's complement."""
    return (u & 0xFF) | ((v & 0xFF) << 8)


def l6v5u5(u: int, v: int, lum: int) -> int:
    """L6V5U5 word: U in bits 0-4, V in 5-9 (two's complement) and L in 10-15."""
    return (u & 0x1F) | ((v & 0x1F) << 5) | (lum << 10)


class SignedFieldTests(unittest.TestCase):
    def test_twos_complement(self) -> None:
        self.assertEqual([signed(v, 8) for v in (0x80, 0x7F, 0xFF, 0)], [-128, 127, -1, 0])
        self.assertEqual([signed(v, 5) for v in (0x10, 0x0F, 0x1F)], [-16, 15, -1])


class V8U8Tests(unittest.TestCase):
    def test_flat_normal_is_blue_violet(self) -> None:
        self.assertEqual(v8u8_texel(v8u8(0, 0)), bytes((128, 128, 255, 255)))

    def test_extremes(self) -> None:
        # u = -1, v = 127/128: u² + v² > 1, so z = 0 -> B = 128.
        self.assertEqual(v8u8_texel(0x7F80), bytes((0, 255, 128, 255)))
        # u = 127/128: z = sqrt(1 - 0.98443603515625) = 0.12476 -> floor(143.41 + 0.5) = 143.
        self.assertEqual(v8u8_texel(v8u8(127, 0)), bytes((255, 128, 143, 255)))
        self.assertEqual(v8u8_texel(v8u8(-128, -128)), bytes((0, 0, 128, 255)))

    def test_small_values_around_zero(self) -> None:
        # U = -1 (0xFF) and V = 1: almost flat, z = 0.99994 -> 255.
        self.assertEqual(v8u8_texel(0x01FF), bytes((127, 129, 255, 255)))


class L6V5U5Tests(unittest.TestCase):
    def test_extremes(self) -> None:
        # U = -16 (0b10000), V = 15 (0b01111), L = 63: palabra 0xFDF0.
        self.assertEqual(l6v5u5(-16, 15, 63), 0xFDF0)
        self.assertEqual(l6v5u5_texel(0xFDF0), bytes((0, 255, 128, 255)))

    def test_zero_is_132_after_bit_replication(self) -> None:
        # 0 + 16 = 0b10000 -> 0b10000100 = 132; L = 0 -> A = 0.
        self.assertEqual(l6v5u5_texel(l6v5u5(0, 0, 0)), bytes((132, 132, 255, 0)))

    def test_positive_u_and_half_luminance(self) -> None:
        # U = 15 -> 31 -> 255; u = 0.9375, z = sqrt(0.12109375) = 0.34799 -> floor(171.87 + 0.5);
        # L = 32 = 0b100000 -> 0b10000010 = 130.
        self.assertEqual(l6v5u5(15, 0, 32), 0x800F)
        self.assertEqual(l6v5u5_texel(0x800F), bytes((255, 132, 172, 130)))

    def test_minus_one(self) -> None:
        # U = -1 (0b11111) -> 15 = 0b01111 -> 0b01111011 = 123.
        self.assertEqual(l6v5u5_texel(l6v5u5(-1, -16, 0))[:2], bytes((123, 0)))


class DecodeBumpTests(unittest.TestCase):
    def test_rows_honour_the_pitch(self) -> None:
        words = (l6v5u5(0, 0, 63), l6v5u5(-16, 15, 63))
        row = struct.pack("<2H", *words) + bytes(60)  # pitch 64
        data = row + struct.pack("<2H", *reversed(words))
        decoded = decode_bump(data, 2, 2, FORMATS[LIN_L6V5U5], pitch=64)
        flat, edge = bytes((132, 132, 255, 255)), bytes((0, 255, 128, 255))
        self.assertEqual(decoded, flat + edge + edge + flat)

    def test_short_data_is_a_value_error(self) -> None:
        with self.assertRaisesRegex(ValueError, "needs 8"):
            decode_bump(bytes(6), 2, 2, FORMATS[V8U8])

    def test_other_codec_is_a_value_error(self) -> None:
        with self.assertRaisesRegex(ValueError, "bump"):
            decode_bump(bytes(8), 2, 2, FORMATS[DXT1])

    def test_formats_are_decodable_and_marked(self) -> None:
        for code in (V8U8, L6V5U5, LIN_L6V5U5):
            with self.subTest(fmt=FORMATS[code].name):
                self.assertTrue(FORMATS[code].is_decodable)
                self.assertTrue(FORMATS[code].is_signed_bump)
        self.assertFalse(FORMATS[DXT1].is_signed_bump)


def bump_xpr() -> bytes:
    """V8U8 2x2 (flat, min U, max V, U = V = -1), L6V5U5 2x2 and a 4x4 DXT1."""
    v8 = struct.pack("<4H", v8u8(0, 0), v8u8(-128, 0), v8u8(0, 127), v8u8(-1, -1))
    l6 = struct.pack("<4H", *(l6v5u5(0, 0, 0),) * 4)
    return build_xpr(
        [
            texture_header(0, format_word(V8U8, 1, 1)),
            texture_header(8, format_word(L6V5U5, 1, 1)),
            texture_header(16, format_word(DXT1, 2, 2)),
        ],
        v8 + l6 + solid_dxt1_block(0xF800),
    )


class BumpTextureTests(unittest.TestCase):
    def test_swizzled_2x2_decodes_in_order(self) -> None:
        xpr = parse_xpr(bump_xpr())
        image = decode_texture(xpr, xpr.textures[0])
        texels = [image.pixels[i : i + 4] for i in range(0, 16, 4)]
        self.assertEqual(texels[0], bytes((128, 128, 255, 255)))
        self.assertEqual(texels[1], bytes((0, 128, 128, 255)))
        # v = 127/128: z = 0.12476 -> 143.
        self.assertEqual(texels[2], bytes((128, 255, 143, 255)))
        self.assertEqual(texels[3], bytes((127, 127, 255, 255)))

    def test_alpha_mode(self) -> None:
        self.assertEqual(alpha_mode(FORMATS[L6V5U5], bytes(4)), AlphaMode.LUMINANCE)
        self.assertEqual(alpha_mode(FORMATS[V8U8], bytes(4)), AlphaMode.STRAIGHT)

    def test_manifest_marks_the_signed_bump_encoding(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "bump.xpr").write_bytes(bump_xpr())
            manifest = export_textures(open_source(root / "bump.xpr", kmd=False), root / "out")
            records = manifest["textures"]
            self.assertEqual(manifest["exported"], 3)
            self.assertEqual(
                [(r["format"], r["encoding"], r["alpha"]) for r in records],
                [
                    ("V8U8", "signed-bump", "straight"),
                    ("L6V5U5", "signed-bump", "luminance"),
                    ("DXT1", "color", "straight"),
                ],
            )
            png = read_png((root / "out" / str(records[0]["file"])).read_bytes())
            self.assertEqual((png.width, png.height), (2, 2))
            self.assertEqual(png.rgba[:4], bytes((128, 128, 255, 255)))


if __name__ == "__main__":
    unittest.main()
