"""Triangle strips to a triangle list.

The indices of a KMD are u16 strips without restart (0xFFFF never appears): submeshes join
their strips with degenerate triangles. Triangle i of the strip is (a, b, c) if i is even and
(b, a, c) if it is odd, so that all of them keep the orientation of the first. Oriented that
way, the right-hand-rule normal matches the stored normals in 99.93 % of the triangles of
`ryu.kmd` (14673 of 14683): they are CCW seen from outside, which is what glTF and OBJ
expect. Degenerate triangles (two equal indices) are dropped.
"""

from __future__ import annotations

from collections.abc import Sequence

type Triangle = tuple[int, int, int]


def strip_to_triangles(indices: Sequence[int]) -> list[Triangle]:
    """Non-degenerate triangles of a strip, with the alternating winding fixed."""
    triangles: list[Triangle] = []
    for i in range(len(indices) - 2):
        a, b, c = indices[i], indices[i + 1], indices[i + 2]
        if len({a, b, c}) < 3:
            continue
        triangles.append((a, b, c) if i % 2 == 0 else (b, a, c))
    return triangles
