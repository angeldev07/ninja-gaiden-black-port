"""Kernel C stub generator: TSV -> `port/src/kernel/stubs.c`."""

from __future__ import annotations

import io
import re
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

from ngb_tools.port.kernel_stubs import (
    StubError,
    arg_count,
    main,
    render_stub,
    render_stubs_c,
    render_table_row,
)
from ngb_tools.xbe.kernel import (
    CallConv,
    ExportKind,
    KernelExport,
    bundled_kernel_exports,
    parse_kernel_exports,
)

REPO_ROOT = Path(__file__).resolve().parents[2]
COMMITTED_STUBS = REPO_ROOT / "port" / "src" / "kernel" / "stubs.c"
TSV = (
    "1\tAvGetSavedDataAddress\tfunc\tstdcall\t0\n"
    "8\tDbgPrint\tfunc\tcdecl\t\n"
    "156\tKeTickCount\tdata\t-\t\n"
    "160\tKfRaiseIrql\tfunc\tfastcall\t4\n"
    "165\tMmAllocateContiguousMemory\tfunc\tstdcall\t4\n"
    "255\tPsCreateSystemThreadEx\tfunc\tstdcall\t40\n"
)
STUB_RE = re.compile(r"static uint64_t (__\w+) kstub_(\d{3})\(([^)]*)\)")


def export(ordinal: int, name: str, conv: CallConv, arg_bytes: int | None) -> KernelExport:
    return KernelExport(ordinal, name, ExportKind.FUNC, conv, arg_bytes)


class RenderStubTest(unittest.TestCase):
    def test_stdcall_copies_each_argument(self) -> None:
        text = render_stub(export(165, "MmAllocateContiguousMemory", CallConv.STDCALL, 4))
        self.assertEqual(
            text,
            "/* 165 MmAllocateContiguousMemory: stdcall, 1 args */\n"
            "static uint64_t __stdcall kstub_165(uint32_t a0)\n"
            "{\n"
            "    uint32_t args[1];\n"
            "    args[0] = a0;\n"
            "    return ngb_kcall(165u, args, (uint32_t)(uintptr_t)_ReturnAddress());\n"
            "}\n",
        )

    def test_stdcall_without_arguments_passes_null(self) -> None:
        text = render_stub(export(1, "AvGetSavedDataAddress", CallConv.STDCALL, 0))
        self.assertIn("static uint64_t __stdcall kstub_001(void)", text)
        self.assertIn("ngb_kcall(1u, NULL,", text)

    def test_fastcall_keyword_and_count(self) -> None:
        text = render_stub(export(160, "KfRaiseIrql", CallConv.FASTCALL, 4))
        self.assertIn("static uint64_t __fastcall kstub_160(uint32_t a0)", text)

    def test_ten_stdcall_arguments(self) -> None:
        text = render_stub(export(255, "PsCreateSystemThreadEx", CallConv.STDCALL, 40))
        self.assertIn("uint32_t a9)", text)
        self.assertIn("args[9] = a9;", text)
        self.assertNotIn("a10", text)

    def test_cdecl_is_variadic_and_passes_stack_pointer(self) -> None:
        text = render_stub(export(8, "DbgPrint", CallConv.CDECL, None))
        self.assertIn("static uint64_t __cdecl kstub_008(uint32_t a0, ...)", text)
        self.assertIn("ngb_kcall(8u, &a0,", text)

    def test_odd_arg_bytes_is_rejected(self) -> None:
        with self.assertRaises(StubError):
            arg_count(export(2, "Bad", CallConv.STDCALL, 6))

    def test_data_export_has_no_stub(self) -> None:
        data = KernelExport(156, "KeTickCount", ExportKind.DATA, CallConv.NONE, None)
        with self.assertRaises(StubError):
            render_stub(data)
        self.assertEqual(
            render_table_row(data),
            '    {156, NGB_KIND_DATA, NGB_CONV_NONE, 0, "KeTickCount", NULL},',
        )


class RenderFileTest(unittest.TestCase):
    def test_small_table(self) -> None:
        text = render_stubs_c(parse_kernel_exports(TSV))
        stubs = {int(o): (conv, params) for conv, o, params in STUB_RE.findall(text)}
        self.assertEqual(sorted(stubs), [1, 8, 160, 165, 255])
        self.assertEqual(stubs[160][0], "__fastcall")
        self.assertIn(
            '{165, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "MmAllocateContiguousMemory", '
            "(const void *)kstub_165},",
            text,
        )
        self.assertIn('{8, NGB_KIND_FUNC, NGB_CONV_CDECL, 4, "DbgPrint"', text)
        self.assertIn("const uint32_t ngb_kexport_max_ordinal = 255u;", text)
        self.assertTrue(text.endswith("\n") and "\r" not in text)

    def test_bundled_table_matches_arg_bytes(self) -> None:
        exports = bundled_kernel_exports()
        text = render_stubs_c(exports)
        stubs = {int(o): (conv, params) for conv, o, params in STUB_RE.findall(text)}
        funcs = {o: e for o, e in exports.items() if e.kind is ExportKind.FUNC}
        self.assertEqual(set(stubs), set(funcs))
        for ordinal, exp in funcs.items():
            conv, params = stubs[ordinal]
            self.assertEqual(conv, f"__{exp.callconv}", exp.name)
            if exp.callconv is CallConv.CDECL:
                self.assertEqual(params, "uint32_t a0, ...")
            elif not exp.arg_bytes:
                self.assertEqual(params, "void")
            else:
                self.assertEqual(params.count("uint32_t"), exp.arg_bytes // 4, exp.name)
        rows = re.findall(r"^    \{(\d+), NGB_KIND_(FUNC|DATA)", text, re.MULTILINE)
        self.assertEqual(len(rows), len(exports))

    def test_committed_file_is_up_to_date(self) -> None:
        with redirect_stdout(io.StringIO()):
            status = main(["--check", "--out", str(COMMITTED_STUBS)])
        self.assertEqual(status, 0, "regenerate with: python tools/gen_kernel_stubs.py")

    def test_main_writes_and_checks(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tsv = Path(tmp) / "exports.tsv"
            out = Path(tmp) / "sub" / "stubs.c"
            tsv.write_text(TSV, encoding="utf-8")
            with redirect_stdout(io.StringIO()):
                self.assertEqual(main(["--exports", str(tsv), "--out", str(out)]), 0)
                self.assertEqual(main(["--exports", str(tsv), "--out", str(out), "--check"]), 0)
                out.write_text("stale", encoding="utf-8")
                self.assertEqual(main(["--exports", str(tsv), "--out", str(out), "--check"]), 1)


if __name__ == "__main__":
    unittest.main()
