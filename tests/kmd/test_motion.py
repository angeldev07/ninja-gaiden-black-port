"""Tests del banco de motions: claves de 20 bits, duraciones, interpolación y errores."""

from __future__ import annotations

import struct
import unittest

from ngb_tools.kmd.motion import MotionFormatError, parse_motion_bank, parse_track

from .synthetic_motion import build_bank, chr_dat, constant, key

# Pista de 20 frames con los cuatro tipos de clave:
#   frame 0: tipo 1 (valor 0) -> lineal hasta 40 en 4 frames
#   frame 4: tipo 3 (valor 40, pendientes A=160 y B=0) -> Hermite hasta 140 en 10 frames
#   frame 14: tipo 2 (valor 140, pendiente 32) -> Hermite hasta 100 en 6 frames
#   frame 20: fin (valor 100)
MIXED = key(1, 4, 0) + key(3, 10, 40, 160, 0) + key(2, 6, 140, 32) + key(0, 0, 100)


def track(data: bytes):  # type: ignore[no-untyped-def]
    return parse_track(data, 0, len(data))


class KeyTests(unittest.TestCase):
    def test_value_is_a_signed_20_bit_integer(self) -> None:
        # -531 = 0xFFDED: los 4 bits altos (0xF) van en el control, como en ryu.mtn
        self.assertEqual(key(1, 4, -531)[:2], struct.pack("<H", 0xF011))
        self.assertEqual(track(constant(-531, 4)).keys[0].value, -531)
        # 62722 = 0x0F502: positivo aunque el u16 bajo tenga el bit 15 a 1
        self.assertEqual(track(constant(62722, 4)).keys[0].value, 62722)

    def test_frames_accumulate_and_the_end_key_closes_the_track(self) -> None:
        t = track(MIXED)
        self.assertEqual([k.frame for k in t.keys], [0, 4, 14, 20])
        self.assertEqual([k.kind for k in t.keys], [1, 3, 2, 0])
        self.assertEqual(t.frames, 20)
        self.assertEqual((t.keys[1].slope_out, t.keys[1].slope_in_next), (160, 0))
        self.assertEqual((t.keys[2].slope_out, t.keys[2].slope_in_next), (32, 32))


class SampleTests(unittest.TestCase):
    def setUp(self) -> None:
        self.t = track(MIXED)

    def test_keys_and_saturation(self) -> None:
        self.assertEqual(self.t.sample(4), 40.0)
        self.assertEqual(self.t.sample(14), 140.0)
        self.assertEqual(self.t.sample(-3), 0.0)
        self.assertEqual(self.t.sample(25), 100.0)

    def test_value_keys_are_linear(self) -> None:
        self.assertAlmostEqual(self.t.sample(2), 20.0)  # mitad de 0 -> 40

    def test_two_slopes_hermite(self) -> None:
        # t = 0,5; m0 = 160 * 10 / 16 = 100; m1 = 0
        # p = 0,5*40 + 0,125*100 + 0,5*140 - 0,125*0 = 102,5
        self.assertAlmostEqual(self.t.sample(9), 102.5)

    def test_one_slope_is_used_at_both_ends(self) -> None:
        # t = 0,5; m0 = m1 = 32 * 6 / 16 = 12 -> se cancelan: 0,5*140 + 0,5*100 = 120
        self.assertAlmostEqual(self.t.sample(17), 120.0)


class BankTests(unittest.TestCase):
    def setUp(self) -> None:
        self.body = build_bank(
            [[constant(1, 4), constant(2, 4)], [MIXED, constant(5, 20)]], table=[0, 1, 0, 0]
        )

    def test_table_motions_and_distinct_ids(self) -> None:
        bank = parse_motion_bank(self.body)
        self.assertEqual(len(bank.table), 4)
        self.assertEqual(bank.distinct_ids(), (0, 1))
        self.assertEqual(bank.motion(2).offset, bank.motion(0).offset)
        self.assertEqual(bank.motion(1).frames, 20)
        self.assertEqual(bank.motion(0).sample(0), (1.0, 2.0))

    def test_chr_dat_and_raw_bodies_parse_the_same(self) -> None:
        wrapped = parse_motion_bank(chr_dat(self.body))
        self.assertEqual(wrapped.table, parse_motion_bank(self.body).table)

    def test_unknown_id_is_an_error(self) -> None:
        with self.assertRaises(MotionFormatError):
            parse_motion_bank(self.body).motion(9)


class FormatErrorTests(unittest.TestCase):
    def assert_rejected(self, body: bytes) -> None:
        with self.assertRaises(MotionFormatError):
            parse_motion_bank(body)

    def test_tracks_of_different_length(self) -> None:
        self.assert_rejected(build_bank([[constant(0, 4), constant(0, 5)]], [0]))

    def test_track_without_end_key(self) -> None:
        self.assert_rejected(build_bank([[key(1, 4, 0)]], [0]))

    def test_empty_table(self) -> None:
        self.assert_rejected(bytes(16))

    def test_misaligned_motion_offset(self) -> None:
        body = bytearray(build_bank([[constant(0, 4)]], [0]))
        struct.pack_into("<I", body, 0, 0x22)
        self.assert_rejected(bytes(body))

    def test_unordered_track_offsets(self) -> None:
        body = bytearray(build_bank([[constant(0, 4), constant(0, 4)]], [0]))
        (header,) = struct.unpack_from("<I", body, 0)
        first, second = struct.unpack_from("<2I", body, header)
        struct.pack_into("<2I", body, header, second, first)
        self.assert_rejected(bytes(body))


if __name__ == "__main__":
    unittest.main()
