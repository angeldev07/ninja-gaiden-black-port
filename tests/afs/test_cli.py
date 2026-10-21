"""Tests de la CLI: subcomandos, salida y códigos de salida (0 ok, 1 hallazgos, 2 uso/IO)."""

from __future__ import annotations

import contextlib
import io
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from ngb_tools.afs.cli import ExitCode, main

from .synthetic import FILES, build_afs

REPO = Path(__file__).resolve().parents[2]


class CliTestCase(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.afs_path = self.root / "sample.afs"
        self.afs_path.write_bytes(build_afs(FILES))

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def run_cli(self, *argv: str) -> tuple[int, str, str]:
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = main(list(argv))
        return code, out.getvalue(), err.getvalue()

    def write_misaligned(self) -> Path:
        buf = bytearray(build_afs(FILES, "none"))
        struct.pack_into("<II", buf, 8 + 8 * 1, 0x1810, 0x10)
        path = self.root / "bad.afs"
        path.write_bytes(bytes(buf))
        return path


class ListCommandTests(CliTestCase):
    def test_clean_archive_lists_entries_and_exits_ok(self) -> None:
        code, out, _ = self.run_cli("list", str(self.afs_path))
        self.assertEqual(code, ExitCode.OK)
        self.assertIn("menu.xpr", out)

    def test_issues_are_printed_and_exit_with_findings(self) -> None:
        code, out, _ = self.run_cli("list", str(self.write_misaligned()))
        self.assertEqual(code, ExitCode.FINDINGS)
        self.assertIn("! misaligned #1:", out)

    def test_non_afs_file_is_a_finding(self) -> None:
        self.afs_path.write_bytes(b"NOPE" + bytes(12))
        code, _, err = self.run_cli("list", str(self.afs_path))
        self.assertEqual(code, ExitCode.FINDINGS)
        self.assertIn("magia inválida", err)

    def test_missing_file_is_an_io_error(self) -> None:
        code, _, err = self.run_cli("list", str(self.root / "missing.afs"))
        self.assertEqual(code, ExitCode.ERROR)
        self.assertIn("error:", err)

    def test_main_leaves_stdout_configuration_alone(self) -> None:
        stream = io.TextIOWrapper(io.BytesIO(), encoding="ascii", errors="strict")
        with contextlib.redirect_stdout(stream), contextlib.redirect_stderr(io.StringIO()):
            main(["list", str(self.afs_path)])
        self.assertEqual(stream.errors, "strict")

    def test_missing_subcommand_is_a_usage_error(self) -> None:
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as raised:
            main([])
        self.assertEqual(raised.exception.code, ExitCode.ERROR)


class ExtractCommandTests(CliTestCase):
    def test_extract_reports_each_file(self) -> None:
        out_dir = self.root / "out"
        code, out, _ = self.run_cli("extract", str(self.afs_path), str(out_dir), "--index", "1")
        self.assertEqual(code, ExitCode.OK)
        self.assertIn("0001_song.zlib", out)
        self.assertIn("1 entradas ->", out)

    def test_unknown_index_is_a_usage_error(self) -> None:
        code, _, err = self.run_cli("extract", str(self.afs_path), str(self.root), "--index", "9")
        self.assertEqual(code, ExitCode.ERROR)
        self.assertIn("fuera de rango", err)

    def test_negative_index_is_a_usage_error(self) -> None:
        out_dir = self.root / "out"
        code, _, err = self.run_cli("extract", str(self.afs_path), str(out_dir), "--index", "-1")
        self.assertEqual(code, ExitCode.ERROR)
        self.assertIn("fuera de rango", err)
        self.assertFalse(out_dir.exists())

    def test_output_inside_repo_is_a_usage_error(self) -> None:
        target = REPO / "build" / "nope"
        code, _, err = self.run_cli("extract", str(self.afs_path), str(target))
        self.assertEqual(code, ExitCode.ERROR)
        self.assertIn("dentro del repo", err)
        self.assertFalse(target.exists())


class InventoryCommandTests(CliTestCase):
    def test_inventory_writes_both_reports(self) -> None:
        inner = build_afs([("a.xpr", b"XPR0" + bytes(28)), ("b.txt", b"plain text file\n" * 4)])
        outer = build_afs([("inner.afs", inner), ("raw", b"\x01\x00\x00\x00" * 8)], "before_data")
        (self.root / "outer.afs").write_bytes(outer)
        json_path, md_path = self.root / "r" / "inv.json", self.root / "r" / "inv.md"
        code, out, _ = self.run_cli(
            "inventory", str(self.root), "--json", str(json_path), "--md", str(md_path)
        )
        self.assertEqual(code, ExitCode.OK)
        self.assertTrue(json_path.exists() and md_path.exists())
        self.assertIn("## Histograma", md_path.read_text(encoding="utf-8"))
        self.assertIn("2 AFS, 5 entradas", out)

    def test_reports_are_written_durably(self) -> None:
        target = self.root / "r" / "inv.json"
        with mock.patch("ngb_tools._paths.os.fsync", wraps=os.fsync) as fsync:
            code, _, _ = self.run_cli("inventory", str(self.root), "--json", str(target))
        self.assertEqual(code, ExitCode.OK)
        self.assertTrue(fsync.called)

    def test_inventory_with_issues_exits_with_findings(self) -> None:
        self.write_misaligned()
        code, _, err = self.run_cli("inventory", str(self.root))
        self.assertEqual(code, ExitCode.FINDINGS)
        self.assertIn("1 AFS con incidencias", err)

    def test_report_inside_repo_outside_build_is_refused(self) -> None:
        target = REPO / "_afs_test_nope" / "inv.txt"
        self.addCleanup(shutil.rmtree, target.parent, ignore_errors=True)  # si la guarda fallara
        for flag in ("--json", "--md"):
            with self.subTest(flag=flag):
                code, _, err = self.run_cli("inventory", str(self.root), flag, str(target))
                self.assertEqual(code, ExitCode.ERROR)
                self.assertIn("dentro del repo", err)
                self.assertFalse(target.parent.exists())

    def test_report_in_another_repos_build_is_refused(self) -> None:
        other = self.root / "other"
        (other / ".git").mkdir(parents=True)
        target = other / "build" / "inv.json"
        code, _, _ = self.run_cli("inventory", str(self.root), "--json", str(target))
        self.assertEqual(code, ExitCode.ERROR)
        self.assertFalse(target.exists())

    def test_report_in_repo_build_dir_is_allowed(self) -> None:
        build = REPO / "build"
        build.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=build) as tmp:
            target = Path(tmp) / "reports" / "inv.md"
            code, _, _ = self.run_cli("inventory", str(self.root), "--md", str(target))
            self.assertEqual(code, ExitCode.OK)
            self.assertTrue(target.exists())

    def test_missing_root_is_a_usage_error(self) -> None:
        code, _, _ = self.run_cli("inventory", str(self.root / "nope"))
        self.assertEqual(code, ExitCode.ERROR)


class ModuleEntryPointTests(CliTestCase):
    def test_python_dash_m_runs_the_cli(self) -> None:
        env = {**os.environ, "PYTHONPATH": str(REPO / "src"), "PYTHONIOENCODING": "utf-8"}
        result = subprocess.run(
            [sys.executable, "-P", "-s", "-m", "ngb_tools.afs", "list", str(self.afs_path)],
            capture_output=True,
            text=True,
            encoding="utf-8",
            env=env,
            cwd=REPO,
            check=False,
        )
        self.assertEqual(result.returncode, ExitCode.OK, result.stderr)
        self.assertIn("3 entradas", result.stdout)


if __name__ == "__main__":
    unittest.main()
