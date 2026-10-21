"""Tests del KMD de escenario: partes, push buffers, colisión e incidencias."""

from __future__ import annotations

import unittest
from unittest import mock

from ngb_tools.stage.errors import StageFormatError
from ngb_tools.stage.model import decode_stage_push_buffer, parse_stage_kmd

from . import synthetic
from .synthetic import (
    COLOR,
    SynthPart,
    SynthStageObject,
    build_stage_kmd,
    push_buffer_bytes,
    two_part_object,
)


class StageModelTests(unittest.TestCase):
    def test_two_part_object_pairs_each_part_with_its_buffers(self) -> None:
        model = parse_stage_kmd(build_stage_kmd([two_part_object(), SynthStageObject()]))
        self.assertEqual(model.issues, ())
        self.assertEqual((len(model.objects), model.part_count), (2, 3))
        first = model.objects[0]
        self.assertIsNotNone(first.parts[0].index_buffer)
        self.assertIsNone(first.parts[1].index_buffer)
        refs = [ref for ref, _ in first.submeshes()]
        self.assertEqual(refs, [(0, 1, 0), (0, 0, 0)])
        self.assertEqual(model.push_buffer_of, {(0, 1, 0): 0})
        self.assertEqual(model.submesh_indices((0, 1, 0)), (0, 1, 2, 3))
        self.assertEqual(len(model.triangles((0, 0, 0))), 2)

    def test_vertices_come_per_part_in_world_space(self) -> None:
        model = parse_stage_kmd(build_stage_kmd([two_part_object((10.0, 0.0, 0.0))]))
        part1 = model.vertices(0, 1)
        self.assertEqual(part1.positions[0], (12.0, 0.0, 0.0))
        self.assertEqual(part1.colors, (COLOR,) * 4)
        self.assertEqual(model.world_translation(0), (0.0, 0.0, 0.0))

    def test_zero_scale_is_not_an_issue(self) -> None:
        model = parse_stage_kmd(build_stage_kmd([SynthStageObject(scale=(0.0, 0.0, 0.0))]))
        self.assertEqual(model.issues, ())
        self.assertTrue(model.exportable((0, 0, 0)))

    def test_collision_is_attached_by_object(self) -> None:
        model = parse_stage_kmd(build_stage_kmd([SynthStageObject(), two_part_object()]))
        self.assertEqual(list(model.collision), [1])
        self.assertEqual(model.collision[1].triangles, ((0, 2, 1),))

    def test_push_buffer_garbage_after_end_is_tolerated(self) -> None:
        obj = SynthStageObject(parts=[SynthPart(push_buffer=True, pb_garbage=0xC2F916E6)])
        model = parse_stage_kmd(build_stage_kmd([obj]))
        self.assertEqual(model.issues, ())
        self.assertEqual(model.submesh_indices((0, 0, 0)), (0, 1, 2, 3))

    def test_strict_decoder_is_used_first(self) -> None:
        self.assertEqual(decode_stage_push_buffer(push_buffer_bytes((0, 1, 2))), (0, 1, 2))

    def test_index_out_of_range_is_an_issue_and_not_exportable(self) -> None:
        obj = SynthStageObject(parts=[SynthPart(strip=(0, 1, 9))])
        model = parse_stage_kmd(build_stage_kmd([obj]))
        self.assertFalse(model.exportable((0, 0, 0)))
        self.assertIn("índice 9", str(model.issues[0]))

    def test_more_parts_than_vertex_buffers_is_a_format_error(self) -> None:
        two_parts = synthetic.descriptor(two_part_object())
        with mock.patch.object(synthetic, "descriptor", lambda _obj: two_parts):
            data = build_stage_kmd([SynthStageObject()], collision=False)
        with self.assertRaisesRegex(StageFormatError, "2 partes y hay 1 VB"):
            parse_stage_kmd(data)

    def test_missing_xpr_is_a_format_error(self) -> None:
        with self.assertRaises(StageFormatError):
            parse_stage_kmd(b"garbage" * 8)


if __name__ == "__main__":
    unittest.main()
