"""Tests de la disposición de mips y de la decodificación del nivel 0 de una textura."""

from __future__ import annotations

import struct
import unittest
from unittest import mock

from ngb_tools.xpr.errors import (
    TextureTooLargeError,
    UnsupportedTextureError,
    XprFormatError,
    XprTruncatedError,
)
from ngb_tools.xpr.formats import FORMATS
from ngb_tools.xpr.resources import FormatWord, SizeWord, Texture, parse_xpr
from ngb_tools.xpr.texture import (
    MAX_TEXTURE_PIXELS,
    AlphaMode,
    alpha_mode,
    check_texture,
    decode_texture,
    mip_chain,
    texture_layout,
)

from .synthetic import (
    build_xpr,
    d3dcolor,
    format_word,
    palette_header,
    size_word,
    solid_dxt1_block,
    texture_header,
)
from .test_swizzle import MORTON_4X4, MORTON_8X4, flatten

DXT1, P8, A8, A8R8G8B8, LIN_A8R8G8B8, D24S8 = 0x0C, 0x0B, 0x19, 0x06, 0x12, 0x2A
RED, BLUE = 0xF800, 0x001F


def bare_texture(fmt: int, size: int = 0) -> Texture:
    return Texture(0, 0, 0x00040001, 0, 0, FormatWord(fmt), SizeWord(size))


def levels_of(fmt: int, size: int = 0) -> list[tuple[int, int, int, int]]:
    layout = texture_layout(bare_texture(fmt, size))
    return [(m.width, m.height, m.offset, m.size) for m in layout.levels]


class MipChainTests(unittest.TestCase):
    def test_dxt1_levels_are_packed_back_to_back(self) -> None:
        # 64x64 = 16x16 bloques de 8 B; cada nivel es la cuarta parte del anterior.
        levels = levels_of(format_word(DXT1, 6, 6, levels=4))
        self.assertEqual(
            levels,
            [(64, 64, 0, 2048), (32, 32, 2048, 512), (16, 16, 2560, 128), (8, 8, 2688, 32)],
        )

    def test_dxt_levels_below_4x4_still_take_a_whole_block(self) -> None:
        sizes = [size for *_, size in levels_of(format_word(DXT1, 3, 3, levels=4))]
        self.assertEqual(sizes, [32, 8, 8, 8])

    def test_swizzled_levels_stop_at_1x1(self) -> None:
        # 8x4 ARGB declara 10 niveles pero solo caben 4: 8x4, 4x2, 2x1, 1x1.
        levels = levels_of(format_word(A8R8G8B8, 3, 2, levels=10))
        self.assertEqual(levels, [(8, 4, 0, 128), (4, 2, 128, 32), (2, 1, 160, 8), (1, 1, 168, 4)])

    def test_zero_declared_levels_means_one(self) -> None:
        self.assertEqual(len(levels_of(format_word(DXT1, 2, 2, levels=0))), 1)

    def test_linear_texture_uses_size_and_pitch(self) -> None:
        fmt = format_word(LIN_A8R8G8B8, 0, 0)
        layout = texture_layout(bare_texture(fmt, size_word(3, 2, 64)))
        self.assertEqual((layout.width, layout.height, layout.pitch), (3, 2, 64))
        self.assertEqual(layout.byte_size, 128)

    def test_mip_chain_matches_the_layout(self) -> None:
        chain = mip_chain(FORMATS[DXT1], 16, 16, 2)
        self.assertEqual([(m.level, m.offset) for m in chain], [(0, 0), (1, 128)])


class UnsupportedLayoutTests(unittest.TestCase):
    def test_cubemap_is_detected(self) -> None:
        with self.assertRaisesRegex(UnsupportedTextureError, "cubemap"):
            texture_layout(bare_texture(format_word(DXT1, 4, 4, cubemap=True)))

    def test_volume_is_detected(self) -> None:
        with self.assertRaisesRegex(UnsupportedTextureError, "volumen"):
            texture_layout(bare_texture(format_word(A8, 4, 4, dimensions=3, log2_p=2)))

    def test_unknown_format_code(self) -> None:
        with self.assertRaisesRegex(UnsupportedTextureError, "0x08"):
            texture_layout(bare_texture(format_word(0x08, 4, 4)))

    def test_known_but_undecodable_format_has_a_layout_but_no_decoder(self) -> None:
        # Se sabe cuántos bytes ocupa (para el sha256 del manifiesto), no cómo pintarlo.
        layout = texture_layout(bare_texture(format_word(D24S8, 4, 4)))
        self.assertEqual(layout.byte_size, 16 * 16 * 4)
        xpr = parse_xpr(build_xpr([texture_header(0, format_word(D24S8, 4, 4))], bytes(1024)))
        with self.assertRaisesRegex(UnsupportedTextureError, "D24S8"):
            decode_texture(xpr, xpr.textures[0])

    def test_linear_pitch_shorter_than_a_row(self) -> None:
        # 32 píxeles ARGB = 128 bytes por fila, pero el pitch declarado es 64.
        texture = bare_texture(format_word(LIN_A8R8G8B8, 0, 0), size_word(32, 2, 64))
        with self.assertRaisesRegex(UnsupportedTextureError, "pitch"):
            texture_layout(texture)

    def test_dimensions_other_than_two_or_three(self) -> None:
        with self.assertRaisesRegex(UnsupportedTextureError, r"^1 dimensiones"):
            texture_layout(bare_texture(format_word(DXT1, 4, 0, dimensions=1)))

    def test_linear_texture_without_size_word(self) -> None:
        with self.assertRaisesRegex(UnsupportedTextureError, "Size"):
            texture_layout(bare_texture(format_word(LIN_A8R8G8B8, 0, 0)))


class DecodeTextureTests(unittest.TestCase):
    """XPR0 sintético con dos texturas (DXT1 8x4 y P8 4x4 swizzled) y una paleta."""

    def setUp(self) -> None:
        dxt1 = solid_dxt1_block(RED) + solid_dxt1_block(BLUE)  # 2 bloques: 16 B
        p8 = bytes(range(16))  # memoria swizzled: el byte k es el índice k
        palette = b"".join(d3dcolor(k, 0, 0) for k in range(256))
        self.xpr = parse_xpr(
            build_xpr(
                [
                    texture_header(0, format_word(DXT1, 3, 2)),
                    texture_header(16, format_word(P8, 2, 2)),
                    palette_header(32),
                ],
                dxt1 + p8 + palette,
            )
        )

    def test_dxt1_decodes_its_blocks_in_order(self) -> None:
        image = decode_texture(self.xpr, self.xpr.textures[0])
        self.assertEqual((image.width, image.height), (8, 4))
        self.assertEqual(image.pixels[0:4], bytes([255, 0, 0, 255]))
        self.assertEqual(image.pixels[16:20], bytes([0, 0, 255, 255]))

    def test_p8_is_unswizzled_before_the_palette_lookup(self) -> None:
        image = decode_texture(self.xpr, self.xpr.textures[1])
        self.assertEqual(list(image.pixels[0::4]), flatten(MORTON_4X4))
        self.assertEqual(set(image.pixels[3::4]), {255})

    def test_p8_without_palette_is_unsupported(self) -> None:
        xpr = parse_xpr(build_xpr([texture_header(0, format_word(P8, 2, 2))], bytes(16)))
        with self.assertRaisesRegex(UnsupportedTextureError, "paleta"):
            decode_texture(xpr, xpr.textures[0])

    def test_swizzled_a8_follows_the_morton_order(self) -> None:
        xpr = parse_xpr(build_xpr([texture_header(0, format_word(A8, 3, 2))], bytes(range(32))))
        image = decode_texture(xpr, xpr.textures[0])
        self.assertEqual(list(image.pixels[3::4]), flatten(MORTON_8X4))

    def test_only_level_zero_is_decoded(self) -> None:
        # 8x8 DXT1 con 2 niveles: nivel 0 rojo (4 bloques) y nivel 1 azul (1 bloque).
        data = solid_dxt1_block(RED) * 4 + solid_dxt1_block(BLUE)
        xpr = parse_xpr(build_xpr([texture_header(0, format_word(DXT1, 3, 3, levels=2))], data))
        image = decode_texture(xpr, xpr.textures[0])
        self.assertEqual((image.width, image.height), (8, 8))
        self.assertEqual(set(struct.iter_unpack("4B", image.pixels)), {(255, 0, 0, 255)})

    def test_data_past_the_xpr_is_truncation(self) -> None:
        xpr = parse_xpr(build_xpr([texture_header(0, format_word(DXT1, 3, 3))], bytes(16)))
        with self.assertRaises(XprTruncatedError):
            decode_texture(xpr, xpr.textures[0])


class CheckTextureTests(unittest.TestCase):
    """`check_texture` valida sin decodificar: lo que `list` necesita para avisar."""

    def test_valid_texture_returns_its_layout(self) -> None:
        xpr = parse_xpr(build_xpr([texture_header(0, format_word(DXT1, 3, 3))], bytes(32)))
        check = check_texture(xpr, xpr.textures[0])
        self.assertEqual((check.layout.byte_size, check.present_levels), (32, 1))

    def test_missing_low_mips_keep_level_zero(self) -> None:
        # El nivel 0 cabe (32 B) pero el 1 no: se exporta el 0 y se avisa de lo que falta.
        header = texture_header(0, format_word(DXT1, 3, 3, levels=2))
        xpr = parse_xpr(build_xpr([header], bytes(32)))
        check = check_texture(xpr, xpr.textures[0])
        self.assertEqual((check.present_levels, check.missing_levels), (1, 1))
        self.assertEqual(check.present_bytes, 32)
        self.assertEqual(decode_texture(xpr, xpr.textures[0]).width, 8)

    def test_level_zero_past_the_end_is_truncation(self) -> None:
        header = texture_header(0, format_word(DXT1, 3, 3, levels=2))
        xpr = parse_xpr(build_xpr([header], bytes(16)))
        with self.assertRaises(XprTruncatedError):
            check_texture(xpr, xpr.textures[0])

    def test_palette_past_the_end_is_truncation(self) -> None:
        headers = [texture_header(0, format_word(P8, 2, 2)), palette_header(16)]
        xpr = parse_xpr(build_xpr(headers, bytes(16 + 512)))
        with self.assertRaises(XprTruncatedError):
            check_texture(xpr, xpr.textures[0])

    def test_undecodable_format_is_unsupported(self) -> None:
        xpr = parse_xpr(build_xpr([texture_header(0, format_word(D24S8, 2, 2))], bytes(64)))
        with self.assertRaisesRegex(UnsupportedTextureError, "D24S8"):
            check_texture(xpr, xpr.textures[0])


class HostileTextureTests(unittest.TestCase):
    """Cabeceras de textura manipuladas: se rechazan antes de reservar memoria."""

    def test_absurd_dimensions_are_refused_by_the_layout(self) -> None:
        # log2 = 15: 32768x32768 RGBA serían 4 GiB.
        with self.assertRaisesRegex(TextureTooLargeError, "32768x32768"):
            texture_layout(bare_texture(format_word(A8R8G8B8, 15, 15)))

    def test_too_large_is_a_kind_of_unsupported(self) -> None:
        self.assertTrue(issubclass(TextureTooLargeError, UnsupportedTextureError))

    def test_absurd_texture_in_a_tiny_xpr_is_refused_before_decoding(self) -> None:
        xpr = parse_xpr(build_xpr([texture_header(0, format_word(DXT1, 15, 15))], bytes(16)))
        with self.assertRaises(TextureTooLargeError):
            decode_texture(xpr, xpr.textures[0])

    def test_limit_is_on_the_pixel_count(self) -> None:
        self.assertEqual(MAX_TEXTURE_PIXELS, 4096 * 4096)
        self.assertEqual(texture_layout(bare_texture(format_word(DXT1, 12, 12))).width, 4096)
        with self.assertRaises(TextureTooLargeError):
            texture_layout(bare_texture(format_word(DXT1, 13, 12)))

    def test_data_offset_near_4_gib_is_truncation(self) -> None:
        xpr = parse_xpr(build_xpr([texture_header(0xFFFFFFF0, format_word(DXT1, 2, 2))], b""))
        with self.assertRaises(XprTruncatedError):
            check_texture(xpr, xpr.textures[0])

    def test_indices_past_a_short_palette_are_transparent(self) -> None:
        # Paleta de 32 entradas (código 3) e índices 40: no hay entrada, negro transparente.
        headers = [texture_header(0, format_word(P8, 2, 2)), palette_header(16, size_code=3)]
        palette = b"".join(d3dcolor(255, 255, 255) for _ in range(32))
        xpr = parse_xpr(build_xpr(headers, bytes([40] * 16) + palette))
        image = decode_texture(xpr, xpr.textures[0])
        self.assertEqual(image.pixels, bytes(4 * 16))


class DecoderErrorTests(unittest.TestCase):
    def test_decoder_value_errors_become_format_errors(self) -> None:
        # La API pública solo lanza errores tipados: un ValueError interno no escapa.
        xpr = parse_xpr(build_xpr([texture_header(0, format_word(DXT1, 2, 2))], bytes(8)))
        broken = mock.patch("ngb_tools.xpr.texture.decode_bc1", side_effect=ValueError("roto"))
        with broken, self.assertRaisesRegex(XprFormatError, "roto"):
            decode_texture(xpr, xpr.textures[0])


class AlphaModeTests(unittest.TestCase):
    """DXT2/DXT3 y DXT4/DXT5 comparten código: el alfa recto solo se puede suponer o probar."""

    DXT5 = FORMATS[0x0F]

    def test_formats_without_a_premultiplied_twin_are_straight(self) -> None:
        self.assertEqual(alpha_mode(FORMATS[DXT1], bytes(4)), AlphaMode.STRAIGHT)

    def test_a_channel_above_alpha_proves_straight_alpha(self) -> None:
        # Premultiplicado exige R, G, B <= A: (200, 0, 0, 100) no puede serlo.
        rgba = bytes([10, 10, 10, 100, 200, 0, 0, 100])
        self.assertEqual(alpha_mode(self.DXT5, rgba), AlphaMode.STRAIGHT_PROVEN)

    def test_opaque_texture_is_the_same_either_way(self) -> None:
        rgba = bytes([10, 20, 30, 255, 0, 0, 0, 255])
        self.assertEqual(alpha_mode(self.DXT5, rgba), AlphaMode.STRAIGHT_PROVEN)

    def test_colors_never_above_alpha_leave_it_assumed(self) -> None:
        rgba = bytes([10, 10, 10, 100, 0, 0, 0, 0])
        self.assertEqual(alpha_mode(self.DXT5, rgba), AlphaMode.STRAIGHT_ASSUMED)


if __name__ == "__main__":
    unittest.main()
