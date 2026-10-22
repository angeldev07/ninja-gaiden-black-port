"""Tests of the multi-part descriptor and of multi-part objects in a character KMD."""

from __future__ import annotations

import struct
import unittest
from pathlib import Path

from ngb_tools.kmd.errors import KmdFormatError
from ngb_tools.kmd.gltf import ExportOptions, build_gltf
from ngb_tools.kmd.mesh import IndexSource, parse_mesh_descriptor, parse_multipart_descriptor
from ngb_tools.kmd.model import FILL_COLOR, parse_kmd

from .gltfcheck import read_accessor, validate
from .multipart import (
    COLOR,
    build_multipart_kmd,
    one_part_descriptor,
    submesh,
    two_part_descriptor,
)
from .synthetic import SynthObject, build_kmd, descriptor, parent_and_child


class MultipartDescriptorTests(unittest.TestCase):
    def test_one_part_is_exactly_the_classic_descriptor(self) -> None:
        payload = descriptor(SynthObject(texture=2, push_buffer=True))
        parsed = parse_multipart_descriptor(payload)
        self.assertEqual(parsed.parts, (parse_mesh_descriptor(payload),))
        self.assertEqual(parsed.order, ((0, 0),))
        self.assertEqual(parsed.header, struct.unpack_from("<10I", payload))

    def test_submeshes_go_to_the_part_at_0x04(self) -> None:
        parsed = parse_multipart_descriptor(two_part_descriptor())
        self.assertEqual(parsed.order, ((1, 0), (0, 0)))
        first, second = parsed.parts
        self.assertEqual((first.fvf, first.index_count), (0x112, 4))
        self.assertEqual((second.fvf, second.index_count), (0x142, 0))
        self.assertIs(second.submeshes[0].source, IndexSource.PUSH_BUFFER)
        self.assertEqual(parsed.submesh(0).index, 0)
        self.assertEqual(len(parsed.header), 2 + 8 * 2)

    def test_zero_parts_is_an_empty_object(self) -> None:
        parsed = parse_multipart_descriptor(struct.pack("<2I", 0, 0))
        self.assertEqual((parsed.parts, parsed.order, parsed.submesh_count), ((), (), 0))

    def test_part_out_of_range_is_a_format_error(self) -> None:
        raw = struct.pack("<2I", 1, 1) + bytes(0x20) + submesh(0, 1, False)
        with self.assertRaisesRegex(KmdFormatError, "part 1, but there are 1"):
            parse_multipart_descriptor(raw)

    def test_trailing_bytes_are_a_format_error(self) -> None:
        with self.assertRaisesRegex(KmdFormatError, "the USERDATA is"):
            parse_multipart_descriptor(one_part_descriptor() + bytes(4))

    def test_truncated_record_is_a_format_error(self) -> None:
        raw = struct.pack("<2I", 1, 1) + bytes(0x20) + submesh(0, 0, False)[:0x40]
        with self.assertRaisesRegex(KmdFormatError, "truncated"):
            parse_multipart_descriptor(raw)

    def test_too_many_parts_is_a_format_error(self) -> None:
        with self.assertRaisesRegex(KmdFormatError, "cap"):
            parse_multipart_descriptor(struct.pack("<2I", 99, 0))


class MultipartModelTests(unittest.TestCase):
    def setUp(self) -> None:
        self.model = parse_kmd(build_multipart_kmd())

    def test_parts_take_their_own_vertex_and_index_buffers(self) -> None:
        model = self.model
        self.assertEqual(model.issues, ())
        obj = model.objects[0]
        self.assertEqual([p.mesh.fvf for p in obj.parts], [0x112, 0x142])
        self.assertEqual([p.base for p in obj.parts], [0, 4])
        self.assertIsNotNone(obj.parts[0].index_buffer)
        self.assertIsNone(obj.parts[1].index_buffer)
        self.assertEqual(obj.order, ((1, 0), (0, 0)))
        self.assertEqual((obj.mesh.vertex_count, obj.mesh.index_count), (8, 4))
        self.assertEqual(len(model.objects[1].parts), 1)

    def test_push_buffers_are_keyed_by_object_part_and_position(self) -> None:
        self.assertEqual(dict(self.model.push_buffer_of), {(0, 1, 0): 0})

    def test_indices_are_offset_by_the_part_base(self) -> None:
        model = self.model
        self.assertEqual(model.part_indices(0, 0), (0, 1, 2, 3))
        self.assertEqual(model.submesh_indices(0, 0), (4, 5, 6, 7))
        self.assertEqual(model.submesh_indices(0, 1), (0, 1, 2, 3))
        self.assertEqual(model.triangles(0, 0), [(4, 5, 6), (6, 5, 7)])

    def test_merged_vertices_fill_what_a_part_lacks(self) -> None:
        data = self.model.vertices(0)
        self.assertEqual(len(data), 8)
        self.assertEqual(data.positions[4], (2.0, 0.0, 0.0))
        assert data.normals is not None and data.colors is not None and data.uvs is not None
        # Part 1 (0x142) has no normals: they are smoothed from its triangles, which face +Z.
        self.assertEqual(data.normals, ((0.0, 0.0, 1.0),) * 8)
        self.assertEqual(data.colors, (FILL_COLOR,) * 4 + (COLOR,) * 4)
        self.assertEqual(data.uvs[5], (1.0, 0.0))
        self.assertIsNone(data.weights)
        self.assertFalse(self.model.objects[0].skinned)

    def test_gltf_export_is_valid_and_has_both_submeshes(self) -> None:
        gltf, data, stats = build_gltf(
            self.model, {}, Path(), ExportOptions(groups=None), "test.bin"
        )
        gltf["buffers"] = [{"byteLength": len(data)}]
        self.assertEqual(validate(gltf, data), [])
        mesh = gltf["meshes"][0]
        self.assertEqual(len(mesh["primitives"]), 2)
        positions = read_accessor(gltf, data, mesh["primitives"][0]["attributes"]["POSITION"])
        self.assertEqual(len(positions), 8)
        self.assertEqual(stats.triangles, 6)

    def test_header_counts_vertex_buffers(self) -> None:
        model = parse_kmd(build_multipart_kmd(vb_count=2))
        self.assertIn("the header says 2 VB, found 3", [str(i) for i in model.issues])

    def test_parts_that_do_not_match_the_buffers_are_an_error(self) -> None:
        three = struct.pack("<2I", 3, 0) + bytes(3 * 0x20)
        with self.assertRaisesRegex(KmdFormatError, "4 parts and 3 VB"):
            parse_kmd(build_multipart_kmd(descriptor=three))


class SinglePartTests(unittest.TestCase):
    def test_single_part_objects_keep_their_buffers(self) -> None:
        model = parse_kmd(build_kmd(parent_and_child()))
        for obj in model.objects:
            (part,) = obj.parts
            self.assertIs(obj.mesh, part.mesh)
            self.assertEqual(
                (obj.vertex_buffer, obj.index_buffer), (part.vertex_buffer, part.index_buffer)
            )
            self.assertEqual((part.base, obj.order), (0, ((0, 0),)))
        self.assertEqual(model.issues, ())


if __name__ == "__main__":
    unittest.main()
