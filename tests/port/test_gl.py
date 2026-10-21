"""Ejecuta la prueba del backend de OpenGL (port/tests/gl_test.c) y revisa su captura.

`port\\build.cmd` deja `build\\port\\gl_test.exe` (`NGB_GL_TEST` puede apuntar a otro). Sin
Windows o sin el ejecutable, se salta; si no hay contexto OpenGL 4.x (código 77), también.
"""

from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from xpr.pngread import read_png

REPO_ROOT = Path(__file__).resolve().parents[2]
GL_TEST = Path(os.environ.get("NGB_GL_TEST", REPO_ROOT / "build" / "port" / "gl_test.exe"))
EXIT_NO_GL = 77
WIDTH, HEIGHT = 720, 480
LIGHT, DARK = (240, 200, 60, 255), (30, 60, 160, 255)


def run(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(GL_TEST), *args], capture_output=True, text=True, timeout=120, check=False
    )


@unittest.skipUnless(os.name == "nt" and GL_TEST.is_file(), "requiere gl_test.exe compilado")
class GlBackendTest(unittest.TestCase):
    def run_or_skip(self, *args: str) -> subprocess.CompletedProcess[str]:
        result = run(*args)
        if result.returncode == EXIT_NO_GL:
            self.skipTest(f"sin contexto OpenGL 4.x: {result.stdout.strip()}")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def test_triangle_and_checker_quad(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "gl_test.png"
            result = self.run_or_skip("--hidden", "--out", str(out))
            self.assertIn("gl_test: OK", result.stdout)
            png = read_png(out.read_bytes())
        self.assertEqual((png.width, png.height), (WIDTH, HEIGHT))

        def pixel(x: int, y: int) -> tuple[int, ...]:
            start = 4 * (y * WIDTH + x)
            return tuple(png.rgba[start : start + 4])

        red = pixel(100, 170)
        self.assertGreater(red[0], red[1] + 80)
        blue = pixel(180, 40)
        self.assertGreater(blue[2], blue[0] + 80)
        self.assertEqual(pixel(429, 35), LIGHT)
        self.assertEqual(pixel(461, 35), DARK)
        self.assertEqual(pixel(461, 58), LIGHT)
        # DXT1 por cuadrantes y P8 swizzled con paleta (texconv), sin pérdidas.
        self.assertEqual(pixel(117, 348), (255, 0, 0, 255))
        self.assertEqual(pixel(243, 420), (255, 255, 255, 255))
        self.assertEqual(pixel(500, 325), (0, 200, 200, 255))
        self.assertEqual(pixel(437, 397), (128, 0, 255, 255))
        # Banda roja al 50 % (blend SRC_ALPHA / ONE_MINUS_SRC_ALPHA) sobre el fondo.
        bar = pixel(360, 240)
        self.assertTrue(132 <= bar[0] <= 148 and bar[1] < 25 and bar[2] < 25, bar)

    def test_every_generated_variant_compiles(self) -> None:
        result = self.run_or_skip("--stress")
        self.assertRegex(result.stdout, r"--stress compilan (\d+) de \1 variantes")


if __name__ == "__main__":
    unittest.main()
