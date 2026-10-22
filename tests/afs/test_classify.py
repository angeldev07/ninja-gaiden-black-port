"""Tests of header classification, Team Ninja tags and entropy."""

from __future__ import annotations

import unittest
import zlib

from ngb_tools.afs.classify import (
    EntropyBand,
    classify,
    describe_kind,
    entropy_band,
    head_ascii,
    shannon_entropy,
    team_ninja_tag,
)

from .synthetic import make_adx


class ClassifyTests(unittest.TestCase):
    def test_known_magics(self) -> None:
        cases = {
            b"AFS\x00" + bytes(12): "AFS",
            b"XPR0" + bytes(12): "XPR0",
            b"RIFF\x00\x00\x00\x00WAVEfmt ": "RIFF/WAVE",
            b"DDS " + bytes(12): "DDS",
            b"WBND" + bytes(12): "XWB",
            b"\x1f\x8b\x08\x00" + bytes(12): "gzip",
            zlib.compress(b"hello" * 100): "zlib",
            make_adx(): "ADX",
            b"Hello world, this is plain text.\r\n": "text",
            b"chr_dat\x00\x00\x00\x00\x00" + bytes(8): "tag:chr_dat",
            b"KMD\x00" + bytes(12): "tag:KMD",
            b"\x01\x00\x00\x00\x08\x00\x00\x80": "hex:01000000",
            b"": "empty",
        }
        for data, kind in cases.items():
            with self.subTest(head=data[:8]):
                self.assertEqual(classify(data).kind, kind)

    def test_extension_guess(self) -> None:
        self.assertEqual(classify(b"XPR0" + bytes(12)).ext, "xpr")
        self.assertEqual(classify(b"\x02\x00\x00\x00").ext, "bin")

    def test_adx_signature_without_copyright_is_not_adx(self) -> None:
        self.assertEqual(classify(b"\x80\x00\x00\x20" + bytes(60)).kind, "hex:80000020")

    def test_riff_without_wave_is_generic_riff(self) -> None:
        self.assertEqual(classify(b"RIFF\x00\x00\x00\x00AVI LIST").kind, "RIFF")


class TeamNinjaTagTests(unittest.TestCase):
    def test_tag_of_four_or_more_printables(self) -> None:
        self.assertEqual(team_ninja_tag(b"itm_dat2\x00\x00\x00\x00"), "itm_dat2")

    def test_three_letter_tag_needs_nul_terminator(self) -> None:
        self.assertEqual(team_ninja_tag(b"KMD\x00\x00"), "KMD")
        self.assertIsNone(team_ninja_tag(b"KMD\x01\x00"))

    def test_blank_tag_is_not_a_tag(self) -> None:
        self.assertIsNone(team_ninja_tag(b"    \x00\x01\x02"))

    def test_tag_kinds_are_described_as_team_ninja_chunks(self) -> None:
        self.assertIn("Team Ninja", describe_kind("tag:chr_dat"))
        self.assertEqual(describe_kind("hex:01000000"), "unknown")

    def test_shared_kind_keeps_its_first_description(self) -> None:
        self.assertEqual(describe_kind("AFS"), "AFS container (nested)")


class EntropyTests(unittest.TestCase):
    def test_entropy_values(self) -> None:
        self.assertEqual(shannon_entropy(b""), 0.0)
        self.assertEqual(shannon_entropy(bytes(1000)), 0.0)
        self.assertAlmostEqual(shannon_entropy(b"ab" * 500), 1.0)
        self.assertAlmostEqual(shannon_entropy(bytes(range(256)) * 16), 8.0)

    def test_bands(self) -> None:
        self.assertEqual(entropy_band(4.9, 10), EntropyBand.LOW)
        self.assertEqual(entropy_band(7.5, 10), EntropyBand.MID)
        self.assertEqual(entropy_band(7.9, 10), EntropyBand.HIGH)
        self.assertEqual(entropy_band(0.0, 0), EntropyBand.EMPTY)

    def test_head_ascii_masks_non_printables(self) -> None:
        self.assertEqual(head_ascii(b"KMD\x00\x7f~ "), "KMD..~ ")


if __name__ == "__main__":
    unittest.main()
