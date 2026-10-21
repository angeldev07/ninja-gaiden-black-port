"""Tests del escritor PNG: se relee con un lector estricto (chunks, CRC e IDAT)."""

from __future__ import annotations

import unittest

from ngb_tools.xpr.png import encode_png

from .pngread import PngError, iter_chunks, read_png


def gradient(width: int, height: int) -> bytes:
    return bytes(
        value
        for y in range(height)
        for x in range(width)
        for value in (x * 255 // max(width - 1, 1), y * 255 // max(height - 1, 1), 128, 200)
    )


class EncodePngTests(unittest.TestCase):
    def test_round_trip_preserves_every_pixel(self) -> None:
        rgba = gradient(7, 3)
        decoded = read_png(encode_png(7, 3, rgba))
        self.assertEqual((decoded.width, decoded.height), (7, 3))
        self.assertEqual(decoded.rgba, rgba)

    def test_minimal_chunk_sequence(self) -> None:
        decoded = read_png(encode_png(1, 1, bytes([1, 2, 3, 4])))
        self.assertEqual(decoded.chunk_types, (b"IHDR", b"IDAT", b"IEND"))

    def test_corrupted_byte_is_caught_by_the_crc(self) -> None:
        png = bytearray(encode_png(2, 2, bytes(16)))
        png[20] ^= 0xFF  # dentro de los datos de IHDR
        with self.assertRaisesRegex(PngError, "CRC"):
            iter_chunks(bytes(png))

    def test_pixel_count_must_match_the_size(self) -> None:
        with self.assertRaises(ValueError):
            encode_png(2, 2, bytes(15))

    def test_empty_image_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            encode_png(0, 4, b"")


if __name__ == "__main__":
    unittest.main()
