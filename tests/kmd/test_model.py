"""Tests del KMD completo: emparejamiento, jerarquía, push buffers e invariantes."""

from __future__ import annotations

import struct
import unittest

from ngb_tools.kmd.errors import KmdFormatError
from ngb_tools.kmd.model import parse_kmd
from teamninja.synthetic import build_chunk

from .synthetic import (
    FVF_SKINNED,
    SynthObject,
    build_kmd,
    build_xpr0,
    hierarc,
    objinfo_entry,
    parent_and_child,
)


def messages(data: bytes) -> list[str]:
    return [str(issue) for issue in parse_kmd(data).issues]


class ParentChildTests(unittest.TestCase):
    def setUp(self) -> None:
        self.model = parse_kmd(build_kmd(parent_and_child()))

    def test_parses_without_issues(self) -> None:
        self.assertEqual(self.model.issues, ())
        self.assertEqual(len(self.model.objects), 2)
        self.assertEqual(len(self.model.textures), 1)

    def test_hierarchy_and_world_translation(self) -> None:
        self.assertEqual(self.model.parent(1), 0)
        self.assertIsNone(self.model.parent(0))
        self.assertEqual(self.model.children(0), (1,))
        self.assertEqual(self.model.world_translation(0), (0.0, 1.0, 0.0))
        self.assertEqual(self.model.world_translation(1), (2.0, 1.0, 0.0))

    def test_triangles_come_from_the_strip(self) -> None:
        self.assertEqual(self.model.triangles(1, 0), [(0, 1, 2), (2, 1, 3)])

    def test_vertices_are_local_to_their_node(self) -> None:
        self.assertEqual(self.model.vertices(1).positions[3], (1.0, 1.0, 0.0))


class PushBufferTests(unittest.TestCase):
    def test_push_buffer_submesh_gives_the_same_triangles(self) -> None:
        objects = parent_and_child()
        objects[1].push_buffer = True
        model = parse_kmd(build_kmd(objects))
        self.assertEqual(model.issues, ())
        self.assertIsNone(model.objects[1].index_buffer)
        self.assertEqual(model.triangles(1, 0), [(0, 1, 2), (2, 1, 3)])

    def test_odd_strip_in_a_push_buffer(self) -> None:
        objects = [SynthObject(strip=(0, 1, 2), push_buffer=True)]
        model = parse_kmd(build_kmd(objects))
        self.assertEqual(model.issues, ())
        self.assertEqual(model.triangles(0, 0), [(0, 1, 2)])


class SkinTests(unittest.TestCase):
    def test_skinned_piece_sits_on_its_parent_and_ignores_its_translation(self) -> None:
        objects = [
            SynthObject(translation=(0.0, 1.0, 0.0), children=(1,)),
            SynthObject(translation=(0.0, 1.0, 0.0), children=(2,)),
            SynthObject(translation=(5.0, 5.0, 5.0), fvf=FVF_SKINNED, weights=(1, 1, 0, 0)),
        ]
        model = parse_kmd(build_kmd(objects))
        self.assertEqual(model.issues, ())
        self.assertTrue(model.objects[2].skinned)
        self.assertEqual(model.world_translation(2), (0.0, 2.0, 0.0))
        self.assertEqual(model.skin_joints(2), (0, 1))


class InvariantTests(unittest.TestCase):
    def test_index_out_of_range_is_an_issue_and_the_submesh_is_unusable(self) -> None:
        objects = parent_and_child()
        objects[1].strip = (0, 1, 2, 9)
        model = parse_kmd(build_kmd(objects))
        self.assertTrue(any("índice 9" in str(i) for i in model.issues))
        self.assertFalse(model.exportable(1, 0))
        self.assertTrue(model.exportable(0, 0))

    def test_rotation_is_reported_and_ignored(self) -> None:
        objects = parent_and_child()
        objects[1].rotation = (0.1, 0.0, 0.0)
        self.assertTrue(any("rotación" in m for m in messages(build_kmd(objects))))

    def test_missing_texture_is_an_issue(self) -> None:
        objects = parent_and_child()
        objects[0].texture = 4
        self.assertTrue(any("textura 4" in m for m in messages(build_kmd(objects))))

    def test_stride_that_does_not_match_the_fvf_is_an_issue(self) -> None:
        objects = parent_and_child()
        objects[1].stride_override = 40
        model = parse_kmd(build_kmd(objects))
        self.assertTrue(any("mide 32 bytes" in str(i) for i in model.issues))
        self.assertIsNone(model.objects[1].layout)

    def test_unsupported_fvf_is_an_issue(self) -> None:
        objects = parent_and_child()
        objects[1].fvf = 0x104
        model = parse_kmd(build_kmd(objects))
        self.assertTrue(any("XYZRHW" in str(i) for i in model.issues))

    def test_inconsistent_parent_field_is_an_issue(self) -> None:
        objects = parent_and_child()
        data = bytearray(build_kmd(objects))
        tree = hierarc(objects, root=0)
        start = data.index(tree)
        # El nodo 1 dice que su padre es él mismo... no: que no tiene padre.
        entry = start + struct.unpack_from("<I", tree, 0x30 + 4)[0]
        struct.pack_into("<H", data, entry + 2, 0xFFFF)
        self.assertTrue(any("HIERARC: padre None" in m for m in messages(bytes(data))))

    def test_missing_hierarchy_is_an_issue_and_every_object_is_a_root(self) -> None:
        model = parse_kmd(build_kmd(parent_and_child(), hierarchy=False))
        self.assertIsNone(model.hierarchy)
        self.assertIsNone(model.parent(1))
        self.assertTrue(any("sin HIERARC" in str(i) for i in model.issues))


class FormatErrorTests(unittest.TestCase):
    def test_objects_that_cannot_be_paired_are_an_error(self) -> None:
        objects = parent_and_child()
        objinfo = build_chunk(b"OBJINFO", [objinfo_entry(o) for o in objects[:1]])
        data = build_chunk(b"KMD", [objinfo, build_xpr0(objects), hierarc(objects, 0)])
        with self.assertRaises(KmdFormatError):
            parse_kmd(data)

    def test_missing_objinfo_is_an_error(self) -> None:
        objects = parent_and_child()
        data = build_chunk(b"KMD", [build_xpr0(objects), hierarc(objects, 0)])
        with self.assertRaises(KmdFormatError):
            parse_kmd(data)

    def test_garbage_is_an_error(self) -> None:
        with self.assertRaises(KmdFormatError):
            parse_kmd(b"not a kmd at all" * 4)


if __name__ == "__main__":
    unittest.main()
