"""HITMDL tests: `VHD` records with several blocks."""

from __future__ import annotations

import struct
import unittest

from ngb_tools.stage.collision import parse_collision_record, parse_hitmdl
from ngb_tools.stage.errors import StageFormatError
from teamninja.synthetic import build_chunk

from .synthetic import vhd_record

FLOOR = (((0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 0.0, 1.0)), ((0, 2, 1),))
WALL = (
    ((5.0, 0.0, 0.0), (5.0, 2.0, 0.0), (5.0, 0.0, 2.0), (5.0, 2.0, 2.0)),
    ((0, 1, 2), (1, 3, 2)),
)


class CollisionRecordTests(unittest.TestCase):
    def test_blocks_are_merged_with_their_vertex_base(self) -> None:
        mesh = parse_collision_record(3, vhd_record([FLOOR, WALL], attr=8))
        self.assertEqual((mesh.obj, mesh.attribute), (3, 8))
        self.assertEqual(len(mesh.vertices), 7)
        self.assertEqual(mesh.vertices[3], (5.0, 0.0, 0.0))
        self.assertEqual(mesh.triangles, ((0, 2, 1), (3, 4, 5), (4, 6, 5)))
        self.assertEqual(mesh.surfaces, (0, 1, 1))

    def test_bad_magic_is_a_format_error(self) -> None:
        raw = b"XXX\0" + vhd_record([FLOOR])[4:]
        with self.assertRaisesRegex(StageFormatError, "magic"):
            parse_collision_record(0, raw)

    def test_triangle_outside_the_block_is_a_format_error(self) -> None:
        raw = bytearray(vhd_record([FLOOR]))
        struct.pack_into("<H", raw, 0x30 + 0x50, 7)
        with self.assertRaisesRegex(StageFormatError, "out of range"):
            parse_collision_record(0, bytes(raw))

    def test_offsets_beyond_the_record_are_a_format_error(self) -> None:
        raw = vhd_record([FLOOR])
        with self.assertRaisesRegex(StageFormatError, "do not fit"):
            parse_collision_record(0, raw[:-4])


class HitmdlTests(unittest.TestCase):
    def test_slot_k_is_object_k_and_empty_slots_have_no_collision(self) -> None:
        data = build_chunk(b"HITMDL", [None, vhd_record([FLOOR]), None])
        meshes = parse_hitmdl(data, 3)
        self.assertEqual(list(meshes), [1])

    def test_chunk_without_records(self) -> None:
        self.assertEqual(parse_hitmdl(build_chunk(b"HITMDL", [None, None]), 2), {})

    def test_slot_count_must_match_the_objects(self) -> None:
        with self.assertRaisesRegex(StageFormatError, "2 slots but there are 3"):
            parse_hitmdl(build_chunk(b"HITMDL", [None, None]), 3)

    def test_other_tag_is_a_format_error(self) -> None:
        with self.assertRaisesRegex(StageFormatError, "HITMDL"):
            parse_hitmdl(build_chunk(b"TEXANM", [None]), 1)


if __name__ == "__main__":
    unittest.main()
