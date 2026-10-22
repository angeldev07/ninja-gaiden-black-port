"""Tests of the NV2A Morton order (swizzle), with hand-computed tables."""

from __future__ import annotations

import struct
import tracemalloc
import unittest
from collections.abc import Iterator

from ngb_tools.xpr.swizzle import morton_order, swizzle_masks, unswizzle

# For each pixel in linear order (row by row), its position in swizzled memory.
# x takes the even bits and y the odd ones while both dimensions have bits.
MORTON_4X4 = [
    [0, 1, 4, 5],
    [2, 3, 6, 7],
    [8, 9, 12, 13],
    [10, 11, 14, 15],
]
# 16x2: y has a single bit (bit 1); x takes bit 0 and bit 2 onwards (masks 0b11101 / 0b10).
MORTON_16X2 = [
    [0, 1, 4, 5, 8, 9, 12, 13, 16, 17, 20, 21, 24, 25, 28, 29],
    [2, 3, 6, 7, 10, 11, 14, 15, 18, 19, 22, 23, 26, 27, 30, 31],
]
# 4x16: x has bits 0 and 2; y, bit 1 and bits 3 to 5 (masks 0b000101 / 0b111010).
# Base of each row: y0<<1 | y1<<3 | y2<<4 | y3<<5; columns: +0, +1, +4, +5.
MORTON_4X16 = [
    [0, 1, 4, 5],
    [2, 3, 6, 7],
    [8, 9, 12, 13],
    [10, 11, 14, 15],
    [16, 17, 20, 21],
    [18, 19, 22, 23],
    [24, 25, 28, 29],
    [26, 27, 30, 31],
    [32, 33, 36, 37],
    [34, 35, 38, 39],
    [40, 41, 44, 45],
    [42, 43, 46, 47],
    [48, 49, 52, 53],
    [50, 51, 54, 55],
    [56, 57, 60, 61],
    [58, 59, 62, 63],
]
# 8x4: x has one more bit than y, so bit 4 belongs to x alone (masks 0b10101 / 0b01010).
MORTON_8X4 = [
    [0, 1, 4, 5, 16, 17, 20, 21],
    [2, 3, 6, 7, 18, 19, 22, 23],
    [8, 9, 12, 13, 24, 25, 28, 29],
    [10, 11, 14, 15, 26, 27, 30, 31],
]


def flatten(rows: list[list[int]]) -> list[int]:
    return [value for row in rows for value in row]


class SwizzleMaskTests(unittest.TestCase):
    def test_square_masks_interleave_x_then_y(self) -> None:
        self.assertEqual(swizzle_masks(4, 4), (0b0101, 0b1010))

    def test_wide_texture_gives_the_extra_bit_to_x(self) -> None:
        self.assertEqual(swizzle_masks(8, 4), (0b10101, 0b01010))

    def test_tall_texture_gives_the_extra_bits_to_y(self) -> None:
        self.assertEqual(swizzle_masks(4, 16), (0b000101, 0b111010))

    def test_one_pixel_wide_texture_is_all_y(self) -> None:
        self.assertEqual(swizzle_masks(1, 8), (0, 0b111))

    def test_non_power_of_two_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            swizzle_masks(6, 4)


class MortonOrderTests(unittest.TestCase):
    def test_4x4(self) -> None:
        self.assertEqual(list(morton_order(4, 4)), flatten(MORTON_4X4))

    def test_8x4(self) -> None:
        self.assertEqual(list(morton_order(8, 4)), flatten(MORTON_8X4))

    def test_tall_4x8_continues_below_the_first_square(self) -> None:
        order = list(morton_order(4, 8))
        self.assertEqual(order[:16], flatten(MORTON_4X4))
        self.assertEqual(order[4 * 4], 16)  # (x=0, y=4): first pixel of the second square

    def test_16x2_and_4x16(self) -> None:
        self.assertEqual(list(morton_order(16, 2)), flatten(MORTON_16X2))
        self.assertEqual(list(morton_order(4, 16)), flatten(MORTON_4X16))

    def test_512x2_has_a_single_y_bit(self) -> None:
        # Hand-written oracle for this shape: x0 at bit 0, y at bit 1, x>>1 from bit 2.
        expected = [(x & 1) | (y << 1) | ((x >> 1) << 2) for y in range(2) for x in range(512)]
        order = list(morton_order(512, 2))
        self.assertEqual(order, expected)
        self.assertEqual(
            (order[1], order[2], order[511], order[512], order[512 + 256]), (1, 4, 1021, 2, 514)
        )

    def test_4x64_has_two_x_bits(self) -> None:
        # Hand oracle: x0 bit 0, y0 bit 1, x1 bit 2, y1 bit 3 and y>>2 from bit 4.
        expected = [
            (x & 1) | ((y & 1) << 1) | ((x >> 1) << 2) | (((y >> 1) & 1) << 3) | ((y >> 2) << 4)
            for y in range(64)
            for x in range(4)
        ]
        order = list(morton_order(4, 64))
        self.assertEqual(order, expected)
        self.assertEqual((order[4 * 4], order[4 * 5 + 1], order[-1]), (16, 19, 255))

    def test_order_is_lazy(self) -> None:
        order = morton_order(64, 64)
        self.assertTrue(isinstance(order, Iterator), type(order).__name__)


class UnswizzleTests(unittest.TestCase):
    def test_single_byte_elements_follow_the_morton_table(self) -> None:
        # Swizzled memory: byte k is swizzled element k.
        swizzled = bytes(range(32))
        self.assertEqual(list(unswizzle(swizzled, 8, 4, 1)), flatten(MORTON_8X4))

    def test_multi_byte_elements_move_as_a_unit(self) -> None:
        swizzled = b"".join(bytes([k, 0xA0 | k]) for k in range(16))
        linear = unswizzle(swizzled, 4, 4, 2)
        # Linear pixel 2 (x=2, y=0) is swizzled pixel 4.
        self.assertEqual(linear[4:6], bytes([4, 0xA4]))

    def test_16x2_with_two_byte_elements(self) -> None:
        swizzled = b"".join(bytes([k, 0x80 | k]) for k in range(32))
        linear = unswizzle(swizzled, 16, 2, 2)
        expected = b"".join(bytes([k, 0x80 | k]) for k in flatten(MORTON_16X2))
        self.assertEqual(linear, expected)

    def test_4x16_with_four_byte_elements(self) -> None:
        swizzled = b"".join(struct.pack("<I", 0xA5000000 | k) for k in range(64))
        linear = unswizzle(swizzled, 4, 16, 4)
        expected = b"".join(struct.pack("<I", 0xA5000000 | k) for k in flatten(MORTON_4X16))
        self.assertEqual(linear, expected)

    def test_three_byte_elements_are_rejected(self) -> None:
        with self.assertRaises(ValueError):
            unswizzle(bytes(48), 4, 4, 3)

    def test_wrong_length_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            unswizzle(bytes(15), 4, 4, 1)


class UnswizzleMemoryTests(unittest.TestCase):
    """Peak memory is O(n bytes): no lists with one object per pixel."""

    def test_1024x1024_argb_peak_is_a_few_times_the_image(self) -> None:
        size = 1024 * 1024 * 4
        swizzled = bytes(size)
        tracemalloc.start()
        try:
            unswizzle(swizzled, 1024, 1024, 4)
            _, peak = tracemalloc.get_traced_memory()
        finally:
            tracemalloc.stop()
        # Working copy + output + final bytes: about 3 times the image (12 MiB).
        self.assertLess(peak, 4 * size)


if __name__ == "__main__":
    unittest.main()
