"""Tests de la CLI y de la escena glTF: zona de 2 partes (+ una repetida y una de `_cmn`)."""

from __future__ import annotations

import contextlib
import io
import json
import os
import struct
import tempfile
import unittest
from pathlib import Path
from typing import Any
from unittest import mock

from kmd.gltfcheck import load_gltf, validate
from ngb_tools.stage.cli import ExitCode, default_output_dir, main

from .test_zone import write_game

REPO = Path(__file__).resolve().parents[2]


def read_glb(path: Path) -> tuple[dict[str, Any], bytes]:
    data = path.read_bytes()
    magic, version, total = struct.unpack_from("<4sII", data)
    assert (magic, version, total) == (b"glTF", 2, len(data))
    json_len, json_type = struct.unpack_from("<II", data, 12)
    assert json_type == 0x4E4F534A
    gltf = json.loads(data[20 : 20 + json_len])
    bin_len, bin_type = struct.unpack_from("<II", data, 20 + json_len)
    assert bin_type == 0x004E4942
    binary = data[28 + json_len : 28 + json_len + bin_len]
    return gltf, binary[: gltf["buffers"][0]["byteLength"]]


class CliTestCase(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.game = write_game(self.root)
        self.out = self.root / "out"

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def run_cli(self, *argv: str) -> tuple[int, str, str]:
        out, err = io.StringIO(), io.StringIO()
        with (
            mock.patch.dict(os.environ, {"NGB_DATA": str(self.root / "data")}),
            contextlib.redirect_stdout(out),
            contextlib.redirect_stderr(err),
        ):
            code = main(list(argv))
        return code, out.getvalue(), err.getvalue()

    def export(self, *extra: str) -> tuple[int, str]:
        code, out, _ = self.run_cli(
            "export", "boff", "--game", str(self.game), "--out", str(self.out), *extra
        )
        return code, out


class ExportGlbTests(CliTestCase):
    def test_zone_is_one_scene_with_a_node_per_part(self) -> None:
        code, out = self.export("--collision")
        self.assertEqual(code, ExitCode.OK, out)
        self.assertIn("boff_03: idéntica a boff_01, se salta", out)
        gltf, data = read_glb(self.out / "boff.glb")
        self.assertEqual(validate(gltf, data), [])
        nodes = gltf["nodes"]
        (root,) = gltf["scenes"][0]["nodes"]
        self.assertEqual(nodes[root]["name"], "boff")
        parts = [nodes[i]["name"] for i in nodes[root]["children"]]
        self.assertEqual(parts, ["boff_01", "boff_02", "boff_99"])

    def test_objects_have_no_transform_and_zero_scale_is_not_hidden(self) -> None:
        self.export()
        gltf, _ = read_glb(self.out / "boff.glb")
        objects = [n for n in gltf["nodes"] if "_obj" in n["name"]]
        self.assertEqual(len(objects), 4)  # 1 + 1 + 2 (boff_03 se salta)
        for node in objects:
            self.assertNotIn("scale", node)
            self.assertNotIn("translation", node)
            self.assertNotIn("hidden_by_default", node["extras"])
            self.assertEqual(node["extras"]["kmd_scale"], [0.0, 0.0, 0.0])

    def test_two_part_object_is_one_mesh_with_two_attribute_sets(self) -> None:
        self.export()
        gltf, _ = read_glb(self.out / "boff.glb")
        mesh = next(m for m in gltf["meshes"] if m["name"] == "boff_01_obj000")
        first, second = mesh["primitives"]
        self.assertEqual((first["extras"]["kmd_part"], second["extras"]["kmd_part"]), (1, 0))
        self.assertNotEqual(first["attributes"]["POSITION"], second["attributes"]["POSITION"])
        self.assertIn("COLOR_0", first["attributes"])
        self.assertEqual(gltf["accessors"][first["attributes"]["POSITION"]]["min"], [2, 0, 0])

    def test_images_are_embedded_png(self) -> None:
        self.export()
        gltf, data = read_glb(self.out / "boff.glb")
        for image in gltf["images"]:
            view = gltf["bufferViews"][image["bufferView"]]
            self.assertEqual(image["mimeType"], "image/png")
            start = view["byteOffset"]
            self.assertEqual(data[start : start + 4], b"\x89PNG")
        self.assertTrue((self.root / "data/extracted/textures/boff_01/_manifest.json").is_file())

    def test_collision_node_is_translucent_and_marked(self) -> None:
        self.export("--collision")
        gltf, _ = read_glb(self.out / "boff.glb")
        node = next(n for n in gltf["nodes"] if n["name"] == "boff_01_collision")
        self.assertTrue(node["extras"]["collision"])
        material = gltf["materials"][gltf["meshes"][node["mesh"]]["primitives"][0]["material"]]
        self.assertEqual((material["alphaMode"], material["doubleSided"]), ("BLEND", True))

    def test_without_the_flag_there_is_no_collision(self) -> None:
        self.export()
        gltf, _ = read_glb(self.out / "boff.glb")
        self.assertFalse(any(n["name"].endswith("_collision") for n in gltf["nodes"]))

    def test_selected_parts_only(self) -> None:
        code, out = self.export("--parts", "02")
        self.assertEqual(code, ExitCode.OK, out)
        gltf, _ = read_glb(self.out / "boff.glb")
        self.assertEqual(
            [n["name"] for n in gltf["nodes"] if n["name"].startswith("boff_")][-1], "boff_02"
        )
        self.assertNotIn("boff_01", [n["name"] for n in gltf["nodes"]])


class ExportGltfTests(CliTestCase):
    def test_gltf_with_external_bin_and_images(self) -> None:
        code, out = self.export("--format", "gltf")
        self.assertEqual(code, ExitCode.OK, out)
        gltf, data = load_gltf(self.out / "boff.gltf")
        self.assertEqual(validate(gltf, data, self.out), [])
        self.assertTrue(all("uri" in image for image in gltf["images"]))


class CommandTests(CliTestCase):
    def test_info_lists_parts_and_duplicates(self) -> None:
        code, out, _ = self.run_cli("info", "boff", "--game", str(self.game))
        self.assertEqual(code, ExitCode.OK)
        self.assertIn("= boff_01", out)
        self.assertIn("boff_99", out)
        self.assertIn("2 VB", out)

    def test_unknown_zone_is_a_usage_error(self) -> None:
        code, _, err = self.run_cli(
            "export", "tomb", "--game", str(self.game), "--out", str(self.out)
        )
        self.assertEqual(code, ExitCode.ERROR)
        self.assertIn("no hay AFS", err)

    def test_output_inside_the_repo_is_refused(self) -> None:
        code, _, err = self.run_cli(
            "export", "boff", "--game", str(self.game), "--out", str(REPO / "build-x")
        )
        self.assertEqual(code, ExitCode.ERROR)
        self.assertIn("prohibida", err)

    def test_default_output_dir(self) -> None:
        with mock.patch.dict(os.environ, {"NGB_DATA": str(self.root)}):
            self.assertEqual(default_output_dir("BOFF"), self.root / "extracted/stages/boff")


if __name__ == "__main__":
    unittest.main()
