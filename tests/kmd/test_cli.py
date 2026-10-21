"""Tests de la CLI: subcomandos, salida y códigos de salida (0 ok, 1 hallazgos, 2 uso/IO)."""

from __future__ import annotations

import contextlib
import io
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from ngb_tools.kmd.cli import ExitCode, default_output_dir, main, model_stem

from .synthetic import build_kmd, parent_and_child

REPO = Path(__file__).resolve().parents[2]


class CliTestCase(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def write(self, name: str, data: bytes) -> str:
        path = self.root / name
        path.write_bytes(data)
        return str(path)

    def run_cli(self, *argv: str) -> tuple[int, str, str]:
        out, err = io.StringIO(), io.StringIO()
        with (
            mock.patch.dict(os.environ, {"NGB_DATA": str(self.root / "data")}),
            contextlib.redirect_stdout(out),
            contextlib.redirect_stderr(err),
        ):
            code = main(list(argv))
        return code, out.getvalue(), err.getvalue()


class InfoTests(CliTestCase):
    def test_summary_of_objects_groups_and_bones(self) -> None:
        code, out, _ = self.run_cli("info", self.write("a.kmd", build_kmd(parent_and_child())))
        self.assertEqual(code, ExitCode.OK)
        self.assertIn("2 objetos, 1 texturas", out)
        self.assertIn("huesos (objetos con hijos): 1", out)

    def test_issues_are_findings(self) -> None:
        objects = parent_and_child()
        objects[1].strip = (0, 1, 9)
        code, out, _ = self.run_cli("info", self.write("a.kmd", build_kmd(objects)))
        self.assertEqual(code, ExitCode.FINDINGS)
        self.assertIn("! obj1", out)

    def test_unreadable_kmd_is_a_finding(self) -> None:
        code, _, err = self.run_cli("info", self.write("a.kmd", b"garbage" * 8))
        self.assertEqual(code, ExitCode.FINDINGS)
        self.assertIn("error:", err)

    def test_missing_file_is_an_io_error(self) -> None:
        code, _, _ = self.run_cli("info", str(self.root / "missing.kmd"))
        self.assertEqual(code, ExitCode.ERROR)


class ExportTests(CliTestCase):
    def test_default_output_goes_under_ngb_data_models(self) -> None:
        kmd = self.write("hero.kmd", build_kmd(parent_and_child()))
        code, out, _ = self.run_cli("export", kmd)
        self.assertEqual(code, ExitCode.OK, out)
        models = self.root / "data" / "extracted" / "models" / "hero"
        self.assertTrue((models / "hero.gltf").is_file())
        self.assertTrue((models / "hero.bin").is_file())
        self.assertTrue((self.root / "data" / "extracted" / "textures" / "hero").is_dir())

    def test_obj_format(self) -> None:
        kmd = self.write("hero.kmd", build_kmd(parent_and_child()))
        code, _, _ = self.run_cli("export", kmd, "--format", "obj", "--out", str(self.root / "o"))
        self.assertEqual(code, ExitCode.OK)
        self.assertTrue((self.root / "o" / "hero.obj").is_file())
        self.assertTrue((self.root / "o" / "hero.mtl").is_file())

    def test_output_inside_the_repo_is_refused_before_writing_anything(self) -> None:
        kmd = self.write("hero.kmd", build_kmd(parent_and_child()))
        target = REPO / "kmd-test-output"
        textures = self.root / "tex"
        code, _, err = self.run_cli(
            "export", kmd, "--out", str(target), "--textures", str(textures)
        )
        self.assertEqual(code, ExitCode.ERROR)
        self.assertIn("repo", err)
        self.assertFalse(target.exists())
        self.assertFalse(textures.exists())

    def test_skipped_submesh_is_a_finding_but_the_rest_is_exported(self) -> None:
        objects = parent_and_child()
        objects[1].strip = (0, 1, 9)
        kmd = self.write("hero.kmd", build_kmd(objects))
        code, out, _ = self.run_cli("export", kmd, "--out", str(self.root / "o"))
        self.assertEqual(code, ExitCode.FINDINGS)
        self.assertIn("aviso: obj1", out)
        self.assertTrue((self.root / "o" / "hero.gltf").is_file())


class NamingTests(unittest.TestCase):
    def test_dot_only_names_are_replaced(self) -> None:
        self.assertEqual(model_stem(Path("..kmd")), "sin_nombre")
        path = default_output_dir(Path("x/0000_ryu.kmd"), {"NGB_DATA": "/d"})
        self.assertEqual(path, Path("/d/extracted/models/0000_ryu"))


if __name__ == "__main__":
    unittest.main()
