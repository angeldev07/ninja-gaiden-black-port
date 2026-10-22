"""Un-swizzling of NV2A textures (Morton order or Z-curve).

A *swizzled* texture does not store pixels row by row: it orders them along the Z-curve. The
memory position of (x, y) comes from interleaving their bits, first one from x and then one from
y, starting at the least significant. For a 4x4 texture:

    x = x1 x0, y = y1 y0   ->   offset = y1 x1 y0 x0

This way neighboring pixels in 2D end up close in memory and the texture cache hits more
when filtering. If one dimension is larger, its leftover bits follow in the high part:
in 8x4 the offset is `x2 y1 x1 y0 x0`. Both dimensions must be powers of two.

What is permuted is the *element*: a pixel in uncompressed formats (1, 2 or 4
bytes), or the palette index in P8. DXT is not swizzled: the NV2A reads it as 4x4
blocks in linear order (xemu's `L_DXT*` "linear" formats).

Sources:
- xemu, `generate_swizzle_masks` and `unswizzle_box`:
  https://github.com/xemu-project/xemu/blob/master/hw/xbox/nv2a/pgraph/swizzle.c
- xemu, DXT upload without un-swizzling (`s3tc_decompress_2d` over the data as-is):
  https://github.com/xemu-project/xemu/blob/master/hw/xbox/nv2a/pgraph/gl/texture.c
- F. Giesen, "Texture tiling and swizzling":
  https://fgiesen.wordpress.com/2011/01/17/texture-tiling-and-swizzling/
"""

from __future__ import annotations

from array import array
from collections.abc import Iterator
from operator import itemgetter
from types import MappingProxyType
from typing import Final

# 1-, 2- or 4-byte element -> `array` code of the same size. Elements are moved
# as opaque units, so the machine's byte order does not matter.
_FOUR_BYTES: Final = next(code for code in "IL" if array(code).itemsize == 4)
_ARRAY_CODES: Final = MappingProxyType({1: "B", 2: "H", 4: _FOUR_BYTES})


def _require_power_of_two(name: str, value: int) -> None:
    if value <= 0 or value & (value - 1):
        raise ValueError(f"{name} = {value}: swizzling requires a power of two")


def swizzle_masks(width: int, height: int) -> tuple[int, int]:
    """Bit masks of x and y within the swizzled offset (as in xemu).

    On each turn one offset bit is given to x and then another to y, while each
    dimension still has bits left to place.
    """
    _require_power_of_two("width", width)
    _require_power_of_two("height", height)
    mask_x = mask_y = 0
    mask_bit = 1
    bit = 1
    while bit < width or bit < height:
        if bit < width:
            mask_x |= mask_bit
            mask_bit <<= 1
        if bit < height:
            mask_y |= mask_bit
            mask_bit <<= 1
        bit <<= 1
    return mask_x, mask_y


def _spread(count: int, mask: int) -> list[int]:
    """The values 0..count-1 with their bits deposited at the positions of `mask`.

    xemu trick: `(v - mask) & mask` adds 1 within the mask, letting the carry
    skip the bits that are not its own.
    """
    values = [0] * count
    for i in range(1, count):
        values[i] = (values[i - 1] - mask) & mask
    return values


def morton_order(width: int, height: int) -> Iterator[int]:
    """For each pixel in linear order (row by row), its index in swizzled memory.

    It is an iterator: it only stores the offsets of one row and one column
    (`width + height` integers), never one per pixel.
    """
    mask_x, mask_y = swizzle_masks(width, height)
    xs = _spread(width, mask_x)
    for row in _spread(height, mask_y):
        yield from map(row.__or__, xs)


def unswizzle(data: bytes, width: int, height: int, element_size: int) -> bytes:
    """Reorder a swizzled image of `width`x`height` elements into linear order.

    Since the x and y bits do not mix, the offset of (x, y) is `xs[x] | ys[y]`: each linear
    row gathers the elements `ys[y] | xs[x]`. They are gathered row by row with
    `itemgetter` over an `array` of elements (C loops, no object per pixel).

    Memory: working copy, output and final `bytes`, about 3 times the image, plus a tuple
    of `width` integers per row. Measured on 1024x1024 ARGB (4 MiB): see the tests.
    """
    code = _ARRAY_CODES.get(element_size)
    if code is None:
        raise ValueError(f"{element_size}-byte elements: only 1, 2 or 4")
    expected = width * height * element_size
    if len(data) != expected:
        raise ValueError(f"{len(data)} bytes for {width}x{height}x{element_size} = {expected}")
    mask_x, mask_y = swizzle_masks(width, height)
    xs = _spread(width, mask_x)
    elements = array(code, data)
    linear = array(code)
    for row in _spread(height, mask_y):
        picked = itemgetter(*map(row.__or__, xs))(elements)
        # With a single index, itemgetter returns the element and not a tuple.
        linear.extend(picked if width > 1 else (picked,))
    return linear.tobytes()
