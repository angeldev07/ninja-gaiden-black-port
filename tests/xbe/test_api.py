"""API pública del paquete: el flujo típico solo necesita `ngb_tools.xbe`."""

from __future__ import annotations

import json
import unittest

from ngb_tools.xbe import (
    BadMagicError,
    XbeError,
    XbeFormatError,
    XbeImage,
    analyze,
    load_kernel_exports,
    render_json,
    render_markdown,
)

from .synthetic import build_synthetic_xbe


class PublicApiTests(unittest.TestCase):
    def test_typical_flow(self) -> None:
        image = XbeImage.from_bytes(build_synthetic_xbe(), "synthetic.xbe")
        analysis = analyze(image, load_kernel_exports(), with_strings=True)
        self.assertEqual(json.loads(render_json(analysis))["certificate"]["title_id_str"], "TC-013")
        self.assertIn("# XBE: synthetic.xbe", render_markdown(analysis))

    def test_errors_share_a_base(self) -> None:
        with self.assertRaises(XbeError) as caught:
            XbeImage.from_bytes(b"MZ" + b"\0" * 0x200)
        self.assertIsInstance(caught.exception, XbeFormatError)
        self.assertIsInstance(caught.exception, BadMagicError)


if __name__ == "__main__":
    unittest.main()
