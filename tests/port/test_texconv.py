"""texconv.c contra ngb_tools.xpr: el .h de vectores está al día y el test en C pasa.

`port\\build.cmd` compila y ejecuta `build\\port\\texconv_test.exe` (no necesita GPU);
`NGB_TEXCONV_TEST` puede apuntar a otro. Sin Windows o sin el ejecutable, esa parte se salta.
"""

from __future__ import annotations

import os
import subprocess
import unittest
from pathlib import Path

from . import texconv_vectors

REPO_ROOT = Path(__file__).resolve().parents[2]
TEXCONV_TEST = Path(
    os.environ.get("NGB_TEXCONV_TEST", REPO_ROOT / "build" / "port" / "texconv_test.exe")
)


class VectorsTest(unittest.TestCase):
    def test_header_matches_the_python_reference(self) -> None:
        expected = texconv_vectors.render()
        actual = texconv_vectors.HEADER.read_text(encoding="ascii")
        self.assertEqual(
            actual, expected, "regenera con: python tests/port/texconv_vectors.py --write"
        )

    def test_every_case_has_its_expected_size(self) -> None:
        for case in texconv_vectors.CASES:
            with self.subTest(case=case.name):
                src, _, expected = texconv_vectors.build(case, texconv_vectors.random.Random(1))
                faces = 6 if case.cube else 1
                self.assertEqual(len(src) % faces, 0)
                self.assertGreater(len(expected), 0)


@unittest.skipUnless(os.name == "nt" and TEXCONV_TEST.is_file(), "requiere texconv_test.exe")
class TexconvBinaryTest(unittest.TestCase):
    def test_c_suite_passes(self) -> None:
        result = subprocess.run(
            [str(TEXCONV_TEST)], capture_output=True, text=True, timeout=60, check=False
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("texconv_test: OK", result.stdout)


if __name__ == "__main__":
    unittest.main()
