"""HLE de la entrada de XAPI (H8): el loader parchea XAPI y el XBE sintético la llama.

Necesita Windows y el loader compilado (ver test_loader). `--no-host-input` deja solo el
guion de `--input-script`: la prueba no depende del teclado ni de un mando conectado.
"""

from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from .synthetic_input import SIGNATURES, build_input_xbe, input_code, xpp_section
from .synthetic_loader import boot_code, build_loader_xbe
from .test_loader import EXIT_OK, LOADER

ERROR_IO_PENDING = 0x3E5
ERROR_DEVICE_NOT_CONNECTED = 0x48F


@unittest.skipUnless(os.name == "nt" and LOADER.is_file(), "requiere Windows y el loader compilado")
class InputHleTest(unittest.TestCase):
    def run_xbe(self, xbe: bytes, *extra_args: str) -> tuple[int, str]:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "synthetic.xbe"
            log = Path(tmp) / "run.log"
            path.write_bytes(xbe)
            env = {**os.environ, "NGB_DATA": tmp}
            result = subprocess.run(
                [str(LOADER), str(path), "--log", str(log), "--timeout", "20", *extra_args],
                capture_output=True,
                timeout=60,
                check=False,
                env=env,
            )
            return result.returncode, log.read_text(encoding="utf-8", errors="replace")

    def test_scripted_pad_reaches_the_game(self) -> None:
        code, log = self.run_xbe(
            build_input_xbe(input_code()),
            "--no-host-input",
            "--input-script",
            "t=0ms:START+A+LT@60s",
        )
        self.assertEqual(code, EXIT_OK, log)
        self.assertIn("input: 8 of 8 XAPI functions patched", log)
        self.assertIn("keyboard: disabled; script: 1 presses", log)
        for name, (va, _, ret_va, ret_n) in SIGNATURES.items():
            self.assertIn(f"input: {name} 0x{va:08X} (ret {ret_n} at 0x{ret_va:08X};", log)
        # La primera consulta de cambios notifica la inserción; XGetDevices ve el puerto 0.
        self.assertIn("DbgPrint: changes=1 ins=1 rem=0", log)
        self.assertIn("DbgPrint: devices=1", log)
        # XINPUT_STATE de Xbox: START en wButtons; A y el gatillo izquierdo, analógicos.
        self.assertIn("DbgPrint: state=0 pkt=2 btn=0010 a=255 b=0 lt=255", log)
        # SetState: ERROR_IO_PENDING como la XAPI real, completado al momento (dwStatus 0).
        self.assertIn(f"DbgPrint: feedback={ERROR_IO_PENDING:x} status=0", log)
        self.assertRegex(log, r"input: T1 @0001[0-9A-F]{4} XInputGetState\(0x[0-9A-F]+\)")

    def test_without_script_the_pad_is_idle(self) -> None:
        code, log = self.run_xbe(build_input_xbe(input_code()), "--no-host-input")
        self.assertEqual(code, EXIT_OK, log)
        self.assertIn("DbgPrint: state=0 pkt=1 btn=0000 a=0 b=0 lt=0", log)

    def test_signature_mismatch_skips_only_that_function(self) -> None:
        signatures = dict(SIGNATURES)
        va, prologue, ret_va, _ = signatures["XInputSetState"]
        signatures["XInputSetState"] = (va, prologue, ret_va, 12)  # ret 12, no ret 8
        code, log = self.run_xbe(
            build_input_xbe(input_code(), xpp_section(signatures)), "--no-host-input"
        )
        # Sin parche, la llamada cae en los int3 de la sección: el loader lo para.
        self.assertNotEqual(code, EXIT_OK, log)
        self.assertIn("input: 7 of 8 XAPI functions patched", log)
        self.assertIn(
            f"input: XInputSetState (0x{va:08X}) not patched: no ret 8 at 0x{ret_va:08X}",
            log,
        )
        self.assertIn("DbgPrint: devices=1", log)

    def test_other_xbe_is_left_alone(self) -> None:
        code, log = self.run_xbe(build_loader_xbe(boot_code()))
        self.assertEqual(code, EXIT_OK, log)
        self.assertIn("input: Ninja Gaiden Black XAPI not recognized in this XBE", log)
        self.assertNotIn("patched", log)

    def test_invalid_script_is_a_usage_error(self) -> None:
        code, log = self.run_xbe(build_input_xbe(input_code()), "--input-script", "t=1s:JUMP")
        self.assertEqual(code, 1, log)
        self.assertIn("invalid --input-script: entry 't=1s:JUMP': unknown button", log)


if __name__ == "__main__":
    unittest.main()
