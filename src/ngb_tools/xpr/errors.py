"""Error hierarchy of the XPR tool.

The CLI maps each family to an exit code:

- 1, validation finding: a broken XPR0 (`XprFormatError`) or textures that cannot be
  exported (`UnsupportedTextureError`).
- 2, usage or input error: the file is not what was asked for (`XprInputError`: not an
  XPR0, or with `--kmd` not a chunk with an XPR0 slot), I/O (`OSError`) and output
  inside a repo (`ngb_tools._paths.UnsafeOutputError`, common to the tools).
"""

from __future__ import annotations


class XprError(Exception):
    """Root of all `ngb_tools.xpr` errors."""


class XprInputError(XprError):
    """The input is not what was asked for: not XPR0, or not a chunk with an XPR0 slot."""


class XprFormatError(XprError):
    """The bytes do not form a valid XPR0 (magic, sizes or resource headers)."""


class XprTruncatedError(XprFormatError):
    """The XPR0 or a resource ends beyond the available bytes."""


class UnsupportedTextureError(XprError):
    """Recognized texture that cannot be exported (cubemap, volume, format or palette)."""


class TextureTooLargeError(UnsupportedTextureError):
    """Dimensions above the pixel cap: rejected before allocating memory."""
