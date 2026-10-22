"""CLI `python -m ngb_tools.xbe`: outputs and exit codes (0 ok, 1 validation, 2 usage/IO)."""

from __future__ import annotations

import contextlib
import io
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from ngb_tools.xbe.cli import ExitCode, main, run

from .synthetic import (
    SECTION_RAW,
    SECTION_VA,
    ExtraSection,
    build_synthetic_xbe,
    corrupt_text_section,
)


class CliTestCase(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = Path(self.enterContext(tempfile.TemporaryDirectory()))

    def write_xbe(self, data: bytes, name: str = "synthetic.xbe") -> Path:
        path = self.tmp / name
        path.write_bytes(data)
        return path

    def run_cli(self, *argv: str) -> tuple[int, str, str]:
        stdout, stderr = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            code = main(list(argv))
        return code, stdout.getvalue(), stderr.getvalue()


class SuccessTests(CliTestCase):
    def test_cli_writes_json_and_markdown(self) -> None:
        xbe = self.write_xbe(build_synthetic_xbe("debug"))
        out = self.tmp / "out"
        code, _, _ = self.run_cli(
            str(xbe), "--json", str(out / "r.json"), "--md", str(out / "r.md"), "--strings"
        )
        self.assertEqual(code, ExitCode.OK)
        report = json.loads((out / "r.json").read_text(encoding="utf-8"))
        self.assertEqual(report["image_header"]["base_address"], "0x00010000")
        self.assertEqual(report["xor"]["build_kind"], "debug")
        self.assertIn("strings", report)
        markdown = (out / "r.md").read_text(encoding="utf-8")
        self.assertIn("**TC-013**", markdown)
        self.assertIn("`DbgPrint`", markdown)

    def test_markdown_goes_to_stdout_without_outputs(self) -> None:
        code, stdout, _ = self.run_cli(str(self.write_xbe(build_synthetic_xbe())))
        self.assertEqual(code, ExitCode.OK)
        self.assertTrue(stdout.startswith("# XBE: synthetic.xbe\n"))

    def test_custom_exports_table_names_ordinals(self) -> None:
        exports = self.tmp / "exports.tsv"
        exports.write_text("8\tMiDbgPrint\tfunc\tcdecl\t\n", encoding="utf-8")
        xbe = self.write_xbe(build_synthetic_xbe())
        code, _, _ = self.run_cli(
            str(xbe), "--exports", str(exports), "--json", str(self.tmp / "r.json")
        )
        self.assertEqual(code, ExitCode.OK)
        imports = json.loads((self.tmp / "r.json").read_text(encoding="utf-8"))["kernel_imports"]
        self.assertEqual([imp["name"] for imp in imports][1], "MiDbgPrint")
        self.assertIn("unknown", imports[0]["name"])


class ValidationFailureTests(CliTestCase):
    def test_cli_reports_errors(self) -> None:
        bad = self.write_xbe(b"not an xbe" * 100, "bad.xbe")
        code, _, stderr = self.run_cli(str(bad), "--json", str(self.tmp / "x.json"))
        self.assertEqual(code, ExitCode.VALIDATION)
        self.assertIn("magic", stderr)
        self.assertFalse((self.tmp / "x.json").exists())

    def test_digest_mismatch_fails_but_still_writes_reports(self) -> None:
        xbe = self.write_xbe(corrupt_text_section(build_synthetic_xbe()))
        code, _, stderr = self.run_cli(str(xbe), "--json", str(self.tmp / "r.json"))
        self.assertEqual(code, ExitCode.VALIDATION)
        self.assertIn(".text", stderr)
        report = json.loads((self.tmp / "r.json").read_text(encoding="utf-8"))
        self.assertIs(report["sections"][0]["digest_ok"], False)

    def test_overlapping_sections_fail_validation(self) -> None:
        extra = ExtraSection(b"OVER", SECTION_VA + 0x100, 0x40, SECTION_RAW, 0x40, flags=0)
        xbe = self.write_xbe(build_synthetic_xbe(extra_sections=(extra,)))
        code, _, stderr = self.run_cli(str(xbe), "--json", str(self.tmp / "r.json"))
        self.assertEqual(code, ExitCode.VALIDATION)
        self.assertIn("overlap", stderr)
        self.assertTrue((self.tmp / "r.json").exists())


class ConsoleEntryTests(CliTestCase):
    def test_run_reads_sys_argv_and_forces_utf8_console(self) -> None:
        xbe = self.write_xbe(build_synthetic_xbe())
        console = io.TextIOWrapper(io.BytesIO(), encoding="cp1252")
        argv = ["ngb-xbe", str(xbe), "--md", str(self.tmp / "r.md")]
        with mock.patch("sys.argv", argv), mock.patch("sys.stdout", console):
            code = run()
        self.assertEqual(code, ExitCode.OK)
        self.assertEqual(console.encoding, "utf-8")
        self.assertTrue((self.tmp / "r.md").exists())


class UsageErrorTests(CliTestCase):
    def test_missing_input_is_a_usage_error(self) -> None:
        code, _, stderr = self.run_cli(str(self.tmp / "nope.xbe"))
        self.assertEqual(code, ExitCode.USAGE)
        self.assertIn("error", stderr)

    def test_non_positive_min_len_is_rejected_by_argparse(self) -> None:
        xbe = self.write_xbe(build_synthetic_xbe())
        with self.assertRaises(SystemExit) as caught:
            self.run_cli(str(xbe), "--strings", "--min-len", "0")
        self.assertEqual(caught.exception.code, ExitCode.USAGE)

    def test_malformed_exports_table_is_a_usage_error(self) -> None:
        exports = self.tmp / "exports.tsv"
        exports.write_text("x\tFoo\n", encoding="utf-8")
        xbe = self.write_xbe(build_synthetic_xbe())
        code, _, stderr = self.run_cli(str(xbe), "--exports", str(exports))
        self.assertEqual(code, ExitCode.USAGE)
        self.assertIn("line 1", stderr)

    def test_unwritable_output_is_a_usage_error(self) -> None:
        blocker = self.tmp / "blocker"
        blocker.write_text("not a directory", encoding="utf-8")
        xbe = self.write_xbe(build_synthetic_xbe())
        code, _, _ = self.run_cli(str(xbe), "--md", str(blocker / "r.md"))
        self.assertEqual(code, ExitCode.USAGE)

    def test_output_never_overwrites_the_input(self) -> None:
        data = build_synthetic_xbe()
        xbe = self.write_xbe(data)
        code, _, stderr = self.run_cli(str(xbe), "--md", str(xbe))
        self.assertEqual(code, ExitCode.USAGE)
        self.assertIn("input file", stderr)
        self.assertEqual(xbe.read_bytes(), data)


if __name__ == "__main__":
    unittest.main()
