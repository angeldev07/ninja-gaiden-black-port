"""Tests del decodificador de push buffers: solo BEGIN_END y ARRAY_ELEMENT16/32."""

from __future__ import annotations

import struct
import unittest

from ngb_tools.kmd.errors import PushBufferError
from ngb_tools.kmd.pushbuffer import decode_push_buffer

from .synthetic import push_buffer_bytes


def words(*values: int) -> bytes:
    return struct.pack(f"<{len(values)}I", *values)


class DecodeTests(unittest.TestCase):
    def test_even_strip_comes_back_in_order(self) -> None:
        self.assertEqual(decode_push_buffer(push_buffer_bytes([5, 6, 7, 8])), (5, 6, 7, 8))

    def test_odd_strip_uses_array_element32_for_the_last_index(self) -> None:
        self.assertEqual(decode_push_buffer(push_buffer_bytes([1, 2, 3])), (1, 2, 3))

    def test_trailing_zero_padding_is_accepted(self) -> None:
        data = push_buffer_bytes([0, 1, 2, 3]) + bytes(8)
        self.assertEqual(decode_push_buffer(data), (0, 1, 2, 3))

    def test_garbage_in_the_padding_word_after_end_is_ignored(self) -> None:
        # Como ssato_02.kmd res236: el u32 que sigue a END trae 0xc2f916e6.
        data = push_buffer_bytes([0, 1, 2])[:-4] + words(0xC2F916E6) + bytes(4)
        self.assertEqual(decode_push_buffer(data), (0, 1, 2))


class RejectTests(unittest.TestCase):
    def assert_rejected(self, data: bytes) -> None:
        with self.assertRaises(PushBufferError):
            decode_push_buffer(data)

    def test_other_methods_are_rejected(self) -> None:
        # SET_VERTEX_DATA_ARRAY_FORMAT (0x1760) dentro de la tira
        self.assert_rejected(words(0x000417FC, 6, 0x00041760, 0x2042, 0x000417FC, 0))

    def test_jumps_are_rejected(self) -> None:
        self.assert_rejected(words(0x20000000))

    def test_only_triangle_strips_are_accepted(self) -> None:
        self.assert_rejected(words(0x000417FC, 5, 0x000417FC, 0))

    def test_missing_end_is_rejected(self) -> None:
        self.assert_rejected(words(0x000417FC, 6, 0x00041800, 0x00010000))

    def test_indices_outside_begin_end_are_rejected(self) -> None:
        self.assert_rejected(words(0x00041800, 0x00010000))

    def test_data_after_end_is_rejected(self) -> None:
        self.assert_rejected(push_buffer_bytes([0, 1, 2]) + words(0x00041800, 1))

    def test_garbage_beyond_the_padding_word_is_rejected(self) -> None:
        self.assert_rejected(push_buffer_bytes([0, 1, 2])[:-4] + words(0xC2F916E6, 7))

    def test_garbage_before_end_is_a_command(self) -> None:
        # Sin END, la palabra suelta se lee como cabecera de comando y se rechaza.
        self.assert_rejected(words(0x000417FC, 6, 0x00041800, 0x00010000, 0xC2F916E6))

    def test_truncated_arguments_are_rejected(self) -> None:
        self.assert_rejected(words(0x000417FC, 6, 0x00081800, 0x00010000))

    def test_unaligned_size_is_rejected(self) -> None:
        self.assert_rejected(push_buffer_bytes([0, 1, 2]) + b"\0")


if __name__ == "__main__":
    unittest.main()
