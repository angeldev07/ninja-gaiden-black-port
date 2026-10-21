"""Ejecuta el loader nativo compilado sobre XBE sintéticos y comprueba el log.

Necesita Windows y el loader compilado (`port\\build.cmd` deja `build\\port\\ngb-loader.exe`;
`NGB_LOADER` puede apuntar a otro). Si falta cualquiera de las dos cosas, se salta.
"""

from __future__ import annotations

import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

from .synthetic_loader import (
    ANSI_VA,
    DBG_PRINT,
    HAL_READ_SMC_TRAY_STATE,
    HOLA_VA,
    KE_QUERY_PERFORMANCE_FREQUENCY,
    RTL_INIT_ANSI_STRING,
    boot_code,
    build_loader_xbe,
    mmio_code,
    unimplemented_code,
)

REPO_ROOT = Path(__file__).resolve().parents[2]
LOADER = Path(os.environ.get("NGB_LOADER", REPO_ROOT / "build" / "port" / "ngb-loader.exe"))
EXIT_OK, EXIT_UNIMPLEMENTED, EXIT_CRASH = 0, 2, 3
HOSTPATH_TEST = LOADER.parent / "hostpath_test.exe"


@unittest.skipUnless(os.name == "nt" and LOADER.is_file(), "requiere Windows y el loader compilado")
class LoaderRunTest(unittest.TestCase):
    def run_xbe(self, xbe: bytes) -> tuple[int, str]:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "synthetic.xbe"
            log = Path(tmp) / "run.log"
            path.write_bytes(xbe)
            env = {**os.environ, "NGB_DATA": tmp}  # el HDD virtual también cae en tmp
            result = subprocess.run(
                [str(LOADER), str(path), "--log", str(log), "--timeout", "20"],
                capture_output=True,
                timeout=60,
                check=False,
                env=env,
            )
            return result.returncode, log.read_text(encoding="utf-8", errors="replace")

    def test_boot_calls_kernel_and_returns(self) -> None:
        code, log = self.run_xbe(build_loader_xbe(boot_code()))
        self.assertEqual(code, EXIT_OK, log)
        self.assertIn("thunk: 0x00011800, 3 funciones + 1 variables enlazadas", log)
        freq = re.search(
            rf"\[1\] T1 @\w+ {KE_QUERY_PERFORMANCE_FREQUENCY} "
            r"KeQueryPerformanceFrequency\(\) -> 0x(\w+)",
            log,
        )
        self.assertIsNotNone(freq, log)
        assert freq is not None
        self.assertEqual(int(freq.group(1), 16), 733333333)  # 64 bits en EDX:EAX
        self.assertIn("DbgPrint: ngb synthetic 42", log)
        self.assertRegex(log, rf"\[2\] T1 @\w+ {DBG_PRINT} DbgPrint\(0x\w+, 0x0000002A, ")
        self.assertIn(
            f"{RTL_INIT_ANSI_STRING} RtlInitAnsiString(0x{ANSI_VA:08X}, 0x{HOLA_VA:08X})", log
        )
        self.assertIn("DbgPrint: len=4", log)  # la pila stdcall quedó bien: siguió ejecutando
        self.assertIn("el entry point ha vuelto", log)
        self.assertIn("STOP OK", log)
        self.assertIn("resumen: 3 funciones distintas llamadas (3 implementadas)", log)

    def test_unimplemented_call_stops(self) -> None:
        xbe = build_loader_xbe(unimplemented_code(), imports=(HAL_READ_SMC_TRAY_STATE,))
        code, log = self.run_xbe(xbe)
        self.assertEqual(code, EXIT_UNIMPLEMENTED, log)
        self.assertIn("9 HalReadSMCTrayState(0, 0) -> NO IMPLEMENTADA", log)
        self.assertIn("primera sin implementar: 9 HalReadSMCTrayState", log)

    def test_nv2a_mmio_is_reported_as_crash(self) -> None:
        code, log = self.run_xbe(build_loader_xbe(mmio_code(), imports=()))
        self.assertEqual(code, EXIT_CRASH, log)
        self.assertIn("EXCEPCIÓN ACCESS_VIOLATION", log)
        self.assertIn("lectura de 0xFD000000: MMIO NV2A (GPU)", log)
        self.assertIn("código del XBE", log)


@unittest.skipUnless(os.name == "nt" and HOSTPATH_TEST.is_file(), "requiere el test C compilado")
class HostPathTest(unittest.TestCase):
    def test_paths_never_leave_root(self) -> None:
        """hostpath_test.exe: '..', rutas absolutas y ':' no salen de la raíz."""
        result = subprocess.run(
            [str(HOSTPATH_TEST)], capture_output=True, text=True, timeout=30, check=False
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("hostpath_test: OK", result.stdout)


if __name__ == "__main__":
    unittest.main()
