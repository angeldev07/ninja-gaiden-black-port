"""Typed errors of `ngb_tools.xbe`.

Hierarchy:

    XbeError                    base of the whole package
    ├── XbeFormatError          the file is not a valid XBE or is inconsistent
    │   ├── TruncatedDataError  a structure runs past the end of the file
    │   ├── BadMagicError       does not start with 'XBEH'
    │   ├── UnmappedAddressError  a VA does not point to file data
    │   ├── ImageLayoutError    structure outside its region, 32-bit overflow or bound exceeded
    │   └── ThunkTableError     kernel import table without a terminator
    └── KernelExportsError      the TSV exports table is malformed

The CLI maps `XbeFormatError` to a validation failure (code 1) and
`KernelExportsError` to a usage error (code 2): the XBE is not at fault.
"""

from __future__ import annotations


class XbeError(Exception):
    """Base of the XBE inspector errors."""


class XbeFormatError(XbeError):
    """The file is not a valid XBE, is truncated or is inconsistent."""


class TruncatedDataError(XbeFormatError):
    """Read of `size` bytes at `offset` falls outside the file bounds."""

    def __init__(self, offset: int, size: int) -> None:
        super().__init__(f"read of {size} bytes outside the file at offset 0x{offset:X}")
        self.offset = offset
        self.size = size


class BadMagicError(XbeFormatError):
    """The signature in the first 4 bytes is not 'XBEH'."""


class UnmappedAddressError(XbeFormatError):
    """A virtual address (VA) is not backed by bytes of the file.

    Happens with pointers into BSS (memory the loader fills with zeros) or
    outside the image.
    """

    def __init__(self, va: int, what: str) -> None:
        super().__init__(f"{what}: VA 0x{va:08X} is not backed by the file")
        self.va = va
        self.what = what


class ImageLayoutError(XbeFormatError):
    """The declared layout is impossible: a structure outside its region, a range that
    overflows 32 bits or runs past the image, or a counter above its bound.

    Checking this before walking anything is what stops a hostile XBE from hanging
    the analysis with a `section_count` of billions.
    """


class ThunkTableError(XbeFormatError):
    """The kernel thunk table does not end in 0 within the bound."""


class KernelExportsError(XbeError):
    """A line of the xboxkrnl exports table cannot be parsed."""
