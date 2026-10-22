"""Regenerate `port/src/kernel/stubs.c` from the kernel exports table (TSV).

Wrapper around `python -m ngb_tools.port.kernel_stubs`, runnable from the repo root
without installing the package: `python tools/gen_kernel_stubs.py [--check]`.
"""

from __future__ import annotations

import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "src"))

from ngb_tools.port.kernel_stubs import main  # noqa: E402

if __name__ == "__main__":
    out = ["--out", str(REPO_ROOT / "port" / "src" / "kernel" / "stubs.c")]
    sys.exit(main([*out, *sys.argv[1:]]))
