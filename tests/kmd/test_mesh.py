"""Tests of OBJINFO, HIERARC and the mesh descriptor, each on its own."""

from __future__ import annotations

import struct
import unittest

from ngb_tools.kmd.errors import KmdFormatError
from ngb_tools.kmd.hierarchy import parse_hierarchy
from ngb_tools.kmd.mesh import IndexSource, parse_mesh_descriptor
from ngb_tools.kmd.objinfo import parse_objinfo
from teamninja.synthetic import build_chunk

from .synthetic import SynthObject, descriptor, hierarc, objinfo_entry, parent_and_child


class DescriptorTests(unittest.TestCase):
    def test_fields_material_and_stage(self) -> None:
        mesh = parse_mesh_descriptor(descriptor(SynthObject(texture=3, cull=0, blend=True)))
        self.assertEqual(
            (mesh.fvf, mesh.vertex_count, mesh.stride, mesh.index_count), (0x112, 4, 32, 4)
        )
        (sub,) = mesh.submeshes
        self.assertIs(sub.source, IndexSource.INDEX_BUFFER)
        self.assertEqual((sub.first_index, sub.index_count), (0, 4))
        self.assertTrue(sub.double_sided)
        self.assertTrue(sub.alpha_blend)
        self.assertEqual(sub.blend_factors, (0x302, 1))
        self.assertEqual((sub.stages[0].name, sub.base_texture), ("tx_o", 3))
        assert sub.material is not None
        self.assertAlmostEqual(sub.material.power, 35.0)

    def test_push_buffer_submesh(self) -> None:
        mesh = parse_mesh_descriptor(descriptor(SynthObject(push_buffer=True)))
        self.assertIs(mesh.submeshes[0].source, IndexSource.PUSH_BUFFER)
        self.assertEqual(mesh.index_count, 0)

    def test_trailing_bytes_are_an_error(self) -> None:
        with self.assertRaises(KmdFormatError):
            parse_mesh_descriptor(descriptor(SynthObject()) + bytes(4))

    def test_truncated_descriptor_is_an_error(self) -> None:
        with self.assertRaises(KmdFormatError):
            parse_mesh_descriptor(descriptor(SynthObject())[:-1])

    def test_unknown_index_source_is_an_error(self) -> None:
        raw = bytearray(descriptor(SynthObject()))
        struct.pack_into("<I", raw, 0x28 + 0x10, 24)
        with self.assertRaises(KmdFormatError):
            parse_mesh_descriptor(bytes(raw))


class ObjinfoTests(unittest.TestCase):
    def test_entry_fields(self) -> None:
        obj = SynthObject(translation=(1.0, 2.0, 3.0), group=4, scale=(0.0, 0.0, 0.0))
        (info,) = parse_objinfo(build_chunk(b"OBJINFO", [objinfo_entry(obj)]))
        self.assertEqual(
            (info.translation, info.group, info.submesh_count), ((1.0, 2.0, 3.0), 4, 1)
        )
        self.assertEqual(info.scale, (0.0, 0.0, 0.0))
        self.assertEqual(len(info.submesh_spheres), 1)

    def test_bad_magic_is_an_error(self) -> None:
        entry = b"XX" + objinfo_entry(SynthObject())[2:]
        with self.assertRaises(KmdFormatError):
            parse_objinfo(build_chunk(b"OBJINFO", [entry]))

    def test_empty_slot_is_an_error(self) -> None:
        with self.assertRaises(KmdFormatError):
            parse_objinfo(build_chunk(b"OBJINFO", [objinfo_entry(SynthObject()), None]))


class HierarchyTests(unittest.TestCase):
    def test_parent_children_and_root(self) -> None:
        tree = parse_hierarchy(hierarc(parent_and_child(), root=0))
        self.assertEqual(tree.root, 0)
        self.assertEqual(tree.nodes[0].children, (1,))
        self.assertEqual((tree.nodes[0].parent, tree.nodes[1].parent), (None, 0))
        self.assertEqual(tree.nodes[0].subtree_size, 2)
        self.assertEqual(tree.parent_map(), {1: 0})

    def test_child_out_of_range_is_an_error(self) -> None:
        objects = parent_and_child()
        objects[0].children = (7,)
        with self.assertRaises(KmdFormatError):
            parse_hierarchy(hierarc([objects[0]], root=0))

    def test_root_out_of_range_is_an_error(self) -> None:
        with self.assertRaises(KmdFormatError):
            parse_hierarchy(hierarc(parent_and_child(), root=5))


if __name__ == "__main__":
    unittest.main()
