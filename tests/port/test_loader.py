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
    PB_PUT,
    RTL_INIT_ANSI_STRING,
    boot_code,
    build_loader_xbe,
    gpu_code,
    gpu_extra,
    mmio_code,
    unimplemented_code,
)

REPO_ROOT = Path(__file__).resolve().parents[2]
LOADER = Path(os.environ.get("NGB_LOADER", REPO_ROOT / "build" / "port" / "ngb-loader.exe"))
EXIT_OK, EXIT_UNIMPLEMENTED, EXIT_CRASH = 0, 2, 3
HOSTPATH_TEST = LOADER.parent / "hostpath_test.exe"
PUSHBUF_TEST = LOADER.parent / "pushbuf_test.exe"
RASTER_TEST = LOADER.parent / "raster_test.exe"


@unittest.skipUnless(os.name == "nt" and LOADER.is_file(), "requiere Windows y el loader compilado")
class LoaderRunTest(unittest.TestCase):
    pgraph_log = ""

    def run_xbe(self, xbe: bytes, *extra_args: str) -> tuple[int, str]:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "synthetic.xbe"
            log = Path(tmp) / "run.log"
            path.write_bytes(xbe)
            env = {**os.environ, "NGB_DATA": tmp}  # el HDD virtual también cae en tmp
            result = subprocess.run(
                [str(LOADER), str(path), "--log", str(log), "--timeout", "20", *extra_args],
                capture_output=True,
                timeout=60,
                check=False,
                env=env,
            )
            pgraph = Path(tmp) / "run-pgraph.log"
            if pgraph.is_file():
                self.pgraph_log = pgraph.read_text(encoding="utf-8", errors="replace")
            return result.returncode, log.read_text(encoding="utf-8", errors="replace")

    def test_log_inside_a_git_repo_is_refused(self) -> None:
        # Junto al log se vuelcan PNG del juego: un --log dentro de un repo se rechaza antes de
        # ejecutar nada.
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp) / "repo"
            (repo / ".git").mkdir(parents=True)
            xbe = Path(tmp) / "synthetic.xbe"
            xbe.write_bytes(build_loader_xbe(boot_code()))
            log = repo / "runs" / "run.log"
            result = subprocess.run(
                [str(LOADER), str(xbe), "--log", str(log), "--timeout", "20"],
                capture_output=True,
                timeout=60,
                check=False,
                env={**os.environ, "NGB_DATA": tmp},
            )
        self.assertNotEqual(result.returncode, EXIT_OK)
        self.assertIn(b"repo git", result.stderr)
        self.assertFalse(log.exists())

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

    def test_unmodelled_mmio_is_reported_as_crash(self) -> None:
        """Con --mmio-strict, un acceso a la APU para la ejecución con informe."""
        xbe = build_loader_xbe(mmio_code(0xFE820010), imports=())
        code, log = self.run_xbe(xbe, "--mmio-strict")
        self.assertEqual(code, EXIT_CRASH, log)
        self.assertIn("EXCEPCIÓN ACCESS_VIOLATION", log)
        self.assertIn("lectura de 0xFE820010: MMIO APU (audio)", log)
        self.assertIn("código del XBE", log)

    def test_nv2a_registers_and_push_buffer(self) -> None:
        """H7a: PMC_BOOT_0, flush de PFB que termina solo y un push buffer decodificado."""
        xbe = build_loader_xbe(gpu_code(), imports=(DBG_PRINT,), extra=gpu_extra())
        code, log = self.run_xbe(xbe)
        self.assertEqual(code, EXIT_OK, log)
        self.assertIn(f"DbgPrint: boot=02a000a3 get={PB_PUT:08x}", log)
        pg = self.pgraph_log
        self.assertIn("SET_COLOR_CLEAR_VALUE              = 0xFF336699", pg)
        self.assertIn("CLEAR_SURFACE                      = 0x000000F0", pg)
        self.assertIn("FLIP_STALL", pg)
        self.assertIn("fin del frame 0", pg)


@unittest.skipUnless(os.name == "nt" and HOSTPATH_TEST.is_file(), "requiere el test C compilado")
class HostPathTest(unittest.TestCase):
    def test_paths_never_leave_root(self) -> None:
        """hostpath_test.exe: '..', rutas absolutas y ':' no salen de la raíz."""
        result = subprocess.run(
            [str(HOSTPATH_TEST)], capture_output=True, text=True, timeout=30, check=False
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("hostpath_test: OK", result.stdout)


@unittest.skipUnless(os.name == "nt" and PUSHBUF_TEST.is_file(), "requiere el test C compilado")
class PushBufferTest(unittest.TestCase):
    def test_decoder(self) -> None:
        """pushbuf_test.exe: cabeceras, saltos, llamadas y errores del push buffer."""
        result = subprocess.run(
            [str(PUSHBUF_TEST)], capture_output=True, text=True, timeout=30, check=False
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("pushbuf_test: OK", result.stdout)


@unittest.skipUnless(os.name == "nt" and RASTER_TEST.is_file(), "requiere el test C compilado")
class RasterTest(unittest.TestCase):
    def test_software_rasterizer(self) -> None:
        """raster_test.exe: cobertura de un triángulo, textura 2x2, paleta, mezcla y alfa."""
        result = subprocess.run(
            [str(RASTER_TEST)], capture_output=True, text=True, timeout=30, check=False
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("raster_test: OK", result.stdout)


if __name__ == "__main__":
    unittest.main()
