"""Checks of `ngb-launcher.exe` that need no window on screen.

Needs Windows and the compiled launcher (`port\\build.cmd` leaves `build\\port\\ngb-launcher.exe`).
`NGB_LOADER` may point to the loader in another folder: the launcher is looked up next to it. If it
is missing, the tests are skipped.

- `--fonts FILE` writes the face GDI selected for each font of the toolkit and exits. The embedded
  Josefin Sans must win; with `--no-embedded-fonts` the Segoe UI fallback must be used instead.
- `--perf-anim FILE` composes 120 frames of the moving backdrop (Play hero and an inner page) and
  writes the mean milliseconds per frame. The design budget is 6 ms at 100 % DPI; the test allows
  a 16 ms frame so that slow CI machines do not fail it, and prints the figure.
"""

from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
LOADER = Path(os.environ.get("NGB_LOADER", REPO_ROOT / "build" / "port" / "ngb-loader.exe"))
LAUNCHER = LOADER.parent / "ngb-launcher.exe"


def font_faces(*extra: str) -> dict[str, str]:
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "fonts.txt"
        subprocess.run([str(LAUNCHER), "--fonts", str(out), *extra], check=True, timeout=30)
        lines = out.read_text(encoding="utf-8").splitlines()
    return dict(line.split("=", 1) for line in lines if "=" in line)


@unittest.skipUnless(os.name == "nt" and LAUNCHER.exists(), "needs Windows and ngb-launcher.exe")
class LauncherFontsTest(unittest.TestCase):
    def test_embedded_josefin_is_selected(self) -> None:
        faces = font_faces()
        for key in ("label", "display", "body"):
            self.assertTrue(faces[key].startswith("Josefin Sans"), f"{key}: {faces[key]}")

    def test_fallback_without_embedded_fonts(self) -> None:
        faces = font_faces("--no-embedded-fonts")
        self.assertEqual(faces["display"], "Segoe UI Light")
        self.assertEqual(faces["label"], "Segoe UI Semibold")

    def test_embedded_kanji_subset_is_selected(self) -> None:
        self.assertTrue(font_faces()["kanji"].startswith("Shippori Mincho B1"))

    def test_kanji_without_embedded_fonts_is_a_system_mincho_or_none(self) -> None:
        faces = font_faces("--no-embedded-fonts")
        self.assertIn(faces["kanji"], ("Yu Mincho", "MS Mincho", "none"))


@unittest.skipUnless(os.name == "nt" and LAUNCHER.exists(), "needs Windows and ngb-launcher.exe")
class LauncherAnimationTest(unittest.TestCase):
    def test_backdrop_frame_cost(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "perf.txt"
            subprocess.run([str(LAUNCHER), "--perf-anim", str(out), "--no-game-art"], check=True, timeout=60)
            values = dict(line.split("=", 1) for line in out.read_text(encoding="utf-8").split())
        hero, inner = float(values["hero_ms"]), float(values["inner_ms"])
        print(f"backdrop frame: hero {hero:.2f} ms, inner {inner:.2f} ms")
        self.assertLess(hero, 16.0)
        self.assertLess(inner, 16.0)


if __name__ == "__main__":
    unittest.main()
