"""Tests de los exportadores glTF y OBJ sobre KMD sintéticos, con el validador propio."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from ngb_tools.kmd.gltf import ExportOptions, write_gltf
from ngb_tools.kmd.model import KmdModel, parse_kmd
from ngb_tools.kmd.obj import build_obj
from ngb_tools.kmd.textures import resolve_texture_files

from .gltfcheck import load_gltf, read_accessor, validate
from .synthetic import FVF_SKINNED, SynthObject, build_kmd, parent_and_child


class ExportCase(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.kmd_path = self.root / "game" / "test.kmd"
        self.kmd_path.parent.mkdir()

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def export(
        self, objects: list[SynthObject], options: ExportOptions | None = None
    ) -> tuple[KmdModel, dict[str, object], bytes]:
        data = build_kmd(objects)
        self.kmd_path.write_bytes(data)
        model = parse_kmd(data)
        files, _ = resolve_texture_files(model, self.kmd_path, self.root / "textures")
        (gltf_path, _), _ = write_gltf(
            model, self.root / "models", "test", files, options or ExportOptions()
        )
        gltf, binary = load_gltf(gltf_path)
        self.assertEqual(validate(gltf, binary, gltf_path.parent), [])
        return model, gltf, binary


class GltfTests(ExportCase):
    def test_hierarchy_becomes_nested_nodes_with_local_translations(self) -> None:
        _, gltf, _ = self.export(parent_and_child())
        nodes = gltf["nodes"]
        assert isinstance(nodes, list)
        self.assertEqual(len(nodes), 2)
        self.assertEqual(nodes[0]["translation"], [0.0, 1.0, 0.0])
        self.assertEqual(nodes[0]["children"], [1])
        self.assertEqual(nodes[1]["translation"], [2.0, 0.0, 0.0])
        self.assertEqual(gltf["scenes"], [{"nodes": [0]}])

    def test_strip_becomes_ccw_triangle_list_and_position_bounds(self) -> None:
        _, gltf, binary = self.export(parent_and_child())
        prim = gltf["meshes"][1]["primitives"][0]  # type: ignore[index]
        indices = [v[0] for v in read_accessor(gltf, binary, prim["indices"])]
        self.assertEqual(indices, [0, 1, 2, 2, 1, 3])
        position = gltf["accessors"][prim["attributes"]["POSITION"]]  # type: ignore[index]
        self.assertEqual((position["min"], position["max"]), ([0, 0, 0], [1, 1, 0]))
        uvs = read_accessor(gltf, binary, prim["attributes"]["TEXCOORD_0"])
        self.assertEqual(uvs[2], (0.0, -1.0))  # UV tal cual: glTF y D3D comparten origen

    def test_material_points_to_the_exported_png(self) -> None:
        _, gltf, _ = self.export(parent_and_child())
        material = gltf["materials"][0]  # type: ignore[index]
        self.assertIn("baseColorTexture", material["pbrMetallicRoughness"])
        uri = gltf["images"][0]["uri"]  # type: ignore[index]
        self.assertTrue(uri.startswith("../textures/"))
        self.assertTrue((self.root / "models" / uri).is_file())

    def test_blend_and_cull_none_map_to_alpha_mode_and_double_sided(self) -> None:
        objects = parent_and_child()
        objects[1].blend, objects[1].cull = True, 0
        _, gltf, _ = self.export(objects)
        modes = {(m.get("alphaMode"), m.get("doubleSided")) for m in gltf["materials"]}  # type: ignore[union-attr]
        self.assertEqual(modes, {(None, None), ("BLEND", True)})

    def test_default_groups_skip_lod_and_all_groups_include_it(self) -> None:
        objects = [*parent_and_child(), SynthObject(group=5)]
        objects[0].children = (1, 2)
        _, gltf, _ = self.export(objects)
        self.assertEqual(len(gltf["meshes"]), 2)  # type: ignore[arg-type]
        _, everything, _ = self.export(objects, ExportOptions(groups=None))
        self.assertEqual(len(everything["meshes"]), 3)  # type: ignore[arg-type]

    def test_zero_scale_is_exported_visible_and_flagged(self) -> None:
        objects = parent_and_child()
        objects[1].scale = (0.0, 0.0, 0.0)
        _, gltf, _ = self.export(objects)
        node = gltf["nodes"][1]  # type: ignore[index]
        self.assertNotIn("scale", node)
        self.assertTrue(node["extras"]["hidden_by_default"])

    def test_unusable_submesh_is_skipped(self) -> None:
        objects = parent_and_child()
        objects[1].strip = (0, 1, 9)
        _, gltf, _ = self.export(objects)
        self.assertEqual(len(gltf["meshes"]), 1)  # type: ignore[arg-type]
        self.assertNotIn("mesh", gltf["nodes"][1])  # type: ignore[index]

    def test_skin_moves_the_piece_to_model_space_with_two_joints(self) -> None:
        objects = [
            SynthObject(translation=(0.0, 1.0, 0.0), children=(1,)),
            SynthObject(translation=(0.0, 1.0, 0.0), children=(2,)),
            SynthObject(translation=(9.0, 9.0, 9.0), fvf=FVF_SKINNED, weights=(1, 1, 0, 0)),
        ]
        _, gltf, binary = self.export(objects, ExportOptions(skin=True))
        (skin,) = gltf["skins"]  # type: ignore[misc]
        self.assertEqual(skin["joints"], [0, 1])
        skinned = [n for n in gltf["nodes"] if "skin" in n]  # type: ignore[union-attr]
        self.assertEqual(len(skinned), 1)
        prim = gltf["meshes"][skinned[0]["mesh"]]["primitives"][0]  # type: ignore[index]
        positions = read_accessor(gltf, binary, prim["attributes"]["POSITION"])
        self.assertEqual(positions[0], (0.0, 2.0, 0.0))  # local + mundo del padre (0, 2, 0)
        weights = read_accessor(gltf, binary, prim["attributes"]["WEIGHTS_0"])
        self.assertEqual(weights[0], (1.0, 0.0, 0.0, 0.0))
        self.assertEqual(weights[3], (0.0, 1.0, 0.0, 0.0))
        matrices = read_accessor(gltf, binary, skin["inverseBindMatrices"])
        self.assertEqual(matrices[1][12:15], (0.0, -2.0, 0.0))


class ObjTests(ExportCase):
    def test_obj_has_world_positions_flipped_uvs_and_ccw_faces(self) -> None:
        data = build_kmd(parent_and_child())
        self.kmd_path.write_bytes(data)
        model = parse_kmd(data)
        files, _ = resolve_texture_files(model, self.kmd_path, self.root / "textures")
        obj, mtl, stats = build_obj(model, files, self.root / "models", ExportOptions(), "t.mtl")
        lines = obj.splitlines()
        self.assertEqual((stats.objects, stats.triangles), (2, 4))
        vertices = [line for line in lines if line.startswith("v ")]
        self.assertEqual(vertices[4], "v 2.000000 1.000000 0.000000")  # hijo: (2, 1, 0) + local
        self.assertIn("vt 0.000000 2.000000", lines)  # v = -1 -> 1 - v = 2
        self.assertIn("f 5/5/5 6/6/6 7/7/7", lines)
        self.assertIn("map_Kd ../textures/", mtl)


class TextureResolutionTests(ExportCase):
    def test_textures_are_exported_once_and_then_reused(self) -> None:
        data = build_kmd(parent_and_child())
        self.kmd_path.write_bytes(data)
        model = parse_kmd(data)
        files, exported = resolve_texture_files(model, self.kmd_path, self.root / "tex")
        self.assertTrue(exported)
        self.assertEqual(sorted(files), [0])
        again, exported_again = resolve_texture_files(model, self.kmd_path, self.root / "tex")
        self.assertFalse(exported_again)
        self.assertEqual(again, files)

    def test_a_manifest_for_other_data_is_not_trusted(self) -> None:
        data = build_kmd(parent_and_child())
        self.kmd_path.write_bytes(data)
        model = parse_kmd(data)
        resolve_texture_files(model, self.kmd_path, self.root / "tex")
        manifest_path = self.root / "tex" / "_manifest.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["textures"][0]["sha256"] = "0" * 64
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        _, exported = resolve_texture_files(model, self.kmd_path, self.root / "tex")
        self.assertTrue(exported)


if __name__ == "__main__":
    unittest.main()
