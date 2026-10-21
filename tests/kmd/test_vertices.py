"""Tests de la disposición FVF y la decodificación de vértices."""

from __future__ import annotations

import struct
import unittest

from ngb_tools.kmd.errors import KmdFormatError, UnsupportedVertexFormatError
from ngb_tools.kmd.vertices import decode_vertices, vertex_layout


class LayoutTests(unittest.TestCase):
    def test_strides_of_the_formats_seen_in_the_game(self) -> None:
        self.assertEqual(vertex_layout(0x112).stride, 32)
        self.assertEqual(vertex_layout(0x116).stride, 36)
        self.assertEqual(vertex_layout(0x042).stride, 16)
        self.assertEqual(vertex_layout(0x142).stride, 24)

    def test_offsets_follow_the_d3d_order(self) -> None:
        layout = vertex_layout(0x116)
        self.assertEqual((layout.weight_count, layout.normal, layout.texcoord), (1, 16, 28))
        colored = vertex_layout(0x142)
        self.assertEqual((colored.normal, colored.diffuse, colored.texcoord), (None, 12, 16))

    def test_texcoord_size_bits_change_the_stride(self) -> None:
        # TEX1 con coordenadas de 3 componentes (bits 16-17 = 1)
        self.assertEqual(vertex_layout(0x102 | (1 << 16)).stride, 24)

    def test_transformed_positions_and_unknown_bits_are_rejected(self) -> None:
        for fvf in (0x104, 0x112 | 0x1000, 0x00C):
            with self.subTest(fvf=hex(fvf)), self.assertRaises(UnsupportedVertexFormatError):
                vertex_layout(fvf)


class DecodeTests(unittest.TestCase):
    def test_static_vertex(self) -> None:
        raw = struct.pack("<8f", 1, 2, 3, 0, 0, 1, 0.25, -0.5)
        data = decode_vertices(raw, vertex_layout(0x112), 1)
        self.assertEqual(data.positions, ((1.0, 2.0, 3.0),))
        self.assertEqual(data.normals, ((0.0, 0.0, 1.0),))
        self.assertEqual(data.uvs, ((0.25, -0.5),))
        self.assertIsNone(data.weights)

    def test_skinned_vertex_has_its_blend_weight(self) -> None:
        raw = struct.pack("<9f", 1, 2, 3, 0.75, 0, 1, 0, 0.5, 0.5)
        data = decode_vertices(raw, vertex_layout(0x116), 1)
        self.assertEqual(data.weights, (0.75,))
        self.assertEqual(data.normals, ((0.0, 1.0, 0.0),))

    def test_colored_vertex_keeps_the_d3dcolor(self) -> None:
        raw = struct.pack("<3fI", 0, 0, 0, 0xFF7F7F7F)
        data = decode_vertices(raw, vertex_layout(0x042), 1)
        self.assertEqual(data.colors, (0xFF7F7F7F,))
        self.assertIsNone(data.uvs)

    def test_short_buffer_is_an_error(self) -> None:
        with self.assertRaises(KmdFormatError):
            decode_vertices(bytes(31), vertex_layout(0x112), 1)


if __name__ == "__main__":
    unittest.main()
