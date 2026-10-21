"""Ejecuta `gfx_test.exe` (presentador de frames, port/src/gfx) sin mostrar la ventana.

Necesita Windows y el test compilado (`port\\build.cmd` deja `build\\port\\gfx_test.exe`).
`NGB_LOADER` puede apuntar al loader de otra carpeta: el test se busca a su lado. Si falta,
se salta. El ejecutable ya comprueba por sí solo las conversiones (código de salida 1 si
fallan). Aquí se valida además el PNG que escribe en C: firma, IHDR, el CRC de cada chunk,
el zlib del IDAT y los colores de los cuadrantes del patrón swizzled.
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
    """(ancho, alto, filas RGB sin el byte de filtro) de un PNG RGB de 8 bits, filtro 0."""
    data = path.read_bytes()
    if not data.startswith(PNG_SIGNATURE):
        raise ValueError("firma PNG ausente")
    pos, idat, header = 8, b"", b""
    while pos < len(data):
        (length,) = struct.unpack_from(">I", data, pos)
        kind = data[pos + 4 : pos + 8]
        body = data[pos + 8 : pos + 8 + length]
        (crc,) = struct.unpack_from(">I", data, pos + 8 + length)
        if zlib.crc32(kind + body) != crc:
            raise ValueError(f"CRC incorrecto en {kind!r}")
        if kind == b"IHDR":
            header = body
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    width, height, depth, color = struct.unpack(">IIBB", header[:10])
    if (depth, color) != (8, 2):
        raise ValueError(f"se esperaba RGB de 8 bits, hay depth {depth} color {color}")
    raw = zlib.decompress(idat)
    stride = 1 + 3 * width
    if len(raw) != stride * height or any(raw[y * stride] for y in range(height)):
        raise ValueError("filas de tamaño o filtro inesperados")
    rows = b"".join(raw[y * stride + 1 : (y + 1) * stride] for y in range(height))
    return width, height, rows


@unittest.skipUnless(os.name == "nt" and GFX_TEST.is_file(), "requiere Windows y gfx_test.exe")
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
        self.assertIn("0 fallos", self.result.stdout)

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

        self.assertEqual(pixel(40, 10), (0xE0, 0x30, 0x30))  # rojo, arriba a la izquierda
        self.assertEqual(pixel(200, 50), (0x30, 0xC0, 0x30))  # verde, arriba a la derecha
        self.assertEqual(pixel(50, 200), (0x30, 0x50, 0xE0))  # azul, abajo a la izquierda
        self.assertEqual(pixel(200, 170), (0xE0, 0xD0, 0x30))  # amarillo, abajo a la derecha
        self.assertEqual(pixel(77, 77), (0xFF, 0xFF, 0xFF))  # diagonal
        self.assertEqual(pixel(64, 100), (0, 0, 0))  # rejilla cada 32 px


if __name__ == "__main__":
    unittest.main()
