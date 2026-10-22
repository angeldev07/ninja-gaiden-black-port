"""Run `gfx_test.exe` (frame presenter, port/src/gfx) without showing the window.

Needs Windows and the compiled test (`port\\build.cmd` leaves `build\\port\\gfx_test.exe`).
`NGB_LOADER` may point to the loader in another folder: the test is looked up next to it. If it is
missing, the test is skipped. The executable already checks the conversions by itself (exit
code 1 if they fail). Here the PNG it writes in C is also validated: signature, IHDR, the CRC
of each chunk, the IDAT zlib stream and the colors of the quadrants of the swizzled pattern.
"""

from __future__ import annotations

import os
import struct
import subprocess
import tempfile
import unittest
import zlib
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
LOADER = Path(os.environ.get("NGB_LOADER", REPO_ROOT / "build" / "port" / "ngb-loader.exe"))
GFX_TEST = LOADER.parent / "gfx_test.exe"
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def read_png_rgb(path: Path) -> tuple[int, int, bytes]:
    """(width, height, RGB rows without the filter byte) of an 8-bit RGB PNG, filter 0."""
    data = path.read_bytes()
    if not data.startswith(PNG_SIGNATURE):
        raise ValueError("PNG signature missing")
    pos, idat, header = 8, b"", b""
    while pos < len(data):
        (length,) = struct.unpack_from(">I", data, pos)
        kind = data[pos + 4 : pos + 8]
        body = data[pos + 8 : pos + 8 + length]
        (crc,) = struct.unpack_from(">I", data, pos + 8 + length)
        if zlib.crc32(kind + body) != crc:
            raise ValueError(f"bad CRC in {kind!r}")
        if kind == b"IHDR":
            header = body
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    width, height, depth, color = struct.unpack(">IIBB", header[:10])
    if (depth, color) != (8, 2):
        raise ValueError(f"expected 8-bit RGB, got depth {depth} color {color}")
    raw = zlib.decompress(idat)
    stride = 1 + 3 * width
    if len(raw) != stride * height or any(raw[y * stride] for y in range(height)):
        raise ValueError("rows of unexpected size or filter")
    rows = b"".join(raw[y * stride + 1 : (y + 1) * stride] for y in range(height))
    return width, height, rows


@unittest.skipUnless(os.name == "nt" and GFX_TEST.is_file(), "requires Windows and gfx_test.exe")
class GfxTestRun(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.shots = Path(self.tmp.name)
        result = subprocess.run(
            [str(GFX_TEST), "--frames", "3", "--hidden", "--delay", "0", "--shot", str(self.shots)],
            capture_output=True,
            timeout=60,
            check=False,
            text=True,
        )
        self.result = result

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def test_exits_cleanly_with_no_failed_checks(self) -> None:
        self.assertEqual(self.result.returncode, 0, self.result.stderr)
        self.assertIn("0 failures", self.result.stdout)

    def test_gradient_capture_is_a_720x480_png(self) -> None:
        path = self.shots / "gfx_test-gradient.png"
        self.assertTrue(path.read_bytes().startswith(PNG_SIGNATURE))
        width, height, _ = read_png_rgb(path)
        self.assertEqual((width, height), (720, 480))

    def test_swizzled_capture_shows_the_known_pattern(self) -> None:
        width, height, rgb = read_png_rgb(self.shots / "gfx_test-swizzled.png")
        self.assertEqual((width, height), (256, 256))

        def pixel(x: int, y: int) -> tuple[int, ...]:
            i = 3 * (y * width + x)
            return tuple(rgb[i : i + 3])

        self.assertEqual(pixel(40, 10), (0xE0, 0x30, 0x30))  # red, top left
        self.assertEqual(pixel(200, 50), (0x30, 0xC0, 0x30))  # green, top right
        self.assertEqual(pixel(50, 200), (0x30, 0x50, 0xE0))  # blue, bottom left
        self.assertEqual(pixel(200, 170), (0xE0, 0xD0, 0x30))  # yellow, bottom right
        self.assertEqual(pixel(77, 77), (0xFF, 0xFF, 0xFF))  # diagonal
        self.assertEqual(pixel(64, 100), (0, 0, 0))  # grid every 32 px


if __name__ == "__main__":
    unittest.main()
