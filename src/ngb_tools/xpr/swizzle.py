"""Des-swizzle de texturas de la NV2A (orden Morton o curva Z).

Una textura *swizzled* no guarda los píxeles fila a fila: los ordena por la curva Z. La
posición en memoria de (x, y) sale de intercalar sus bits, primero uno de x y luego uno de
y, empezando por el menos significativo. Para una textura 4x4:

    x = x1 x0, y = y1 y0   ->   offset = y1 x1 y0 x0

Así los píxeles vecinos en 2D quedan cerca en memoria y la caché de texturas acierta más
al filtrar. Si una dimensión es mayor, sus bits sobrantes van seguidos en la parte alta:
en 8x4 el offset es `x2 y1 x1 y0 x0`. Las dos dimensiones deben ser potencias de dos.

Lo que se permuta es el *elemento*: un píxel en los formatos sin comprimir (1, 2 o 4
bytes), o el índice de paleta en P8. Los DXT no se swizzlean: la NV2A los lee como bloques
4x4 en orden lineal (los formatos `L_DXT*`, "linear", de xemu).

Fuentes:
- xemu, `generate_swizzle_masks` y `unswizzle_box`:
  https://github.com/xemu-project/xemu/blob/master/hw/xbox/nv2a/pgraph/swizzle.c
- xemu, subida de DXT sin des-swizzle (`s3tc_decompress_2d` sobre los datos tal cual):
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

# Elemento de 1, 2 o 4 bytes -> código de `array` del mismo tamaño. Los elementos se mueven
# como unidades opacas, así que el orden de bytes de la máquina no importa.
_FOUR_BYTES: Final = next(code for code in "IL" if array(code).itemsize == 4)
_ARRAY_CODES: Final = MappingProxyType({1: "B", 2: "H", 4: _FOUR_BYTES})


def _require_power_of_two(name: str, value: int) -> None:
    if value <= 0 or value & (value - 1):
        raise ValueError(f"{name} = {value}: el swizzle exige una potencia de dos")


def swizzle_masks(width: int, height: int) -> tuple[int, int]:
    """Máscaras de bits de x y de y dentro del offset swizzled (como en xemu).

    En cada vuelta se reparte un bit de offset a x y luego otro a y, mientras a cada
    dimensión le queden bits por colocar.
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
    """Los valores 0..count-1 con sus bits depositados en las posiciones de `mask`.

    Truco de xemu: `(v - mask) & mask` suma 1 dentro de la máscara, dejando que el acarreo
    salte los bits que no son suyos.
    """
    values = [0] * count
    for i in range(1, count):
        values[i] = (values[i - 1] - mask) & mask
    return values


def morton_order(width: int, height: int) -> Iterator[int]:
    """Para cada píxel en orden lineal (fila a fila), su índice en la memoria swizzled.

    Es un iterador: solo guarda los desplazamientos de una fila y de una columna
    (`width + height` enteros), nunca uno por píxel.
    """
    mask_x, mask_y = swizzle_masks(width, height)
    xs = _spread(width, mask_x)
    for row in _spread(height, mask_y):
        yield from map(row.__or__, xs)


def unswizzle(data: bytes, width: int, height: int, element_size: int) -> bytes:
    """Reordena una imagen swizzled de `width`x`height` elementos a orden lineal.

    Como los bits de x y de y no se mezclan, el offset de (x, y) es `xs[x] | ys[y]`: cada
    fila lineal reúne los elementos `ys[y] | xs[x]`. Se reúnen fila a fila con
    `itemgetter` sobre un `array` de elementos (bucles en C, sin un objeto por píxel).

    Memoria: copia de trabajo, salida y `bytes` final, unas 3 veces la imagen, más una tupla
    de `width` enteros por fila. Medido en 1024x1024 ARGB (4 MiB): ver los tests.
    """
    code = _ARRAY_CODES.get(element_size)
    if code is None:
        raise ValueError(f"elementos de {element_size} bytes: solo 1, 2 o 4")
    expected = width * height * element_size
    if len(data) != expected:
        raise ValueError(f"{len(data)} bytes para {width}x{height}x{element_size} = {expected}")
    mask_x, mask_y = swizzle_masks(width, height)
    xs = _spread(width, mask_x)
    elements = array(code, data)
    linear = array(code)
    for row in _spread(height, mask_y):
        picked = itemgetter(*map(row.__or__, xs))(elements)
        # Con un solo índice, itemgetter devuelve el elemento y no una tupla.
        linear.extend(picked if width > 1 else (picked,))
    return linear.tobytes()
