"""Tests del catálogo de texturas y de la exportación a PNG con manifiesto."""

from __future__ import annotations

import hashlib
import json
import re
import tempfile
import unittest
from pathlib import Path
from typing import Any
from unittest import mock

from ngb_tools._paths import UnsafeOutputError
from ngb_tools.xpr.catalog import CatalogEntry, catalog
from ngb_tools.xpr.errors import XprFormatError
from ngb_tools.xpr.export import MANIFEST_NAME, default_output_dir, export_textures
from ngb_tools.xpr.formats import FORMATS
from ngb_tools.xpr.resources import FormatWord, SizeWord, Texture, parse_xpr
from ngb_tools.xpr.source import open_source
from teamninja.synthetic import build_chunk

from .pngread import read_png
from .synthetic import (
    build_xpr,
    d3dcolor,
    format_word,
    palette_header,
    solid_dxt1_block,
    texture_header,
)

DXT1, P8 = 0x0C, 0x0B
DXT1_DATA = solid_dxt1_block(0xF800) + solid_dxt1_block(0x001F)  # 8x4: rojo | azul
P8_DATA = bytes(16)  # 4x4, todo índice 0
PALETTE_DATA = d3dcolor(0, 255, 0) + bytes(1020)  # entrada 0 verde
CUBE_DATA = bytes(6 * 128)
HEADER_SIZE = 0x800


def sample_xpr() -> bytes:
    """DXT1 8x4, P8 4x4 con su paleta y un cubemap que no se puede exportar."""
    return build_xpr(
        [
            texture_header(0, format_word(DXT1, 3, 2)),
            texture_header(16, format_word(P8, 2, 2)),
            palette_header(32),
            texture_header(32 + 1024, format_word(DXT1, 4, 4, cubemap=True)),
        ],
        DXT1_DATA + P8_DATA + PALETTE_DATA + CUBE_DATA,
    )


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


class CatalogTests(unittest.TestCase):
    def test_entries_follow_texture_order_with_their_issues(self) -> None:
        entries = catalog(parse_xpr(sample_xpr()))
        self.assertEqual([e.ordinal for e in entries], [0, 1, 2])
        self.assertEqual([e.texture.index for e in entries], [0, 1, 3])
        self.assertIsNone(entries[0].issue)
        self.assertIn("cubemap", entries[2].issue or "")

    def test_filename_has_index_format_and_size(self) -> None:
        entries = catalog(parse_xpr(sample_xpr()))
        self.assertEqual(entries[0].filename, "0000_DXT1_8x4.png")
        self.assertIsNone(entries[2].filename)

    def test_skipped_entry_still_reports_its_format_and_size(self) -> None:
        cube = catalog(parse_xpr(sample_xpr()))[2]
        self.assertEqual((cube.format_name, cube.width, cube.height), ("DXT1", 16, 16))


class ExportTestCase(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.out = self.root / "out"

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def export(self, name: str, data: bytes, *, kmd: bool = False) -> dict[str, Any]:
        path = self.root / name
        path.write_bytes(data)
        export_textures(open_source(path, kmd=kmd), self.out)
        manifest: dict[str, Any] = json.loads((self.out / MANIFEST_NAME).read_text("utf-8"))
        return manifest


class ExportTests(ExportTestCase):
    def test_one_png_per_exportable_texture_and_a_manifest(self) -> None:
        self.export("sample.xpr", sample_xpr())
        names = sorted(p.name for p in self.out.iterdir())
        self.assertEqual(names, ["0000_DXT1_8x4.png", "0001_P8_4x4.png", MANIFEST_NAME])

    def test_png_holds_the_decoded_level_zero(self) -> None:
        self.export("sample.xpr", sample_xpr())
        png = read_png((self.out / "0000_DXT1_8x4.png").read_bytes())
        self.assertEqual((png.width, png.height), (8, 4))
        self.assertEqual(png.rgba[0:4], bytes([255, 0, 0, 255]))
        self.assertEqual(png.rgba[16:20], bytes([0, 0, 255, 255]))
        p8 = read_png((self.out / "0001_P8_4x4.png").read_bytes())
        self.assertEqual(set(zip(p8.rgba[0::4], p8.rgba[1::4], strict=True)), {(0, 255)})

    def test_manifest_records_offsets_and_source_hashes(self) -> None:
        manifest = self.export("sample.xpr", sample_xpr())
        textures = manifest["textures"]
        assert isinstance(textures, list)
        first, second = textures[0], textures[1]
        self.assertEqual(first["format"], "DXT1")
        self.assertEqual(first["format_code"], DXT1)
        self.assertEqual((first["width"], first["height"], first["mips"]), (8, 4, 1))
        self.assertEqual(first["data_offset"], 0)
        self.assertEqual(first["file_offset"], HEADER_SIZE)
        self.assertEqual(first["byte_length"], len(DXT1_DATA))
        self.assertEqual(first["sha256"], sha256(DXT1_DATA))
        self.assertEqual(second["palette"]["resource_index"], 2)
        self.assertEqual(second["palette"]["file_offset"], HEADER_SIZE + 32)
        self.assertEqual(second["palette"]["sha256"], sha256(PALETTE_DATA))
        self.assertEqual(second["palette"]["source"], "next")
        self.assertEqual((first["alpha"], second["alpha"]), ("straight", "straight"))
        self.assertEqual(first["warnings"], [])
        self.assertEqual(first["levels"][0]["present"], True)

    def test_missing_mips_export_level_zero_with_a_warning(self) -> None:
        header = texture_header(0, format_word(DXT1, 3, 3, levels=3))
        level0 = solid_dxt1_block(0xF800) * 4  # 8x8 = 4 bloques; faltan los niveles 1 y 2
        manifest = self.export("mips.xpr", build_xpr([header], level0))
        record = manifest["textures"][0]
        self.assertEqual(record["status"], "exported")
        self.assertEqual([level["present"] for level in record["levels"]], [True, False, False])
        self.assertEqual((record["byte_length"], record["sha256"]), (32, sha256(level0)))
        self.assertRegex(record["warnings"][0], "2 de 3 niveles de mip")

    def test_decoder_failure_skips_only_that_texture(self) -> None:
        failure = mock.patch(
            "ngb_tools.xpr.export.decode_texture", side_effect=XprFormatError("decodificador roto")
        )
        with failure:
            manifest = self.export("sample.xpr", sample_xpr())
        self.assertEqual(manifest["exported"], 0)
        self.assertIn("decodificador roto", manifest["textures"][0]["issue"])

    def test_unsupported_texture_is_listed_as_skipped(self) -> None:
        manifest = self.export("sample.xpr", sample_xpr())
        self.assertEqual((manifest["exported"], manifest["skipped"]), (2, 1))
        textures = manifest["textures"]
        assert isinstance(textures, list)
        cube = textures[2]
        self.assertEqual((cube["status"], cube["file"], cube["sha256"]), ("skipped", None, None))
        self.assertIn("cubemap", cube["issue"])

    def test_kmd_offsets_are_absolute_in_the_file(self) -> None:
        kmd = build_chunk(b"KMD", [b"OBJINFO\0" + bytes(24), sample_xpr()], table_pos=0x80)
        manifest = self.export("sample.kmd", kmd, kmd=True)
        textures = manifest["textures"]
        assert isinstance(textures, list)
        xpr_offset = manifest["xpr_offset"]
        assert isinstance(xpr_offset, int)
        self.assertEqual(manifest["kmd_slot"], 1)
        self.assertEqual(textures[1]["file_offset"], xpr_offset + HEADER_SIZE + 16)
        self.assertEqual(kmd[xpr_offset : xpr_offset + 4], b"XPR0")

    def test_textures_beyond_the_pixel_budget_are_skipped(self) -> None:
        # Varias cabeceras pueden apuntar a los mismos datos: el presupuesto acota el trabajo.
        path = self.root / "sample.xpr"
        path.write_bytes(sample_xpr())
        manifest = export_textures(open_source(path, kmd=False), self.out, pixel_budget=8 * 4)
        statuses = [t["status"] for t in manifest["textures"]]
        self.assertEqual(statuses, ["exported", "skipped", "skipped"])
        self.assertIn("presupuesto", manifest["textures"][1]["issue"] or "")
        self.assertFalse((self.out / "0001_P8_4x4.png").exists())

    def test_no_partial_files_are_left(self) -> None:
        self.export("sample.xpr", sample_xpr())
        self.assertEqual([p for p in self.out.iterdir() if p.suffix == ".part"], [])


class UnsafeExportTests(ExportTestCase):
    def test_output_inside_a_repo_is_refused_before_writing(self) -> None:
        # La guarda es la de `_paths` (probada en tests/test_paths.py); aquí solo se comprueba
        # que `export` la usa antes de crear nada.
        repo = self.root / "repo"
        (repo / ".git").mkdir(parents=True)
        path = self.root / "sample.xpr"
        path.write_bytes(sample_xpr())
        out = repo / "textures"
        with self.assertRaises(UnsafeOutputError):
            export_textures(open_source(path, kmd=False), out)
        self.assertFalse(out.exists())


class DefaultOutputDirTests(unittest.TestCase):
    def test_uses_ngb_data_and_the_source_stem(self) -> None:
        out = default_output_dir(Path("x/0000_ryu.kmd"), {"NGB_DATA": "D:/datos"})
        self.assertEqual(out, Path("D:/datos") / "extracted" / "textures" / "0000_ryu")

    def test_falls_back_to_home_ngb_data(self) -> None:
        out = default_output_dir(Path("font.xpr"), {})
        self.assertEqual(out, Path.home() / "ngb-data" / "extracted" / "textures" / "font")

    def test_dot_only_stems_cannot_climb_or_collapse(self) -> None:
        for name in ("..", "..kmd", ". "):
            with self.subTest(name=name):
                out = default_output_dir(Path(name), {"NGB_DATA": "D:/datos"})
                self.assertEqual(out.parent, Path("D:/datos") / "extracted" / "textures")
                self.assertNotIn(out.name, ("", ".", ".."))


class OutputNameTests(unittest.TestCase):
    """Los nombres de salida salen del índice y de la tabla de formatos, nunca del archivo."""

    SAFE_NAME = re.compile(r"^\d{4}_[A-Z0-9_]+_\d+x\d+\.png$")

    def test_every_decodable_format_gives_a_plain_file_name(self) -> None:
        for code, fmt in FORMATS.items():
            if not fmt.is_decodable:
                continue
            texture = Texture(0, 0, 0x00040001, 0, 0, FormatWord(code << 8 | 0x20), SizeWord(0))
            entry = CatalogEntry(9999, texture, None, None)
            with self.subTest(fmt=fmt.name):
                self.assertRegex(entry.filename or "", self.SAFE_NAME)

    def test_exported_files_land_directly_in_the_output_dir(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "a.xpr").write_bytes(sample_xpr())
            manifest = export_textures(open_source(root / "a.xpr", kmd=False), root / "out")
            files = [t["file"] for t in manifest["textures"] if t["file"] is not None]
            for name in files:
                self.assertEqual(Path(name).name, name)
            self.assertEqual(
                sorted(p.name for p in (root / "out").iterdir()),
                sorted([*files, MANIFEST_NAME]),
            )


if __name__ == "__main__":
    unittest.main()
