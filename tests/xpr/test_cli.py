"""CLI tests: subcommands, output and exit codes (0 ok, 1 findings, 2 usage/IO)."""

from __future__ import annotations

import contextlib
import io
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from ngb_tools.xpr.cli import ExitCode, main, run
from ngb_tools.xpr.export import MANIFEST_NAME
from teamninja.synthetic import build_chunk

from .synthetic import build_xpr, format_word, solid_dxt1_block, texture_header

REPO = Path(__file__).resolve().parents[2]
DXT1 = 0x0C
GOOD_XPR = build_xpr(
    [texture_header(0, format_word(DXT1, 3, 2))],
    solid_dxt1_block(0xF800) + solid_dxt1_block(0x001F),
)
CUBE_XPR = build_xpr(
    [
        texture_header(0, format_word(DXT1, 3, 2)),
        texture_header(16, format_word(DXT1, 2, 2, cubemap=True)),
    ],
    bytes(16 + 6 * 128),
)


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
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = main(list(argv))
        return code, out.getvalue(), err.getvalue()


class ListCommandTests(CliTestCase):
    def test_lists_each_texture(self) -> None:
        code, out, _ = self.run_cli("list", self.write("a.xpr", GOOD_XPR))
        self.assertEqual(code, ExitCode.OK)
        self.assertIn("DXT1", out)
        self.assertIn("8x4", out)

    def test_unsupported_texture_is_a_finding(self) -> None:
        code, out, _ = self.run_cli("list", self.write("a.xpr", CUBE_XPR))
        self.assertEqual(code, ExitCode.FINDINGS)
        self.assertRegex(out, r"! texture 1: cubemap")

    def test_kmd_flag_reads_the_xpr0_slot(self) -> None:
        kmd = build_chunk(b"KMD", [b"OBJINFO\0" + bytes(24), GOOD_XPR], table_pos=0x80)
        code, out, _ = self.run_cli("list", self.write("a.kmd", kmd), "--kmd")
        self.assertEqual(code, ExitCode.OK)
        self.assertIn("slot 1", out)

    def test_chunk_without_kmd_flag_is_an_input_error_with_a_hint(self) -> None:
        kmd = build_chunk(b"KMD", [GOOD_XPR])
        code, _, err = self.run_cli("list", self.write("a.kmd", kmd))
        self.assertEqual(code, ExitCode.ERROR)
        self.assertIn("--kmd", err)

    def test_broken_xpr0_is_a_finding(self) -> None:
        broken = bytearray(GOOD_XPR)
        broken[8:12] = (0xFFFFFFFF).to_bytes(4, "little")
        code, _, err = self.run_cli("list", self.write("a.xpr", bytes(broken)))
        self.assertEqual(code, ExitCode.FINDINGS)
        self.assertIn("header_size", err)

    def test_missing_file_is_an_io_error(self) -> None:
        code, _, err = self.run_cli("list", str(self.root / "missing.xpr"))
        self.assertEqual(code, ExitCode.ERROR)
        self.assertIn("error:", err)

    def test_missing_subcommand_is_a_usage_error(self) -> None:
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as raised:
            main([])
        self.assertEqual(raised.exception.code, ExitCode.ERROR)


class ExportCommandTests(CliTestCase):
    def test_export_writes_pngs_and_the_manifest(self) -> None:
        out_dir = self.root / "out"
        code, out, _ = self.run_cli("export", self.write("a.xpr", GOOD_XPR), "--out", str(out_dir))
        self.assertEqual(code, ExitCode.OK)
        self.assertIn("0000_DXT1_8x4.png", out)
        self.assertTrue((out_dir / "0000_DXT1_8x4.png").is_file())
        self.assertTrue((out_dir / MANIFEST_NAME).is_file())

    def test_skipped_textures_are_a_finding_but_the_rest_is_exported(self) -> None:
        out_dir = self.root / "out"
        code, out, _ = self.run_cli("export", self.write("a.xpr", CUBE_XPR), "--out", str(out_dir))
        self.assertEqual(code, ExitCode.FINDINGS)
        self.assertIn("cubemap", out)
        self.assertTrue((out_dir / "0000_DXT1_8x4.png").is_file())

    def test_default_output_goes_under_ngb_data(self) -> None:
        data_root = self.root / "ngb-data"
        with mock.patch.dict(os.environ, {"NGB_DATA": str(data_root)}):
            code, _, _ = self.run_cli("export", self.write("font.xpr", GOOD_XPR))
        self.assertEqual(code, ExitCode.OK)
        expected = data_root / "extracted" / "textures" / "font" / MANIFEST_NAME
        self.assertTrue(expected.is_file())

    def test_output_inside_the_repo_is_refused(self) -> None:
        out_dir = REPO / "build" / "xpr-cli-test"
        code, _, err = self.run_cli("export", self.write("a.xpr", GOOD_XPR), "--out", str(out_dir))
        self.assertEqual(code, ExitCode.ERROR)
        self.assertIn("repo", err)
        self.assertFalse(out_dir.exists())


class ConsoleEntryTests(CliTestCase):
    def test_run_reads_sys_argv_and_tolerates_an_ascii_console(self) -> None:
        # The file name has a non-ASCII character (it is echoed in the header): run() makes the
        # console substitute it instead of aborting.
        console = io.TextIOWrapper(io.BytesIO(), encoding="ascii")
        argv = ["ngb-xpr", "list", self.write("téxture.xpr", GOOD_XPR)]
        with mock.patch("sys.argv", argv), mock.patch("sys.stdout", console):
            code = run()
        self.assertEqual(code, ExitCode.OK)
        self.assertEqual(console.errors, "replace")

    def test_main_leaves_the_console_untouched(self) -> None:
        console = io.TextIOWrapper(io.BytesIO(), encoding="utf-8")
        with mock.patch("sys.stdout", console):
            main(["list", self.write("a.xpr", GOOD_XPR)])
        self.assertEqual(console.errors, "strict")


if __name__ == "__main__":
    unittest.main()
