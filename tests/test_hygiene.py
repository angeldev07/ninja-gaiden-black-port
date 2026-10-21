"""Tests unitarios de la guardia de higiene: reglas puras sobre contenido en memoria."""

from __future__ import annotations

import unittest
from collections.abc import Callable

from ngb_tools.hygiene import (
    DEFAULT_POLICY,
    GitError,
    Policy,
    Rule,
    check_blob,
    match_signature,
    parse_allowlist,
    parse_batch_check,
    parse_index_entries,
)

XBE = b"XBEH" + bytes(60)
OID = "0123456789abcdef0123456789abcdef01234567"


def memory_reader(content: bytes) -> Callable[[int], bytes]:
    """Lector de prefijos sobre bytes en memoria (lo que haría git con el blob)."""
    return lambda length: content[:length]


def rules_of(path: str, content: bytes, policy: Policy = DEFAULT_POLICY) -> set[Rule]:
    violations = check_blob(path, len(content), memory_reader(content), policy)
    return {v.rule for v in violations}


def team_ninja_chunk(tag: bytes, field_18: int = 0x40, field_1c: int = 0x20) -> bytes:
    """Cabecera de chunk de Team Ninja: tag de 12 bytes y dos u32 LE en 0x18 y 0x1C."""
    return (
        tag.ljust(12, b"\0")
        + bytes(12)
        + field_18.to_bytes(4, "little")
        + field_1c.to_bytes(4, "little")
        + bytes(32)
    )


def adx_header(copyright_offset: int) -> bytes:
    return b"\x80\x00" + copyright_offset.to_bytes(2, "big") + bytes(28)


class ExtensionRuleTest(unittest.TestCase):
    def test_game_extension_is_rejected_case_insensitively(self) -> None:
        self.assertEqual(rules_of("assets/CMN.AFS", b"x"), {Rule.EXTENSION})

    def test_small_disc_root_files_are_rejected(self) -> None:
        for name in ("dsstdfx.bin", "NinjaLaunchXBEName.def", "verchk.ins"):
            with self.subTest(name=name):
                self.assertEqual(rules_of(name, b"\x10\xbf\x17\x43"), {Rule.EXTENSION})

    def test_dotfile_named_like_an_extension_is_rejected(self) -> None:
        for path in (".iso", "dir/.ISO"):
            with self.subTest(path=path):
                self.assertEqual(rules_of(path, b"x"), {Rule.EXTENSION})

    def test_compressed_archives_are_rejected_by_extension(self) -> None:
        for path in ("a.zip", "b.7z", "c.rar", "d.tar", "e.tar.gz", "f.TGZ", "g.bz2", "h.xz"):
            with self.subTest(path=path):
                self.assertEqual(rules_of(path, b"x"), {Rule.EXTENSION})

    def test_names_that_only_contain_an_extension_pass(self) -> None:
        for path in ("src/iso.py", "notes.isolated", "undef", "x.bin.txt", "docs/afs.md"):
            with self.subTest(path=path):
                self.assertEqual(rules_of(path, b"texto\n"), set())

    def test_unicode_lookalike_extensions_are_rejected(self) -> None:
        # 'İ'.lower() es 'i' + U+0307: con str.lower, CMN.İSO no acabaría en '.iso'.
        for path in ("CMN.İSO", "x.ＩＳＯ", "a.Kmd"):
            with self.subTest(path=ascii(path)):
                self.assertEqual(rules_of(path, b"x"), {Rule.EXTENSION})

    def test_fonts_are_not_blocked_by_extension_only_by_size(self) -> None:
        self.assertEqual(rules_of("fonts/libre.ttf", b"\x00\x01\x00\x00"), set())
        too_big = Policy(max_bytes=10)
        self.assertEqual(rules_of("fonts/juego.ttf", b"\x00\x01" * 6, too_big), {Rule.SIZE})

    def test_empty_file_passes(self) -> None:
        self.assertEqual(rules_of("empty.txt", b""), set())


class PathRuleTest(unittest.TestCase):
    def test_paths_under_ignored_top_level_dirs_are_rejected(self) -> None:
        paths = (
            "game/notas.txt",
            "GAME/a.txt",
            "Extracted/a/b.json",
            "build/out.txt",
            "ＧＡＭＥ/x.txt",
        )
        for path in paths:
            with self.subTest(path=ascii(path)):
                self.assertEqual(rules_of(path, b"texto\n"), {Rule.PATH})

    def test_similar_paths_pass(self) -> None:
        for path in ("docs/game/notas.md", "gamebook/a.txt", "src/build.py", "game", "build"):
            with self.subTest(path=path):
                self.assertEqual(rules_of(path, b"texto\n"), set())

    def test_allowlist_does_not_skip_the_path_rule(self) -> None:
        policy = Policy(allowed=frozenset({"game/x.txt"}))
        self.assertEqual(rules_of("game/x.txt", b"x", policy), {Rule.PATH})


class SignatureRuleTest(unittest.TestCase):
    def test_renamed_xbe_is_caught_by_magic(self) -> None:
        self.assertEqual(rules_of("notes.txt", XBE), {Rule.SIGNATURE})

    def test_each_verified_game_magic_matches(self) -> None:
        magics = (
            b"AFS\x00",
            b"XPR0",
            b"KMD\x00",
            b"WBND",
            b"SDBK",
            b"AIXF",
            b"\x00\x00\x01\xba",
        )
        for magic in magics:
            with self.subTest(magic=magic):
                self.assertIsNotNone(match_signature(magic + bytes(28)))

    def test_each_archive_magic_matches(self) -> None:
        magics = (
            b"PK\x03\x04",
            b"7z\xbc\xaf\x27\x1c",
            b"Rar!\x1a\x07\x00",
            b"\x1f\x8b\x08",
            b"\xfd7zXZ\x00",
            b"\x28\xb5\x2f\xfd",
            b"BZh9\x31\x41\x59\x26\x53\x59",
        )
        for magic in magics:
            with self.subTest(magic=magic):
                self.assertIsNotNone(match_signature(magic + bytes(28)))

    def test_text_starting_like_bzip2_is_not_an_archive(self) -> None:
        for text in (b"BZh is not a bzip2 stream\n", b"BZh9 is not one either\n"):
            with self.subTest(text=text):
                self.assertIsNone(match_signature(text))

    def test_adx_copyright_offset_bounds(self) -> None:
        cases = {0x10: True, 0x200: True, 0x0F: False, 0x201: False}
        for offset, is_adx in cases.items():
            with self.subTest(offset=hex(offset)):
                self.assertEqual(match_signature(adx_header(offset)) is not None, is_adx)

    def test_adx_shorter_than_its_header_is_not_adx(self) -> None:
        # Sin la comprobación de longitud, el byte 0x20 se leería como offset válido.
        self.assertIsNone(match_signature(b"\x80\x00\x20"))

    def test_team_ninja_chunk_header_matches(self) -> None:
        for tag in (b"itm_dat2", b"chr_dat", b"KMD", b"stry_dat_abc"):
            for field_18 in (0x20, 0x40, 0x80):
                with self.subTest(tag=tag, field_18=field_18):
                    self.assertIsNotNone(match_signature(team_ninja_chunk(tag, field_18)))

    def test_team_ninja_lookalikes_do_not_match(self) -> None:
        cases = {
            "tag de 2 chars": team_ninja_chunk(b"ab"),
            "tag no imprimible": team_ninja_chunk(b"itm\x01dat"),
            "basura tras el NUL": team_ninja_chunk(b"itm\x00dat"),
            "0x1C distinto de 0x20": team_ninja_chunk(b"itm_dat2", field_1c=0x40),
            "0x18 fuera del conjunto": team_ninja_chunk(b"itm_dat2", field_18=0x10),
            "cabecera truncada": team_ninja_chunk(b"itm_dat2")[:0x1F],
        }
        for case, head in cases.items():
            with self.subTest(case=case):
                self.assertIsNone(match_signature(head))

    def test_xdvdfs_magic_at_sector_32_is_caught_at_the_exact_boundary(self) -> None:
        image = bytes(0x10000) + b"MICROSOFT*XBOX*MEDIA"
        self.assertEqual(
            rules_of("disc.img", image, Policy(max_bytes=len(image))), {Rule.SIGNATURE}
        )

    def test_truncated_xdvdfs_magic_is_not_an_image(self) -> None:
        image = bytes(0x10000) + b"MICROSOFT*XBOX*MEDI"
        self.assertEqual(rules_of("disc.img", image, Policy(max_bytes=len(image))), set())


class ReadBoundsTest(unittest.TestCase):
    """El blob se lee por un tubo: nunca entero, y nunca más allá de su tamaño."""

    @staticmethod
    def recording_reader(requested: list[int]) -> Callable[[int], bytes]:
        def read(length: int) -> bytes:
            requested.append(length)
            return bytes(length)

        return read

    def test_huge_blob_is_read_only_up_to_the_xdvdfs_magic(self) -> None:
        requested: list[int] = []
        check_blob("disc.img", 8 * 1024**3, self.recording_reader(requested), DEFAULT_POLICY)
        self.assertTrue(requested)
        self.assertLessEqual(max(requested), 0x10000 + 20)

    def test_tiny_blob_is_never_read_past_its_size(self) -> None:
        requested: list[int] = []
        check_blob("a.txt", 3, self.recording_reader(requested), DEFAULT_POLICY)
        self.assertLessEqual(max(requested, default=0), 3)


class SizeRuleTest(unittest.TestCase):
    def test_file_over_limit_is_rejected(self) -> None:
        self.assertEqual(rules_of("big.json", b"{" * 11, Policy(max_bytes=10)), {Rule.SIZE})

    def test_file_at_limit_passes(self) -> None:
        self.assertEqual(rules_of("ok.json", b"{" * 10, Policy(max_bytes=10)), set())


class CombinedRulesTest(unittest.TestCase):
    def test_all_violations_are_reported_together(self) -> None:
        rules = rules_of("x.afs", b"AFS\x00" + bytes(20), Policy(max_bytes=8))
        self.assertEqual(rules, {Rule.EXTENSION, Rule.SIGNATURE, Rule.SIZE})


class AllowlistTest(unittest.TestCase):
    ALLOWED = frozenset({"tools/fixture.bin"})

    def test_parse_skips_comments_and_blank_lines(self) -> None:
        text = "# fixtures legítimos\n\n  tools/fixture.bin  \r\nlaunch.def\n"
        self.assertEqual(parse_allowlist(text), {"tools/fixture.bin", "launch.def"})

    def test_allowed_path_skips_the_extension_rule(self) -> None:
        policy = Policy(allowed=self.ALLOWED)
        self.assertEqual(rules_of("tools/fixture.bin", b"\x01\x02", policy), set())

    def test_allowed_path_still_fails_on_signature(self) -> None:
        policy = Policy(allowed=self.ALLOWED)
        self.assertEqual(rules_of("tools/fixture.bin", XBE, policy), {Rule.SIGNATURE})

    def test_allowed_path_still_fails_on_size(self) -> None:
        policy = Policy(max_bytes=1, allowed=self.ALLOWED)
        self.assertEqual(rules_of("tools/fixture.bin", b"\x01\x02", policy), {Rule.SIZE})

    def test_allowlist_matches_exact_paths_only(self) -> None:
        policy = Policy(allowed=self.ALLOWED)
        for path in ("tools/FIXTURE.bin", "other/fixture.bin", "fixture.bin"):
            with self.subTest(path=path):
                self.assertEqual(rules_of(path, b"\x01", policy), {Rule.EXTENSION})


class GitOutputParsingTest(unittest.TestCase):
    def test_index_paths_keep_tabs_and_survive_invalid_utf8(self) -> None:
        output = (
            f"100644 {OID} 0\tdir/a\tb.txt\0".encode()
            + f"100755 {OID} 0\t".encode()
            + b"bad\xffname.afs\0"
        )
        entries = parse_index_entries(output)
        self.assertEqual(entries[0].path, "dir/a\tb.txt")
        self.assertEqual([e.mode for e in entries], ["100644", "100755"])
        self.assertEqual(rules_of(entries[1].path, b"x"), {Rule.EXTENSION})

    def test_batch_check_reports_sizes(self) -> None:
        self.assertEqual(parse_batch_check(f"{OID} blob 12\n".encode()), {OID: 12})

    def test_missing_object_is_a_git_error(self) -> None:
        with self.assertRaises(GitError):
            parse_batch_check(f"{OID} missing\n".encode())


if __name__ == "__main__":
    unittest.main()
