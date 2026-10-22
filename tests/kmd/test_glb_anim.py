"""Tests of the GLB and the glTF animations, and of `--format glb` / `--motions` in the CLI."""

from __future__ import annotations

import contextlib
import io
import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from ngb_tools.kmd.cli import ExitCode, main, split_motion_spec
from ngb_tools.kmd.gltf import ExportOptions, build_glb, write_gltf
from ngb_tools.kmd.model import parse_kmd
from ngb_tools.kmd.motion import parse_motion_bank
from ngb_tools.kmd.rig import find_rig, motion_clip
from ngb_tools.kmd.textures import resolve_texture_files

from .gltfcheck import load_glb, read_accessor, validate
from .synthetic import build_kmd, parent_and_child
from .synthetic_motion import build_bank, chr_dat, humanoid, standing_motion


class TempCase(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def write(self, name: str, data: bytes) -> Path:
        path = self.root / name
        path.write_bytes(data)
        return path


class GlbTests(TempCase):
    def test_glb_is_self_contained_and_valid(self) -> None:
        kmd = self.write("hero.kmd", build_kmd(parent_and_child()))
        model = parse_kmd(kmd.read_bytes())
        files, _ = resolve_texture_files(model, kmd, self.root / "tex")
        blob, stats = build_glb(model, files, ExportOptions())
        gltf, binary = load_glb(blob)
        self.assertEqual(validate(gltf, binary), [])
        self.assertEqual(stats.objects, 2)
        self.assertNotIn("uri", gltf["buffers"][0])
        (image,) = gltf["images"]
        self.assertEqual(image["mimeType"], "image/png")
        view = gltf["bufferViews"][image["bufferView"]]
        png = binary[view["byteOffset"] : view["byteOffset"] + view["byteLength"]]
        self.assertEqual(png, files[0].read_bytes())
        self.assertEqual(len(blob) % 4, 0)


class AnimationExportTests(TempCase):
    def export(self) -> tuple[dict[str, object], bytes]:
        kmd = self.write("hero.kmd", build_kmd(humanoid()))
        model = parse_kmd(kmd.read_bytes())
        bank = parse_motion_bank(build_bank([standing_motion(frames=2)], [0]))
        clip = motion_clip(find_rig(model), bank.motion(0), "hero#0")
        files, _ = resolve_texture_files(model, kmd, self.root / "tex")
        options = ExportOptions(animations=(clip,))
        (path, _), stats = write_gltf(model, self.root / "out", "hero", files, options)
        self.assertEqual(stats.animations, 1)
        gltf = json.loads(path.read_text(encoding="utf-8"))
        binary = (path.parent / "hero.bin").read_bytes()
        self.assertEqual(validate(gltf, binary, path.parent), [])
        return gltf, binary

    def test_channels_and_samplers_per_bone(self) -> None:
        gltf, binary = self.export()
        (animation,) = gltf["animations"]  # type: ignore[misc]
        self.assertEqual(animation["name"], "hero#0")
        self.assertEqual(len(animation["channels"]), 16)
        self.assertTrue(all(s["interpolation"] == "LINEAR" for s in animation["samplers"]))
        first = animation["channels"][0]
        self.assertEqual(first["target"]["path"], "translation")
        node = gltf["nodes"][first["target"]["node"]]  # type: ignore[index]
        self.assertEqual(node["extras"]["kmd_object"], 0)
        sampler = animation["samplers"][first["sampler"]]
        times = [v[0] for v in read_accessor(gltf, binary, sampler["input"])]
        self.assertEqual(len(times), 3)
        self.assertAlmostEqual(times[2], 2 / 60, places=6)
        for value in read_accessor(gltf, binary, sampler["output"]):
            for got, want in zip(value, (0.0, 0.9, 0.0), strict=True):
                self.assertAlmostEqual(got, want, places=5)


class CliTests(TempCase):
    def run_cli(self, *argv: str) -> tuple[int, str, str]:
        out, err = io.StringIO(), io.StringIO()
        with (
            mock.patch.dict(os.environ, {"NGB_DATA": str(self.root / "data")}),
            contextlib.redirect_stdout(out),
            contextlib.redirect_stderr(err),
        ):
            code = main(list(argv))
        return code, out.getvalue(), err.getvalue()

    def setUp(self) -> None:
        super().setUp()
        self.kmd = str(self.write("hero.kmd", build_kmd(humanoid())))
        self.bank = str(
            self.write(
                "hero.mtn", chr_dat(build_bank([standing_motion(), standing_motion(3)], [0, 1]))
            )
        )

    def test_spec_splitting_keeps_windows_drives(self) -> None:
        self.assertEqual(split_motion_spec("C:/x/ryu.mtn", "all"), (Path("C:/x/ryu.mtn"), "all"))
        self.assertEqual(
            split_motion_spec("C:/x/ryu.mtn:1,90", "all"), (Path("C:/x/ryu.mtn"), "1,90")
        )
        self.assertEqual(split_motion_spec("ryu.mtn:all", "3"), (Path("ryu.mtn"), "all"))

    def test_glb_with_selected_motions(self) -> None:
        out = self.root / "o"
        code, text, _ = self.run_cli(
            "export", self.kmd, "--format", "glb", "--out", str(out), "--motions", f"{self.bank}:1"
        )
        self.assertEqual(code, ExitCode.OK, text)
        gltf, binary = load_glb((out / "hero.glb").read_bytes())
        self.assertEqual(validate(gltf, binary), [])
        self.assertEqual([a["name"] for a in gltf["animations"]], ["hero#1"])

    def test_default_motion_selection_is_every_distinct_motion(self) -> None:
        out = self.root / "o"
        code, _, _ = self.run_cli("export", self.kmd, "--out", str(out), "--motions", self.bank)
        self.assertEqual(code, ExitCode.OK)
        gltf = json.loads((out / "hero.gltf").read_text(encoding="utf-8"))
        self.assertEqual([a["name"] for a in gltf["animations"]], ["hero#0", "hero#1"])

    def test_motions_with_obj_is_a_usage_error(self) -> None:
        code, _, err = self.run_cli("export", self.kmd, "--format", "obj", "--motions", self.bank)
        self.assertEqual(code, ExitCode.ERROR)
        self.assertIn("OBJ", err)

    def test_bad_default_selection_is_a_usage_error(self) -> None:
        code, _, _ = self.run_cli("export", self.kmd, "--motions", self.bank, "--motion", "x")
        self.assertEqual(code, ExitCode.ERROR)

    def test_model_without_rig_is_a_finding(self) -> None:
        plain = str(self.write("plain.kmd", build_kmd(parent_and_child())))
        code, _, err = self.run_cli("export", plain, "--motions", self.bank)
        self.assertEqual(code, ExitCode.FINDINGS)
        self.assertIn("error:", err)


if __name__ == "__main__":
    unittest.main()
