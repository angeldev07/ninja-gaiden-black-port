"""Tests del descriptor de malla con varias partes."""

from __future__ import annotations

import struct
import unittest

from ngb_tools.kmd.mesh import IndexSource
from ngb_tools.stage.descriptor import parse_stage_descriptor
from ngb_tools.stage.errors import StageFormatError

from .synthetic import SynthPart, SynthStageObject, descriptor, submesh_record


class StageDescriptorTests(unittest.TestCase):
    def test_single_part_is_the_character_layout(self) -> None:
        parsed = parse_stage_descriptor(descriptor(SynthStageObject()))
        (part,) = parsed.parts
        self.assertEqual(
            (part.fvf, part.vertex_count, part.stride, part.index_count), (0x142, 4, 24, 4)
        )
        self.assertEqual(parsed.order, ((0, 0),))

    def test_submeshes_go_to_the_part_named_by_field_0x04(self) -> None:
        obj = SynthStageObject(
            parts=[SynthPart(), SynthPart(push_buffer=True, strip=(0, 1, 2))], order=[1, 0, 1]
        )
        parsed = parse_stage_descriptor(descriptor(obj))
        self.assertEqual(parsed.order, ((1, 0), (0, 0), (1, 1)))
        first, second = parsed.parts
        self.assertEqual(len(first.submeshes), 1)
        self.assertEqual(first.index_count, 4)
        self.assertEqual([s.index for s in second.submeshes], [0, 2])
        self.assertTrue(all(s.source is IndexSource.PUSH_BUFFER for s in second.submeshes))
        self.assertEqual(second.index_count, 0)
        self.assertEqual(parsed.submesh_count, 3)

    def test_zero_parts_is_an_empty_object(self) -> None:
        parsed = parse_stage_descriptor(struct.pack("<2I", 0, 0))
        self.assertEqual((parsed.parts, parsed.order), ((), ()))

    def test_part_out_of_range_is_a_format_error(self) -> None:
        raw = bytearray(descriptor(SynthStageObject()))
        struct.pack_into("<I", raw, 8 + 0x20 + 4, 1)  # submalla 0 -> parte 1 de 1
        with self.assertRaisesRegex(StageFormatError, "parte 1, hay 1"):
            parse_stage_descriptor(bytes(raw))

    def test_trailing_bytes_are_a_format_error(self) -> None:
        with self.assertRaisesRegex(StageFormatError, "USERDATA mide"):
            parse_stage_descriptor(descriptor(SynthStageObject()) + bytes(4))

    def test_truncated_record_is_a_format_error(self) -> None:
        raw = struct.pack("<2I", 1, 1) + bytes(0x20) + submesh_record(0, 0, SynthPart())[:0x40]
        with self.assertRaisesRegex(StageFormatError, "cortado"):
            parse_stage_descriptor(raw)

    def test_too_many_parts_is_a_format_error(self) -> None:
        with self.assertRaisesRegex(StageFormatError, "tope"):
            parse_stage_descriptor(struct.pack("<2I", 99, 0))


if __name__ == "__main__":
    unittest.main()
