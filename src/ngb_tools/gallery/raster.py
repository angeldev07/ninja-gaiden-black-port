"""Minimal z-buffer rasterizer: the KMD from the front, in bind pose, with its textures.

Orthographic projection from +z (the character faces +z): screen x = x, y = y, and the
closest is the greater z. Flat shading with the face normal (absolute value, because there
is no culling) and nearest-neighbor texturing with wrap, like D3D UVs. It only draws the
detail and skin groups, placed as in the exporter (`KmdModel.world_translation`). It is for
the contact sheet: it does not aim to be fast.
"""

from __future__ import annotations

import math
from collections.abc import Mapping
from dataclasses import dataclass
from typing import Final

from ngb_tools.kmd.gltf import DEFAULT_GROUPS, selected_objects
from ngb_tools.kmd.model import KmdModel
from ngb_tools.xpr.texture import RgbaImage

type Vec3 = tuple[float, float, float]
type Vec2 = tuple[float, float]
type Color = tuple[int, int, int]

LIGHT: Final = (0.35, 0.55, 0.75)
FLAT_COLOR: Final[Color] = (170, 170, 180)
MARGIN: Final = 0.06


@dataclass(frozen=True, slots=True)
class Triangle:
    points: tuple[Vec3, Vec3, Vec3]
    uvs: tuple[Vec2, Vec2, Vec2] | None
    texture: int


class Canvas:
    """8-bit RGB with a z-buffer."""

    def __init__(self, width: int, height: int, background: Color) -> None:
        self.width, self.height = width, height
        self.rgb = bytearray(bytes(background) * (width * height))
        self.depth = [-math.inf] * (width * height)

    def triangle(
        self,
        screen: tuple[Vec2, Vec2, Vec2],
        depth: tuple[float, float, float],
        shade: float,
        *,
        color: Color = FLAT_COLOR,
        uvs: tuple[Vec2, Vec2, Vec2] | None = None,
        texture: RgbaImage | None = None,
    ) -> None:
        (x0, y0), (x1, y1), (x2, y2) = screen
        area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
        if abs(area) < 1e-12:
            return
        inv = 1.0 / area
        min_x, max_x = max(int(min(x0, x1, x2)), 0), min(int(max(x0, x1, x2)) + 1, self.width - 1)
        min_y = max(int(min(y0, y1, y2)), 0)
        max_y = min(int(max(y0, y1, y2)) + 1, self.height - 1)
        for py in range(min_y, max_y + 1):
            cy = py + 0.5
            for px in range(min_x, max_x + 1):
                cx = px + 0.5
                w0 = ((x1 - cx) * (y2 - cy) - (x2 - cx) * (y1 - cy)) * inv
                w1 = ((x2 - cx) * (y0 - cy) - (x0 - cx) * (y2 - cy)) * inv
                w2 = 1.0 - w0 - w1
                if w0 < 0 or w1 < 0 or w2 < 0:
                    continue
                z = w0 * depth[0] + w1 * depth[1] + w2 * depth[2]
                i = py * self.width + px
                if z <= self.depth[i]:
                    continue
                self.depth[i] = z
                r, g, b = color
                if texture is not None and uvs is not None:
                    u = w0 * uvs[0][0] + w1 * uvs[1][0] + w2 * uvs[2][0]
                    v = w0 * uvs[0][1] + w1 * uvs[1][1] + w2 * uvs[2][1]
                    tx = int((u - math.floor(u)) * texture.width) % texture.width
                    ty = int((v - math.floor(v)) * texture.height) % texture.height
                    j = 4 * (ty * texture.width + tx)
                    r, g, b = texture.pixels[j], texture.pixels[j + 1], texture.pixels[j + 2]
                self.rgb[3 * i : 3 * i + 3] = bytes(
                    (min(255, int(r * shade)), min(255, int(g * shade)), min(255, int(b * shade)))
                )


def model_triangles(model: KmdModel) -> list[Triangle]:
    """Triangles of the detail and skin groups, in model space."""
    triangles: list[Triangle] = []
    for obj in selected_objects(model, DEFAULT_GROUPS):
        if model.objects[obj].layout is None:
            continue
        data = model.vertices(obj)
        ox, oy, oz = model.world_translation(obj)
        points = [(x + ox, y + oy, z + oz) for x, y, z in data.positions]
        for position, sub in enumerate(model.objects[obj].mesh.submeshes):
            if not model.exportable(obj, position):
                continue
            for a, b, c in model.triangles(obj, position):
                uvs = None
                if data.uvs is not None:
                    uvs = (data.uvs[a], data.uvs[b], data.uvs[c])
                triangles.append(Triangle((points[a], points[b], points[c]), uvs, sub.base_texture))
    return triangles


def _shade(points: tuple[Vec3, Vec3, Vec3]) -> float:
    (ax, ay, az), (bx, by, bz), (cx, cy, cz) = points
    e1, e2 = (bx - ax, by - ay, bz - az), (cx - ax, cy - ay, cz - az)
    n = (
        e1[1] * e2[2] - e1[2] * e2[1],
        e1[2] * e2[0] - e1[0] * e2[2],
        e1[0] * e2[1] - e1[1] * e2[0],
    )
    length = math.sqrt(n[0] ** 2 + n[1] ** 2 + n[2] ** 2) or 1.0
    light = math.sqrt(sum(c * c for c in LIGHT))
    return 0.35 + 0.85 * abs(sum(n[i] * LIGHT[i] for i in range(3))) / (length * light)


def render_front(
    triangles: list[Triangle],
    textures: Mapping[int, RgbaImage],
    width: int,
    height: int,
    background: Color,
) -> bytes:
    """RGB of `width`x`height` with the triangles framed from the front."""
    canvas = Canvas(width, height, background)
    if not triangles:
        return bytes(canvas.rgb)
    xs = [p[0] for t in triangles for p in t.points]
    ys = [p[1] for t in triangles for p in t.points]
    span = max(max(xs) - min(xs), (max(ys) - min(ys)) * width / height, 1e-6)
    scale = width * (1 - 2 * MARGIN) / span
    cx, cy = (max(xs) + min(xs)) / 2, (max(ys) + min(ys)) / 2
    for tri in triangles:
        screen = tuple((width / 2 + (p[0] - cx) * scale, height / 2 - (p[1] - cy) * scale)
                       for p in tri.points)  # fmt: skip
        depth = (tri.points[0][2], tri.points[1][2], tri.points[2][2])
        texture = textures.get(tri.texture)
        canvas.triangle(
            (screen[0], screen[1], screen[2]), depth, _shade(tri.points), uvs=tri.uvs,
            texture=texture,
        )  # fmt: skip
    return bytes(canvas.rgb)
