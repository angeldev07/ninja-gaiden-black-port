"""Tests de tiras a triángulos: orden alternado, degenerados y orientación CCW."""

from __future__ import annotations

import unittest

from ngb_tools.kmd.topology import strip_to_triangles

from .synthetic import QUAD_POSITIONS


def normal(a: tuple[float, ...], b: tuple[float, ...], c: tuple[float, ...]) -> tuple[float, ...]:
    e1 = [b[i] - a[i] for i in range(3)]
    e2 = [c[i] - a[i] for i in range(3)]
    return (
        e1[1] * e2[2] - e1[2] * e2[1],
        e1[2] * e2[0] - e1[0] * e2[2],
        e1[0] * e2[1] - e1[1] * e2[0],
    )


class StripTests(unittest.TestCase):
    def test_four_indices_make_two_triangles_with_the_odd_one_flipped(self) -> None:
        self.assertEqual(strip_to_triangles([0, 1, 2, 3]), [(0, 1, 2), (2, 1, 3)])

    def test_both_triangles_of_the_quad_face_the_stored_normal(self) -> None:
        for tri in strip_to_triangles([0, 1, 2, 3]):
            n = normal(*(QUAD_POSITIONS[i] for i in tri))
            self.assertGreater(n[2], 0, tri)

    def test_degenerate_joins_are_dropped_and_parity_is_kept(self) -> None:
        # Dos tiras unidas con degenerados: 0 1 2 3 | 3 4 | 4 5 6 7
        strip = [0, 1, 2, 3, 3, 4, 4, 5, 6, 7]
        self.assertEqual(
            strip_to_triangles(strip),
            [(0, 1, 2), (2, 1, 3), (4, 5, 6), (6, 5, 7)],
        )

    def test_short_strips_have_no_triangles(self) -> None:
        self.assertEqual(strip_to_triangles([]), [])
        self.assertEqual(strip_to_triangles([0, 1]), [])


if __name__ == "__main__":
    unittest.main()
