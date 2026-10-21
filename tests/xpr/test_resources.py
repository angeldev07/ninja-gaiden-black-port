"""Tests del parseo de XPR0: cabecera, recursos tipados, región de datos y paletas."""

from __future__ import annotations

import struct
import unittest

from ngb_tools.xpr.errors import XprFormatError, XprTruncatedError
from ngb_tools.xpr.resources import (
    MAX_RESOURCES,
    FormatWord,
    IndexBuffer,
    Palette,
    PaletteSource,
    PushBuffer,
    SizeWord,
    Texture,
    UserData,
    VertexBuffer,
    parse_xpr,
)

from .synthetic import (
    INDEX_COMMON,
    VERTEX_COMMON,
    buffer_header,
    build_xpr,
    format_word,
    palette_header,
    push_header,
    size_word,
    texture_header,
    userdata,
)

DXT1, P8 = 0x0C, 0x0B
# Región de datos: DXT1 8x8 (4 bloques = 32 B), P8 4x4 (16 B) y su paleta (256 * 4 B).
DXT1_DATA = bytes(range(32))
P8_DATA = bytes(range(16))
PALETTE_DATA = bytes(1024)
DATA = DXT1_DATA + P8_DATA + PALETTE_DATA


def sample_xpr() -> bytes:
    """Dos texturas y una paleta, más userdata y buffers como en un KMD."""
    return build_xpr(
        [
            userdata(b"material"),
            texture_header(0, format_word(DXT1, 3, 3)),
            texture_header(32, format_word(P8, 2, 2)),
            palette_header(48),
            buffer_header(VERTEX_COMMON, 0x400),
            buffer_header(INDEX_COMMON, 0x500),
            push_header(0x600, 0x10),
            buffer_header(VERTEX_COMMON, 0x610),
        ],
        DATA,
    )


class ParseXprTests(unittest.TestCase):
    def test_header_sizes(self) -> None:
        raw = sample_xpr()
        xpr = parse_xpr(raw)
        self.assertEqual(xpr.total_size, len(raw))
        self.assertEqual(xpr.header_size, 0x800)

    def test_resources_are_typed_in_header_order(self) -> None:
        kinds = [type(r) for r in parse_xpr(sample_xpr()).resources]
        expected = [
            UserData,
            Texture,
            Texture,
            Palette,
            VertexBuffer,
            IndexBuffer,
            PushBuffer,
            VertexBuffer,
        ]
        self.assertEqual(kinds, expected)

    def test_push_buffer_header_is_24_bytes(self) -> None:
        # Observado en ryu.kmd y r_ryu.kmd: tras Size y AllocationSize viene un u32 a 0.
        resources = parse_xpr(sample_xpr()).resources
        self.assertEqual(resources[7].offset - resources[6].offset, 24)
        last = resources[7]
        assert isinstance(last, VertexBuffer)
        self.assertEqual(last.data, 0x610)

    def test_resource_index_and_header_offset(self) -> None:
        resources = parse_xpr(sample_xpr()).resources
        self.assertEqual([r.index for r in resources], list(range(8)))
        # 12 de cabecera XPR0 + 8 + 8 de userdata: la primera textura empieza en 0x1C.
        self.assertEqual(resources[1].offset, 0x1C)
        self.assertEqual(resources[2].offset, 0x1C + 20)

    def test_texture_fields(self) -> None:
        texture = parse_xpr(sample_xpr()).textures[0]
        self.assertEqual(texture.data, 0)
        self.assertEqual(texture.format.code, DXT1)
        self.assertEqual((texture.format.width, texture.format.height), (8, 8))

    def test_userdata_payload_and_push_buffer_size(self) -> None:
        resources = parse_xpr(sample_xpr()).resources
        user, push = resources[0], resources[6]
        assert isinstance(user, UserData) and isinstance(push, PushBuffer)
        self.assertEqual(user.payload, b"material")
        self.assertEqual(push.size, 0x10)

    def test_data_offsets_are_relative_to_the_header_size(self) -> None:
        xpr = parse_xpr(sample_xpr())
        self.assertEqual(xpr.data_bytes(32, 16), P8_DATA)

    def test_reading_past_the_end_is_truncation(self) -> None:
        xpr = parse_xpr(sample_xpr())
        with self.assertRaises(XprTruncatedError):
            xpr.data_bytes(len(DATA) - 4, 8)

    def test_trailing_bytes_after_total_size_are_ignored(self) -> None:
        raw = sample_xpr()
        self.assertEqual(parse_xpr(raw + bytes(0x100)).total_size, len(raw))


class PaletteTests(unittest.TestCase):
    def test_palette_size_comes_from_the_top_bits_of_common(self) -> None:
        sizes = {code: Palette(0, 0, 0x00830001 | code << 30, 0, 0).entries for code in range(4)}
        self.assertEqual(sizes, {0: 256, 1: 128, 2: 64, 3: 32})

    def test_p8_texture_uses_the_palette_that_follows_it(self) -> None:
        xpr = parse_xpr(sample_xpr())
        link = xpr.palette_link(xpr.textures[1])
        assert link is not None
        self.assertEqual((link.palette.index, link.source), (3, PaletteSource.NEXT))

    def test_without_a_following_palette_a_lone_palette_is_used(self) -> None:
        raw = build_xpr(
            [palette_header(16), texture_header(0, format_word(P8, 2, 2))], bytes(16 + 1024)
        )
        xpr = parse_xpr(raw)
        link = xpr.palette_link(xpr.textures[0])
        assert link is not None
        self.assertEqual((link.palette.index, link.source), (0, PaletteSource.ONLY))

    def test_ambiguous_palette_is_none(self) -> None:
        raw = build_xpr(
            [
                palette_header(16),
                palette_header(16 + 1024),
                texture_header(0, format_word(P8, 2, 2)),
            ],
            bytes(16 + 2048),
        )
        xpr = parse_xpr(raw)
        self.assertIsNone(xpr.palette_link(xpr.textures[0]))

    def test_textures_and_palettes_are_indexed_once_at_parse_time(self) -> None:
        # Antes se recalculaban en cada consulta: O(texturas x recursos) con muchas P8.
        xpr = parse_xpr(sample_xpr())
        self.assertIs(xpr.palettes, xpr.palettes)
        self.assertIs(xpr.textures, xpr.textures)
        self.assertEqual([p.index for p in xpr.palettes], [3])


class ZeroCopyTests(unittest.TestCase):
    def test_the_xpr_is_a_view_of_the_input_not_a_copy(self) -> None:
        raw = sample_xpr() + bytes(0x100)
        xpr = parse_xpr(raw)
        assert isinstance(xpr.blob, memoryview)
        self.assertIs(xpr.blob.obj, raw)
        self.assertEqual(len(xpr.blob), xpr.total_size)

    def test_a_memoryview_input_is_accepted(self) -> None:
        raw = bytes(8) + sample_xpr()
        xpr = parse_xpr(memoryview(raw)[8:])
        self.assertEqual(xpr.data_bytes(32, 16), P8_DATA)


class FieldWordTests(unittest.TestCase):
    def test_format_word_bitfields(self) -> None:
        word = FormatWord(format_word(0x0F, 7, 6, levels=5, cubemap=True, dimensions=3, log2_p=2))
        self.assertEqual(word.code, 0x0F)
        self.assertEqual((word.width, word.height, word.depth), (128, 64, 4))
        self.assertEqual(word.levels, 5)
        self.assertTrue(word.cubemap)
        self.assertEqual(word.dimensions, 3)

    def test_size_word_of_a_linear_texture(self) -> None:
        word = SizeWord(size_word(640, 480, 2560))
        self.assertEqual((word.width, word.height, word.pitch), (640, 480, 2560))


class MalformedXprTests(unittest.TestCase):
    def test_bad_magic(self) -> None:
        with self.assertRaisesRegex(XprFormatError, "magia"):
            parse_xpr(b"XPR1" + sample_xpr()[4:])

    def test_total_size_beyond_the_data_is_truncation(self) -> None:
        with self.assertRaises(XprTruncatedError):
            parse_xpr(sample_xpr()[:-1])

    def test_missing_end_marker(self) -> None:
        # La cabecera acaba justo tras la textura: no queda sitio para el 0xFFFFFFFF.
        header = texture_header(0, format_word(DXT1, 2, 2))
        raw = build_xpr([header], bytes(8), header_size=12 + len(header), end_marker=False)
        with self.assertRaisesRegex(XprFormatError, "fin"):
            parse_xpr(raw)

    def test_unknown_resource_type(self) -> None:
        raw = build_xpr([buffer_header(0x00060001, 0)], bytes(16))
        with self.assertRaisesRegex(XprFormatError, "tipo"):
            parse_xpr(raw)

    def test_userdata_overflowing_the_header(self) -> None:
        raw = bytearray(build_xpr([userdata(b"abcd")], b""))
        struct.pack_into("<I", raw, 16, 0x10000)
        with self.assertRaisesRegex(XprFormatError, "userdata"):
            parse_xpr(bytes(raw))

    def test_header_size_beyond_total_size(self) -> None:
        raw = bytearray(sample_xpr())
        struct.pack_into("<I", raw, 8, len(raw) + 4)
        with self.assertRaisesRegex(XprFormatError, "header_size"):
            parse_xpr(bytes(raw))


class HostileXprTests(unittest.TestCase):
    """Cabeceras manipuladas: siempre un error tipado, nunca una excepción genérica."""

    def patched(self, offset: int, value: int) -> bytes:
        raw = bytearray(sample_xpr())
        struct.pack_into("<I", raw, offset, value)
        return bytes(raw)

    def test_total_size_smaller_than_the_xpr_header(self) -> None:
        with self.assertRaisesRegex(XprFormatError, "total_size"):
            parse_xpr(self.patched(4, 8))

    def test_total_size_of_4_gib_is_truncation(self) -> None:
        with self.assertRaises(XprTruncatedError):
            parse_xpr(self.patched(4, 0xFFFFFFFF))

    def test_header_size_zero(self) -> None:
        with self.assertRaisesRegex(XprFormatError, "header_size"):
            parse_xpr(self.patched(8, 0))

    def test_header_size_of_4_gib(self) -> None:
        with self.assertRaisesRegex(XprFormatError, "header_size"):
            parse_xpr(self.patched(8, 0xFFFFFFFF))

    def test_userdata_size_of_4_gib(self) -> None:
        with self.assertRaisesRegex(XprFormatError, "userdata"):
            parse_xpr(self.patched(16, 0xFFFFFFFF))

    def test_exactly_max_resources_is_accepted(self) -> None:
        raw = build_xpr([userdata(b"")] * MAX_RESOURCES, b"")
        self.assertEqual(len(parse_xpr(raw).resources), MAX_RESOURCES)

    def test_more_than_max_resources_is_refused(self) -> None:
        raw = build_xpr([userdata(b"")] * (MAX_RESOURCES + 1), b"")
        with self.assertRaisesRegex(XprFormatError, str(MAX_RESOURCES)):
            parse_xpr(raw)

    def test_data_offset_near_4_gib_is_truncation(self) -> None:
        xpr = parse_xpr(sample_xpr())
        with self.assertRaises(XprTruncatedError):
            xpr.require_data(0xFFFFFFFF, 1)


if __name__ == "__main__":
    unittest.main()
