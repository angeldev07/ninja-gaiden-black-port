"""Tiras de triángulos (triangle strips) a lista de triángulos.

Los índices de un KMD son tiras u16 sin reinicio (no aparece 0xFFFF): las submallas unen
sus tiras con triángulos degenerados. El triángulo i de la tira es (a, b, c) si i es par y
(b, a, c) si es impar, para que todos conserven la orientación del primero. Así orientados,
la normal por la regla de la mano derecha coincide con las normales guardadas en el 99,93 %
de los triángulos de `ryu.kmd` (14673 de 14683): son CCW vistos desde fuera, lo que esperan
glTF y OBJ. Los degenerados (dos índices iguales) se descartan.
"""

from __future__ import annotations

from collections.abc import Sequence

type Triangle = tuple[int, int, int]


def strip_to_triangles(indices: Sequence[int]) -> list[Triangle]:
    """Triángulos no degenerados de una tira, con el orden alternado corregido."""
    triangles: list[Triangle] = []
    for i in range(len(indices) - 2):
        a, b, c = indices[i], indices[i + 1], indices[i + 2]
        if len({a, b, c}) < 3:
            continue
        triangles.append((a, b, c) if i % 2 == 0 else (b, a, c))
    return triangles
