"""Xbox ADPCM: bloques mono y estéreo con valores calculados a mano."""

from __future__ import annotations

import unittest

from ngb_tools.audio import xbox_adpcm

from .synthetic import xbox_block

ZERO = bytes(4)


class MonoBlockTests(unittest.TestCase):
    def test_hand_computed_block(self) -> None:
        # Cabecera 100, índice 0. Nibbles (el bajo primero): 7, 0, 8, 0, ...
        #  7, paso 7:  7>>3 + 7 + 7>>1 + 7>>2 = 0+7+3+1 = 11 → 111, índice 0+8 = 8
        #  0, paso 16: 16>>3 = 2 → 113, índice 7
        #  8, paso 14: −(14>>3) = −1 → 112, índice 6
        #  0, pasos 13, 12, 11, 10, 9, 8 → +1 cada uno → 113..118, índice baja hasta 0
        #  0, paso 7: 7>>3 = 0 → se queda en 118.
        # El último byte es 0x70: su nibble bajo (el 63.º) da 0 y el alto se descarta.
        data = bytes([0x07, 0x08]) + bytes(29) + bytes([0x70])
        block = xbox_block([(100, 0)], [[data[i : i + 4] for i in range(0, 32, 4)]])
        self.assertEqual(len(block), 36)
        expected = [100, 111, 113, 112, 113, 114, 115, 116, 117, 118] + [118] * 54
        self.assertEqual(xbox_adpcm.decode(block, 1).tolist(), expected)

    def test_header_index_is_clamped(self) -> None:
        block = xbox_block([(0, 200)], [[ZERO] * 8])  # 200 = −56 con signo → 0
        self.assertEqual(xbox_adpcm.decode(block, 1).tolist(), [0] * 64)

    def test_partial_trailing_block_is_ignored(self) -> None:
        block = xbox_block([(5, 0)], [[ZERO] * 8])
        self.assertEqual(len(xbox_adpcm.decode(block + bytes(20), 1)), 64)

    def test_samples_for_size(self) -> None:
        self.assertEqual(xbox_adpcm.samples_for(36 * 10 + 7, 1), 640)
        self.assertEqual(xbox_adpcm.samples_for(72 * 3, 2), 192)


class StereoBlockTests(unittest.TestCase):
    def test_channels_alternate_in_four_byte_groups(self) -> None:
        # c0: cabecera 100, primer grupo 07 00 00 00 → 100, 111, 113, 114 ... 120 y se queda.
        # c1: cabecera −50; nibble 1 en el grupo 1 (su muestra 9): paso 7 → 7>>3 + 7>>2 = 1.
        left = [b"\x07\x00\x00\x00"] + [ZERO] * 7
        right = [ZERO, b"\x01\x00\x00\x00"] + [ZERO] * 6
        block = xbox_block([(100, 0), (-50, 0)], [left, right])
        self.assertEqual(len(block), 72)
        self.assertEqual(block[20], 0x01)  # 8 de cabeceras + 4 (c0, grupo 0) + 4 + 4
        pcm = xbox_adpcm.decode(block, 2)
        expected_left = [100, 111, 113, 114, 115, 116, 117, 118, 119, 120] + [120] * 54
        expected_right = [-50] * 9 + [-49] * 55
        self.assertEqual(pcm[0::2].tolist(), expected_left)
        self.assertEqual(pcm[1::2].tolist(), expected_right)


if __name__ == "__main__":
    unittest.main()
