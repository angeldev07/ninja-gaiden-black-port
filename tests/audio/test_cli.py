"""Audio CLI over a synthetic AFS: subcommands, manifest and exit codes."""

from __future__ import annotations

import contextlib
import io
import json
import tempfile
import unittest
import wave
from pathlib import Path
from typing import Any
from unittest import mock

from ngb_tools.audio.cli import ExitCode, main

from .synthetic import (
    adx_frame,
    adx_header,
    build_afs,
    build_wva,
    build_xwb,
    wva_record,
    xbox_block,
)

REPO = Path(__file__).resolve().parents[2]
ZERO = bytes(4)


def sample_adx() -> bytes:
    # 64 mono samples: scale 3 -> 4, first nibble 2 -> 8; then silence.
    return adx_header(total=64, loop=(0, 64)) + adx_frame(3, [2]) + adx_frame(0, [])


def sample_wva() -> bytes:
    record = wva_record(xbox_block([(9, 0)], [[ZERO] * 8]))
    return build_wva([(record, True), None])


class AudioCliTestCase(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.afs = self.root / "game" / "snd.afs"
        self.afs.parent.mkdir()
        encrypted = adx_header(version=4, flags=0x09) + adx_frame(0, [])
        self.afs.write_bytes(
            build_afs(
                [
                    ("song.adx", sample_adx()),
                    ("locked.adx", encrypted),
                    ("fx.wva", sample_wva()),
                    ("notes.txt", b"not audio\n"),
                ]
            )
        )

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def run_cli(self, *argv: str) -> tuple[int, str, str]:
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = main(list(argv))
        return code, out.getvalue(), err.getvalue()

    def manifest(self, out_dir: Path) -> Any:
        return json.loads((out_dir / "_manifest.json").read_text("utf-8"))


class AdxCommandTests(AudioCliTestCase):
    def test_single_index_writes_a_readable_wav(self) -> None:
        out_dir = self.root / "out"
        code, out, _ = self.run_cli("adx", str(self.afs), "--index", "0", "--out", str(out_dir))
        self.assertEqual(code, ExitCode.OK)
        self.assertIn("0000_song.wav", out)
        with wave.open(str(out_dir / "0000_song.wav"), "rb") as reader:
            self.assertEqual((reader.getnchannels(), reader.getframerate()), (1, 44100))
            self.assertEqual(reader.getnframes(), 64)
            self.assertEqual(reader.readframes(1), b"\x08\x00")
        (track,) = self.manifest(out_dir)["tracks"]
        self.assertEqual(track["loop"], {"start_sample": 0, "end_sample": 64})
        self.assertEqual(track["status"], "ok")

    def test_encrypted_track_is_reported_and_skipped(self) -> None:
        out_dir = self.root / "all"
        code, out, _ = self.run_cli("adx", str(self.afs), "--out", str(out_dir))
        self.assertEqual(code, ExitCode.FINDINGS)
        self.assertIn("encrypted", out)
        manifest = self.manifest(out_dir)
        statuses = [(t["name"], t["status"]) for t in manifest["tracks"]]
        self.assertEqual(statuses, [("song.adx", "ok"), ("locked.adx", "encrypted")])
        self.assertEqual(sorted(p.name for p in out_dir.glob("*.wav")), ["0000_song.wav"])

    def test_default_output_goes_under_ngb_data(self) -> None:
        with mock.patch.dict("os.environ", {"NGB_DATA": str(self.root)}):
            code, _, _ = self.run_cli("adx", str(self.afs), "--index", "0")
        self.assertEqual(code, ExitCode.OK)
        self.assertTrue((self.root / "extracted" / "audio" / "snd" / "0000_song.wav").exists())

    def test_unknown_index_is_a_usage_error(self) -> None:
        code, _, err = self.run_cli("adx", str(self.afs), "--index", "9", "--out", str(self.root))
        self.assertEqual(code, ExitCode.ERROR)
        self.assertIn("out of range", err)

    def test_non_adx_entry_by_index_is_a_finding(self) -> None:
        out_dir = self.root / "txt"
        code, _, _ = self.run_cli("adx", str(self.afs), "--index", "3", "--out", str(out_dir))
        self.assertEqual(code, ExitCode.FINDINGS)
        self.assertEqual(self.manifest(out_dir)["summary"]["error"], 1)

    def test_output_inside_repo_is_refused(self) -> None:
        target = REPO / "build" / "audio_nope"
        code, _, err = self.run_cli("adx", str(self.afs), "--out", str(target))
        self.assertEqual(code, ExitCode.ERROR)
        self.assertIn("inside repo", err)
        self.assertFalse(target.exists())

    def test_missing_file_is_an_io_error(self) -> None:
        code, _, _ = self.run_cli("adx", str(self.root / "nope.afs"), "--out", str(self.root))
        self.assertEqual(code, ExitCode.ERROR)


class WvaAndXwbCommandTests(AudioCliTestCase):
    def test_wva_records_become_wavs(self) -> None:
        out_dir = self.root / "wva"
        code, out, _ = self.run_cli("wva", str(self.afs), "--out", str(out_dir))
        self.assertEqual(code, ExitCode.OK)
        self.assertIn("1/1 ok", out)
        with wave.open(str(out_dir / "0002_fx_00.wav"), "rb") as reader:
            self.assertEqual((reader.getframerate(), reader.getnframes()), (22050, 64))

    def test_xwb_entries_become_wavs(self) -> None:
        xwb_path = self.root / "Wave.xwb"
        xwb_path.write_bytes(build_xwb([(0, 1, 48000, 16, b"\x01\x00\x02\x00")]))
        out_dir = self.root / "xwb"
        code, _, _ = self.run_cli("xwb", str(xwb_path), "--out", str(out_dir))
        self.assertEqual(code, ExitCode.OK)
        with wave.open(str(out_dir / "0000_wave.wav"), "rb") as reader:
            self.assertEqual(reader.readframes(-1), b"\x01\x00\x02\x00")

    def test_analyze_writes_a_png_next_to_the_wav(self) -> None:
        out_dir = self.root / "an"
        self.run_cli("adx", str(self.afs), "--index", "0", "--out", str(out_dir))
        code, out, _ = self.run_cli("analyze", str(out_dir / "0000_song.wav"))
        self.assertEqual(code, ExitCode.OK)
        self.assertIn("RMS/s", out)
        self.assertEqual((out_dir / "0000_song.png").read_bytes()[:4], b"\x89PNG")


if __name__ == "__main__":
    unittest.main()
