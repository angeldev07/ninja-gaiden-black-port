"""Tabla de exports de xboxkrnl y resolución del thunk de imports del kernel."""

from __future__ import annotations

import hashlib
import struct
import tempfile
import unittest
from importlib.resources import files
from pathlib import Path

from ngb_tools.xbe.errors import KernelExportsError, ThunkTableError
from ngb_tools.xbe.image import XbeImage
from ngb_tools.xbe.kernel import (
    CallConv,
    ExportKind,
    bundled_kernel_exports,
    load_kernel_exports,
    parse_kernel_exports,
    parse_thunk_table,
    read_kernel_imports,
)

from .synthetic import (
    SECTION_RAW_SIZE,
    SECTION_VA,
    THUNK,
    THUNK_OFF,
    THUNK_ORDINALS,
    THUNK_VA,
    XOR_KEYS,
    build_synthetic_xbe,
    with_u32,
)

EXPORTS = bundled_kernel_exports()
# SHA-256 del TSV incluido (texto con fin de línea LF). Si cambia, que sea a propósito:
# la tabla deriva de nxdk 9e9c07f (lib/xboxkrnl/xboxkrnl.exe.def, CC0-1.0).
BUNDLED_TSV_SHA256 = "47a03051a9665546da6cb7888db9eff7351bf32c4f9d453f16e40c7780c2ab05"


def thunk_bytes(*entries: int) -> bytes:
    return struct.pack(f"<{len(entries)}I", *entries)


class ExportsTableTests(unittest.TestCase):
    def test_exports_table(self) -> None:
        self.assertEqual(EXPORTS[1].name, "AvGetSavedDataAddress")
        self.assertEqual(EXPORTS[8].callconv, CallConv.CDECL)
        self.assertGreater(len(EXPORTS), 360)

    def test_bundled_table_keeps_cc0_attribution(self) -> None:
        # La tabla deriva de nxdk (CC0-1.0): la cabecera de procedencia debe viajar con ella.
        text = files("ngb_tools").joinpath("data", "xboxkrnl_exports.tsv").read_text("utf-8")
        self.assertIn("CC0-1.0", text)
        self.assertIn("github.com/XboxDev/nxdk", text)

    def test_bundled_table_content_is_pinned(self) -> None:
        text = files("ngb_tools").joinpath("data", "xboxkrnl_exports.tsv").read_text("utf-8")
        self.assertEqual(hashlib.sha256(text.encode("utf-8")).hexdigest(), BUNDLED_TSV_SHA256)

    def test_bundled_table_is_cached_and_read_only(self) -> None:
        self.assertIs(bundled_kernel_exports(), EXPORTS)
        with self.assertRaises(TypeError):
            EXPORTS[1] = EXPORTS[2]

    def test_comments_blank_lines_and_short_rows(self) -> None:
        exports = parse_kernel_exports("# cabecera\n\n1\tFoo\n2\tBar\tdata\t-\t\n")
        self.assertEqual(sorted(exports), [1, 2])
        self.assertIsNone(exports[1].kind)
        self.assertIsNone(exports[1].arg_bytes)
        self.assertEqual((exports[2].kind, exports[2].callconv), (ExportKind.DATA, CallConv.NONE))

    def test_malformed_ordinal_names_the_line(self) -> None:
        with self.assertRaisesRegex(KernelExportsError, r"línea 2"):
            parse_kernel_exports("1\tFoo\nx\tBar\n")

    def test_duplicate_ordinal_is_rejected(self) -> None:
        with self.assertRaisesRegex(KernelExportsError, "duplicado"):
            parse_kernel_exports("1\tFoo\n1\tBar\n")

    def test_ordinal_must_fit_in_31_bits(self) -> None:
        for line in ("2147483648\tFoo\n", "-1\tFoo\n"):
            with self.subTest(line=line), self.assertRaisesRegex(KernelExportsError, "ordinal"):
                parse_kernel_exports(line)
        self.assertIn(0x7FFFFFFF, parse_kernel_exports("2147483647\tFoo\n"))

    def test_negative_argument_bytes_are_rejected(self) -> None:
        with self.assertRaisesRegex(KernelExportsError, "arg_bytes"):
            parse_kernel_exports("1\tFoo\tfunc\tstdcall\t-4\n")

    def test_unknown_kind_is_rejected(self) -> None:
        with self.assertRaises(KernelExportsError):
            parse_kernel_exports("1\tFoo\tmacro\tstdcall\t4\n")

    def test_custom_table_from_path(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "exports.tsv"
            path.write_text("8\tMiDbgPrint\tfunc\tcdecl\t\n", encoding="utf-8")
            exports = load_kernel_exports(path)
        self.assertEqual(exports[8].name, "MiDbgPrint")


class ThunkTableTests(unittest.TestCase):
    def test_kernel_imports(self) -> None:
        table = read_kernel_imports(XbeImage.from_bytes(build_synthetic_xbe()), EXPORTS)
        assert table is not None
        imports = table.imports
        self.assertEqual([imp.ordinal for imp in imports], list(THUNK_ORDINALS))
        self.assertEqual(
            [imp.name for imp in imports], ["AvGetSavedDataAddress", "DbgPrint", "KeTickCount"]
        )
        self.assertEqual(imports[1].slot_va, THUNK_VA + 4)
        self.assertEqual(imports[2].kind, ExportKind.DATA)
        self.assertEqual(
            (table.va, table.file_offset, table.section), (THUNK_VA, THUNK_OFF, ".text")
        )

    def test_unknown_ordinal(self) -> None:
        (imp,) = parse_thunk_table(thunk_bytes(0x80000000 | 370, 0), 0x1000, EXPORTS)
        self.assertIn("desconocido", imp.name)
        self.assertIsNone(imp.kind)

    def test_entry_without_ordinal_flag(self) -> None:
        (imp,) = parse_thunk_table(thunk_bytes(0x00012345, 0), 0x1000, EXPORTS)
        self.assertIsNone(imp.ordinal)
        self.assertEqual(imp.raw_entry, 0x00012345)
        self.assertIn("no es import por ordinal", imp.name)

    def test_table_without_terminator_raises(self) -> None:
        with self.assertRaises(ThunkTableError):
            parse_thunk_table(thunk_bytes(*[0x80000001] * 600), 0x1000, EXPORTS)

    def test_table_running_off_its_region_raises(self) -> None:
        with self.assertRaisesRegex(ThunkTableError, "final"):
            parse_thunk_table(thunk_bytes(0x80000001, 0x80000002), 0x1000, EXPORTS)

    def test_thunk_reads_stop_at_end_of_backed_region(self) -> None:
        # Slots sin terminador hasta el final de .text; el archivo sigue con otra sección.
        tail = thunk_bytes(0x80000001, 0x80000008)
        thunk_va = SECTION_VA + SECTION_RAW_SIZE - len(tail)
        data = build_synthetic_xbe(text_tail=tail, inserted_file=True)
        data = with_u32(data, 0x158, thunk_va ^ XOR_KEYS["retail"][THUNK])
        with self.assertRaises(ThunkTableError):
            read_kernel_imports(XbeImage.from_bytes(data), EXPORTS)

    def test_no_table_without_known_xor_keys(self) -> None:
        data = with_u32(build_synthetic_xbe(), 0x158, 0x12345678)
        self.assertIsNone(read_kernel_imports(XbeImage.from_bytes(data), EXPORTS))


if __name__ == "__main__":
    unittest.main()
