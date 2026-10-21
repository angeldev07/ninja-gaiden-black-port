"""Tests de los formatos empaquetados (ARGB, 565, 1555, 4444, A8, L8...) y de P8 con paleta."""

from __future__ import annotations

import struct
import tracemalloc
import unittest

from ngb_tools.xpr.formats import FORMATS
from ngb_tools.xpr.pixels import decode_packed, decode_palette, expand_bits

Pixel = tuple[int, int, int, int]


def pixels(rgba: bytes) -> list[Pixel]:
    return [(rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]) for i in range(0, len(rgba), 4)]


def u16(*values: int) -> bytes:
    return struct.pack(f"<{len(values)}H", *values)


def decode_one(code: int, raw: bytes) -> Pixel:
    return pixels(decode_packed(raw, 1, 1, FORMATS[code]))[0]


class ExpandBitsTests(unittest.TestCase):
    def test_short_fields_replicate_their_high_bits(self) -> None:
        self.assertEqual(expand_bits(1, 1), 255)
        self.assertEqual(expand_bits(0b10000, 5), 132)
        self.assertEqual(expand_bits(0xA, 4), 0xAA)
        self.assertEqual(expand_bits(0b111111, 6), 255)

    def test_wide_fields_keep_their_high_byte(self) -> None:
        self.assertEqual(expand_bits(0xABCD, 16), 0xAB)


class PackedFormatTests(unittest.TestCase):
    def test_a8r8g8b8_is_bgra_in_memory(self) -> None:
        self.assertEqual(
            decode_one(0x06, bytes([0x11, 0x22, 0x33, 0x44])), (0x33, 0x22, 0x11, 0x44)
        )

    def test_x8r8g8b8_ignores_the_top_byte(self) -> None:
        self.assertEqual(decode_one(0x07, bytes([0x11, 0x22, 0x33, 0x00])), (0x33, 0x22, 0x11, 255))

    def test_r8g8b8a8_puts_alpha_in_the_low_byte(self) -> None:
        self.assertEqual(
            decode_one(0x3C, bytes([0x44, 0x33, 0x22, 0x11])), (0x11, 0x22, 0x33, 0x44)
        )

    def test_r5g6b5(self) -> None:
        self.assertEqual(decode_one(0x05, u16(0xF800)), (255, 0, 0, 255))
        self.assertEqual(decode_one(0x05, u16(0x07E0)), (0, 255, 0, 255))

    def test_a1r5g5b5_alpha_is_the_top_bit(self) -> None:
        self.assertEqual(decode_one(0x02, u16(0x801F)), (0, 0, 255, 255))
        self.assertEqual(decode_one(0x02, u16(0x7C00)), (255, 0, 0, 0))

    def test_a4r4g4b4(self) -> None:
        self.assertEqual(decode_one(0x04, u16(0xF0A5)), (0, 0xAA, 0x55, 255))

    def test_a8_is_white_with_alpha(self) -> None:
        self.assertEqual(decode_one(0x19, bytes([0x80])), (255, 255, 255, 0x80))

    def test_l8_and_a8l8_are_grey(self) -> None:
        self.assertEqual(decode_one(0x00, bytes([0x40])), (0x40, 0x40, 0x40, 255))
        self.assertEqual(decode_one(0x1A, u16(0x8040)), (0x40, 0x40, 0x40, 0x80))

    def test_a8b8g8r8_is_rgba_in_memory(self) -> None:
        # 0x3A: R en el byte bajo y A en el alto -> en memoria R, G, B, A.
        self.assertEqual(
            decode_one(0x3A, bytes([0x11, 0x22, 0x33, 0x44])), (0x11, 0x22, 0x33, 0x44)
        )

    def test_b8g8r8a8_is_argb_in_memory(self) -> None:
        # 0x3B: A en el byte bajo, luego R, G y B -> en memoria A, R, G, B.
        self.assertEqual(
            decode_one(0x3B, bytes([0x44, 0x11, 0x22, 0x33])), (0x11, 0x22, 0x33, 0x44)
        )

    def test_r5g5b5a1_alpha_is_the_low_bit(self) -> None:
        # 0x38: R bits 11-15 = 31, G bits 6-10 = 0, B bits 1-5 = 16, A bit 0 = 1.
        # 0xF800 | 16 << 1 | 1 = 0xF821; b8 = 16 << 3 | 16 >> 2 = 132.
        self.assertEqual(decode_one(0x38, u16(0xF821)), (255, 0, 132, 255))
        self.assertEqual(decode_one(0x38, u16(0xF820)), (255, 0, 132, 0))

    def test_r4g4b4a4_alpha_is_the_low_nibble(self) -> None:
        # 0x39: 0x1234 -> R 1, G 2, B 3, A 4; cada nibble n se amplía a n * 17.
        self.assertEqual(decode_one(0x39, u16(0x1234)), (0x11, 0x22, 0x33, 0x44))

    def test_x1r5g5b5_ignores_the_top_bit(self) -> None:
        self.assertEqual(decode_one(0x03, u16(0x7C00)), (255, 0, 0, 255))
        self.assertEqual(decode_one(0x03, u16(0x83E0)), (0, 255, 0, 255))

    def test_l16_keeps_the_high_byte(self) -> None:
        self.assertEqual(decode_one(0x32, u16(0xABCD)), (0xAB, 0xAB, 0xAB, 255))

    def test_32bpp_rows_are_read_with_the_pitch(self) -> None:
        # LIN_A8R8G8B8 de 1x2 con pitch 64: B, G, R, A de cada fila y relleno detrás.
        raw = bytes([1, 2, 3, 4]) + bytes(60) + bytes([5, 6, 7, 8]) + bytes(60)
        decoded = decode_packed(raw, 1, 2, FORMATS[0x12], pitch=64)
        self.assertEqual(pixels(decoded), [(3, 2, 1, 4), (7, 6, 5, 8)])

    def test_rows_are_read_with_the_pitch(self) -> None:
        # LIN_A8 de 2x2 con pitch de 64 bytes: solo los dos primeros bytes de cada fila valen.
        raw = bytes([1, 2]) + bytes(62) + bytes([3, 4]) + bytes(62)
        decoded = decode_packed(raw, 2, 2, FORMATS[0x1F], pitch=64)
        self.assertEqual(list(decoded[3::4]), [1, 2, 3, 4])

    def test_non_packed_format_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            decode_packed(bytes(8), 4, 4, FORMATS[0x0C])

    def test_short_data_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            decode_packed(bytes(3), 2, 1, FORMATS[0x05])


class PackedMemoryTests(unittest.TestCase):
    """La decodificación no memoriza una entrada por píxel distinto en 32 bpp."""

    def test_distinct_argb_pixels_peak_is_a_few_times_the_image(self) -> None:
        side = 512
        raw = b"".join(struct.pack("<I", 0x01000000 * (i & 0xFF) + i) for i in range(side * side))
        output = 4 * side * side
        tracemalloc.start()
        try:
            decoded = decode_packed(raw, side, side, FORMATS[0x06])
            _, peak = tracemalloc.get_traced_memory()
        finally:
            tracemalloc.stop()
        self.assertEqual(len(decoded), output)
        self.assertLess(peak, 4 * output)

    def test_every_16_bit_word_decodes(self) -> None:
        # Las 65 536 palabras de R5G6B5 a la vez: la memoria acotada no cambia el resultado.
        raw = u16(*range(1 << 16))
        decoded = pixels(decode_packed(raw, 256, 256, FORMATS[0x05]))
        self.assertEqual(decoded[0x0000], (0, 0, 0, 255))
        self.assertEqual(decoded[0x001F], (0, 0, 255, 255))
        self.assertEqual(decoded[0x07E0], (0, 255, 0, 255))
        self.assertEqual(decoded[0x8410], (132, 130, 132, 255))
        self.assertEqual(decoded[0xFFFF], (255, 255, 255, 255))


class PaletteTests(unittest.TestCase):
    def test_entries_are_d3dcolor_argb(self) -> None:
        # D3DCOLOR = u32 ARGB little endian: en memoria B, G, R, A.
        palette = bytes([0x10, 0x20, 0x30, 0x40, 0xAA, 0xBB, 0xCC, 0xDD])
        decoded = pixels(decode_palette(bytes([1, 0, 1]), palette))
        second, first = (0xCC, 0xBB, 0xAA, 0xDD), (0x30, 0x20, 0x10, 0x40)
        self.assertEqual(decoded, [second, first, second])

    def test_index_past_the_palette_is_transparent_black(self) -> None:
        palette = bytes([0x10, 0x20, 0x30, 0x40])
        self.assertEqual(pixels(decode_palette(bytes([5]), palette)), [(0, 0, 0, 0)])


if __name__ == "__main__":
    unittest.main()
