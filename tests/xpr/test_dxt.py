"""Tests de los decodificadores DXT1/DXT3/DXT5 con bloques cuyo resultado se calcula a mano.

Colores de referencia en RGB565 (little endian en el bloque):
    0xF800 = rojo puro  -> (255, 0, 0)
    0x001F = azul puro  -> (0, 0, 255)
    0x8410 = r=16, g=32, b=16 -> (132, 130, 132) por replicación de bits:
             r8 = 16<<3 | 16>>2 = 132, g8 = 32<<2 | 32>>4 = 130.
Interpolación con truncado, como xemu: c2 = (2*c0 + c1) // 3, c3 = (c0 + 2*c1) // 3.
"""

from __future__ import annotations

import struct
import unittest

from ngb_tools.xpr.dxt import decode_bc1, decode_bc2, decode_bc3

RED, BLUE = 0xF800, 0x001F
Pixel = tuple[int, int, int, int]


def color_block(c0: int, c1: int, index_bytes: bytes) -> bytes:
    """Mitad de color de un bloque: dos colores 565 y 16 índices de 2 bits (4 bytes)."""
    return struct.pack("<HH", c0, c1) + index_bytes


def pixels(rgba: bytes) -> list[Pixel]:
    return [(rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]) for i in range(0, len(rgba), 4)]


def alphas(rgba: bytes) -> list[int]:
    return list(rgba[3::4])


class Bc1Tests(unittest.TestCase):
    def test_four_color_block_when_c0_greater_than_c1(self) -> None:
        # Índices por fila (2 bits por píxel, el píxel 0 en los bits bajos):
        # [0,1,2,3] = 0xE4, [3,2,1,0] = 0x1B, [0,0,0,0] = 0x00, [3,3,3,3] = 0xFF.
        block = color_block(RED, BLUE, bytes([0xE4, 0x1B, 0x00, 0xFF]))
        p0, p1 = (255, 0, 0, 255), (0, 0, 255, 255)
        p2 = (170, 0, 85, 255)  # (2*255 + 0) // 3, 0, (2*0 + 255) // 3
        p3 = (85, 0, 170, 255)
        expected = [p0, p1, p2, p3, p3, p2, p1, p0, p0, p0, p0, p0, p3, p3, p3, p3]
        self.assertEqual(pixels(decode_bc1(block, 4, 4)), expected)

    def test_three_color_block_has_transparent_black(self) -> None:
        # c0 <= c1: c2 = (c0 + c1) // 2 y el índice 3 es negro transparente.
        # Píxel 0 con índice 3 y el resto con 2: 0b10101011 = 0xAB, luego 0xAA.
        block = color_block(BLUE, RED, bytes([0xAB, 0xAA, 0xAA, 0xAA]))
        decoded = pixels(decode_bc1(block, 4, 4))
        self.assertEqual(decoded[0], (0, 0, 0, 0))
        self.assertEqual(decoded[1:], [(127, 0, 127, 255)] * 15)

    def test_565_channels_expand_by_bit_replication(self) -> None:
        block = color_block(0x8410, 0x8410, bytes(4))
        self.assertEqual(pixels(decode_bc1(block, 4, 4)), [(132, 130, 132, 255)] * 16)

    def test_blocks_are_stored_in_linear_row_order(self) -> None:
        red, blue = color_block(RED, RED, bytes(4)), color_block(BLUE, BLUE, bytes(4))
        wide = pixels(decode_bc1(red + blue, 8, 4))
        self.assertEqual(wide[0:8], [(255, 0, 0, 255)] * 4 + [(0, 0, 255, 255)] * 4)
        tall = pixels(decode_bc1(red + blue, 4, 8))
        self.assertEqual(tall[0], (255, 0, 0, 255))
        self.assertEqual(tall[4 * 4], (0, 0, 255, 255))

    def test_texture_smaller_than_a_block_is_cropped(self) -> None:
        block = color_block(RED, BLUE, bytes([0xE4, 0x1B, 0x00, 0xFF]))
        expected = [(255, 0, 0, 255), (0, 0, 255, 255), (85, 0, 170, 255), (170, 0, 85, 255)]
        self.assertEqual(pixels(decode_bc1(block, 2, 2)), expected)

    def test_short_data_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            decode_bc1(bytes(8), 8, 4)


class Bc3Tests(unittest.TestCase):
    # Índices de alfa 0,1,...,7,0,...,7 (3 bits por píxel): 0o76543210 = 0xFAC688 por mitad.
    ALPHA_INDICES = bytes([0x88, 0xC6, 0xFA, 0x88, 0xC6, 0xFA])
    # Color con c0 < c1 e índice 2 en todos: en DXT3/5 el color es SIEMPRE de 4 colores.
    COLOR = color_block(BLUE, RED, bytes([0xAA] * 4))

    def test_eight_alpha_mode_when_a0_greater_than_a1(self) -> None:
        block = bytes([255, 0]) + self.ALPHA_INDICES + self.COLOR
        # a_i = ((8-i)*a0 + (i-1)*a1) // 7 para i = 2..7.
        palette = [255, 0, 218, 182, 145, 109, 72, 36]
        self.assertEqual(alphas(decode_bc3(block, 4, 4)), palette * 2)

    def test_six_alpha_mode_adds_zero_and_255(self) -> None:
        block = bytes([0, 255]) + self.ALPHA_INDICES + self.COLOR
        palette = [0, 255, 51, 102, 153, 204, 0, 255]
        self.assertEqual(alphas(decode_bc3(block, 4, 4)), palette * 2)

    def test_color_half_ignores_the_dxt1_transparent_mode(self) -> None:
        block = bytes([255, 255]) + bytes(6) + self.COLOR
        rgb = {pixel[:3] for pixel in pixels(decode_bc3(block, 4, 4))}
        self.assertEqual(rgb, {(85, 0, 170)})  # (2*0 + 255) // 3, 0, (2*255 + 0) // 3


class Bc2Tests(unittest.TestCase):
    def test_explicit_four_bit_alpha(self) -> None:
        # Alfa del píxel i = i (4 bits, el píxel par en el nibble bajo); se expande * 17.
        alpha = bytes([0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE])
        block = alpha + color_block(BLUE, RED, bytes([0xAA] * 4))
        decoded = decode_bc2(block, 4, 4)
        self.assertEqual(alphas(decoded), [17 * i for i in range(16)])
        self.assertEqual({pixel[:3] for pixel in pixels(decoded)}, {(85, 0, 170)})


GREEN = 0x07E0
# Píxeles de referencia de los recortes 6x5 (cuatro bloques: arriba izq/der, abajo izq/der).
R17, R0, B255, G136 = (255, 0, 0, 17), (255, 0, 0, 0), (0, 0, 255, 255), (0, 255, 0, 136)


class EqualEndpointTests(unittest.TestCase):
    def test_bc1_with_c0_equal_to_c1_uses_the_three_color_mode(self) -> None:
        # c0 == c1 cuenta como c0 <= c1: índice 2 = (c0 + c1) // 2 = rojo; índice 3 = transparente.
        # Píxel 0 con índice 3 y píxel 1 con índice 2: 0b1011 = 0x0B.
        block = color_block(RED, RED, bytes([0x0B, 0, 0, 0]))
        decoded = pixels(decode_bc1(block, 4, 4))
        self.assertEqual(decoded[0], (0, 0, 0, 0))
        self.assertEqual(decoded[1:], [(255, 0, 0, 255)] * 15)

    def test_bc3_with_a0_equal_to_a1_uses_the_six_alpha_mode(self) -> None:
        # a0 == a1 = 100: los interpolados valen 100 y los índices 6 y 7 dan 0 y 255.
        block = bytes([100, 100]) + Bc3Tests.ALPHA_INDICES + Bc3Tests.COLOR
        palette = [100, 100, 100, 100, 100, 100, 0, 255]
        self.assertEqual(alphas(decode_bc3(block, 4, 4)), palette * 2)


class CroppedMultiBlockTests(unittest.TestCase):
    """6x5 ocupa 2x2 bloques (8x8): se ven 2 columnas de los bloques derechos y 1 fila abajo."""

    def test_bc2_6x5(self) -> None:
        top_right_color = color_block(RED, BLUE, bytes([0xE4, 0, 0, 0]))  # fila 0: 0, 1, 2, 3
        blocks = (
            bytes([0x11] * 8) + color_block(RED, RED, bytes(4)),  # alfa 1 * 17 = 17
            bytes([0x32, 0x54]) + bytes(6) + top_right_color,  # fila 0: alfas 2, 3, 4, 5
            bytes([0xFF] * 8) + color_block(BLUE, BLUE, bytes(4)),
            bytes([0x88] * 8) + color_block(GREEN, GREEN, bytes(4)),  # alfa 8 * 17 = 136
        )
        expected = [
            [R17, R17, R17, R17, (255, 0, 0, 34), (0, 0, 255, 51)],
            [R17, R17, R17, R17, R0, R0],
            [R17, R17, R17, R17, R0, R0],
            [R17, R17, R17, R17, R0, R0],
            [B255, B255, B255, B255, G136, G136],
        ]
        decoded = pixels(decode_bc2(b"".join(blocks), 6, 5))
        self.assertEqual(decoded, [pixel for row in expected for pixel in row])

    def test_bc3_6x5(self) -> None:
        # Arriba a la derecha: a0 = 200 > a1 = 100 y alfas de la fila 0 con índices 0, 1, 2, 3:
        # 0 | 1 << 3 | 2 << 6 | 3 << 9 = 0x688. a2 = 1300 // 7 = 185, a3 = 1200 // 7 = 171,
        # pero solo se ven las columnas 0 y 1 (alfas 200 y 100).
        top_right_color = color_block(RED, BLUE, bytes([0xE4, 0, 0, 0]))
        blocks = (
            bytes([17, 17]) + bytes(6) + color_block(RED, RED, bytes(4)),
            bytes([200, 100, 0x88, 0x06]) + bytes(4) + top_right_color,
            bytes([255, 255]) + bytes(6) + color_block(BLUE, BLUE, bytes(4)),
            bytes([136, 136]) + bytes(6) + color_block(GREEN, GREEN, bytes(4)),
        )
        r200 = (255, 0, 0, 200)
        expected = [
            [R17, R17, R17, R17, r200, (0, 0, 255, 100)],
            [R17, R17, R17, R17, r200, r200],
            [R17, R17, R17, R17, r200, r200],
            [R17, R17, R17, R17, r200, r200],
            [B255, B255, B255, B255, G136, G136],
        ]
        decoded = pixels(decode_bc3(b"".join(blocks), 6, 5))
        self.assertEqual(decoded, [pixel for row in expected for pixel in row])


if __name__ == "__main__":
    unittest.main()
