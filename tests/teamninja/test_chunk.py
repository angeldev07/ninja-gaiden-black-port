"""Tests de la cabecera de chunk de Team Ninja y de la búsqueda del slot XPR0."""

from __future__ import annotations

import struct
import unittest

from ngb_tools.teamninja import (
    MAX_SLOTS,
    ChunkFormatError,
    SlotNotFoundError,
    find_xpr0_slot,
    parse_chunk,
)

from .synthetic import build_chunk

OBJINFO = b"OBJINFO\0" + bytes(24)
XPR = b"XPR0" + bytes(28)


class ParseChunkTests(unittest.TestCase):
    def test_header_fields_are_decoded(self) -> None:
        data = build_chunk(b"KMD", [OBJINFO, XPR, None], table_pos=0x80)
        chunk = parse_chunk(data)
        self.assertEqual(chunk.tag, "KMD")
        self.assertEqual(chunk.size, len(data))
        self.assertEqual(chunk.n_slots, 3)
        self.assertEqual(chunk.n_used, 2)
        self.assertEqual(chunk.table_pos, 0x80)

    def test_only_used_slots_are_listed_with_their_extent(self) -> None:
        data = build_chunk(b"KMD", [OBJINFO, None, XPR], table_pos=0x80)
        chunk = parse_chunk(data)
        self.assertEqual([slot.index for slot in chunk.slots], [0, 2])
        first, second = chunk.slots
        self.assertEqual(first.offset, 0x90)
        self.assertEqual(first.end, second.offset)
        self.assertEqual(second.end, len(data))
        self.assertEqual(second.size, len(XPR))

    def test_last_slot_extends_to_the_end_of_the_data(self) -> None:
        data = build_chunk(b"KMD", [OBJINFO, XPR], trailing=0x110)
        chunk = parse_chunk(data)
        self.assertEqual(chunk.size, len(data) - 0x110)
        self.assertEqual(chunk.slots[-1].end, len(data))

    def test_size_field_does_not_bound_the_slots(self) -> None:
        # Como en r_ryu.kmd: el campo 0x0C vale menos que el offset del último slot.
        data = bytearray(build_chunk(b"KMD", [OBJINFO, XPR]))
        struct.pack_into("<I", data, 0x0C, 0x40)
        chunk = parse_chunk(bytes(data))
        self.assertEqual(chunk.size, 0x40)
        self.assertEqual(chunk.slots[-1].end, len(data))

    def test_chunk_without_used_slots_has_no_slots(self) -> None:
        # Como el HITMDL de los escenarios sin colisión: tabla entera a 0.
        chunk = parse_chunk(build_chunk(b"HITMDL", [None, None]))
        self.assertEqual((chunk.n_slots, chunk.n_used, chunk.slots), (2, 0, ()))

    def test_truncated_header_is_a_format_error(self) -> None:
        with self.assertRaises(ChunkFormatError):
            parse_chunk(b"KMD\0" + bytes(8))

    def test_non_printable_tag_is_a_format_error(self) -> None:
        data = bytearray(build_chunk(b"KMD", [XPR]))
        data[0:3] = b"\x01\x02\x03"
        with self.assertRaisesRegex(ChunkFormatError, "tag"):
            parse_chunk(bytes(data))

    def test_header_size_other_than_0x20_is_a_format_error(self) -> None:
        data = bytearray(build_chunk(b"KMD", [XPR]))
        struct.pack_into("<I", data, 0x1C, 0x40)
        with self.assertRaisesRegex(ChunkFormatError, "0x1C"):
            parse_chunk(bytes(data))

    def test_slot_offset_outside_the_chunk_is_a_format_error(self) -> None:
        data = bytearray(build_chunk(b"KMD", [XPR]))
        struct.pack_into("<I", data, 0x20, len(data) + 0x10)
        with self.assertRaisesRegex(ChunkFormatError, "slot 0"):
            parse_chunk(bytes(data))

    def test_used_count_must_match_the_table(self) -> None:
        data = bytearray(build_chunk(b"KMD", [XPR, None]))
        struct.pack_into("<I", data, 0x14, 2)
        with self.assertRaisesRegex(ChunkFormatError, "n_used"):
            parse_chunk(bytes(data))


class HostileChunkTests(unittest.TestCase):
    """Cabeceras manipuladas: error tipado sin leer una tabla gigante."""

    def patched(self, offset: int, value: int) -> bytes:
        data = bytearray(build_chunk(b"KMD", [OBJINFO, XPR]))
        struct.pack_into("<I", data, offset, value)
        return bytes(data)

    def test_huge_slot_count_is_refused_before_reading_the_table(self) -> None:
        with self.assertRaisesRegex(ChunkFormatError, "n_slots"):
            parse_chunk(self.patched(0x10, 0xFFFFFFFF))

    def test_slot_count_above_the_cap(self) -> None:
        with self.assertRaisesRegex(ChunkFormatError, str(MAX_SLOTS)):
            parse_chunk(self.patched(0x10, MAX_SLOTS + 1))

    def test_exactly_max_slots_is_accepted(self) -> None:
        data = build_chunk(b"KMD", [XPR] + [None] * (MAX_SLOTS - 1))
        self.assertEqual(parse_chunk(data).n_slots, MAX_SLOTS)

    def test_table_position_beyond_the_data(self) -> None:
        with self.assertRaisesRegex(ChunkFormatError, "tabla"):
            parse_chunk(self.patched(0x18, 0xFFFFFFF0))

    def test_used_count_larger_than_the_table(self) -> None:
        with self.assertRaisesRegex(ChunkFormatError, "n_used"):
            parse_chunk(self.patched(0x14, 0xFFFFFFFF))


class FindXpr0SlotTests(unittest.TestCase):
    def test_returns_the_slot_that_starts_with_xpr0(self) -> None:
        data = build_chunk(b"KMD", [OBJINFO, XPR, None], table_pos=0x80)
        slot = find_xpr0_slot(data)
        self.assertEqual(slot.index, 1)
        self.assertEqual(data[slot.offset : slot.offset + 4], b"XPR0")

    def test_chunk_without_xpr0_raises_slot_not_found(self) -> None:
        with self.assertRaises(SlotNotFoundError):
            find_xpr0_slot(build_chunk(b"KMD", [OBJINFO]))

    def test_two_xpr0_slots_are_ambiguous(self) -> None:
        with self.assertRaisesRegex(SlotNotFoundError, "2 slots"):
            find_xpr0_slot(build_chunk(b"KMD", [XPR, XPR]))


if __name__ == "__main__":
    unittest.main()
