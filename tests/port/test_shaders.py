"""Run the C test of the NV2A shader translators (port/tests/shaders_test.c).

`port\\build.cmd` builds and runs `build\\port\\shaders_test.exe`; `NGB_SHADERS_TEST` may
point to another. Without Windows or without the executable, it is skipped. If
`glslangValidator` is on the PATH, the generated GLSL is also compiled; if not, that test is
skipped and the text one remains.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
SHADERS_TEST = Path(
    os.environ.get("NGB_SHADERS_TEST", REPO_ROOT / "build" / "port" / "shaders_test.exe")
)
GLSLANG = shutil.which("glslangValidator")
DUMPED = ("basic.vert", "a0.vert", "modulate.frag", "two_stages.frag")


def run(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(SHADERS_TEST), *args], capture_output=True, text=True, timeout=60, check=False
    )


@unittest.skipUnless(os.name == "nt" and SHADERS_TEST.is_file(), "requires the compiled C test")
class ShadersTest(unittest.TestCase):
    def test_c_suite_passes(self) -> None:
        result = run()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("shaders_test: OK", result.stdout)

    def test_dump_writes_vertex_and_fragment_shaders(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            result = run("--dump", tmp)
            self.assertEqual(result.returncode, 0, result.stderr)
            for name in DUMPED:
                text = (Path(tmp) / name).read_text(encoding="ascii")
                self.assertTrue(text.startswith("#version 400\n"), name)
                self.assertIn("void main() {", text)

    @unittest.skipUnless(GLSLANG, "glslangValidator is not on the PATH")
    def test_generated_glsl_compiles(self) -> None:
        assert GLSLANG is not None
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(run("--dump", tmp).returncode, 0)
            for name in DUMPED:
                result = subprocess.run(
                    [GLSLANG, str(Path(tmp) / name)],
                    capture_output=True,
                    text=True,
                    timeout=60,
                    check=False,
                )
                self.assertEqual(result.returncode, 0, f"{name}:\n{result.stdout}")


if __name__ == "__main__":
    unittest.main()
