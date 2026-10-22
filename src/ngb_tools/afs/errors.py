"""Error hierarchy of the AFS tool.

The CLI maps each family to an exit code: format errors are a finding about the data (1)
and usage errors are a failure of the caller (2).

`UnsafeOutputError` (output inside a repo) is common to all the tools and lives in
`ngb_tools._paths`; it is re-exported here and the CLI treats it as a usage error (2).
"""

from __future__ import annotations

from ngb_tools._paths import UnsafeOutputError

__all__ = [
    "AfsError",
    "AfsFormatError",
    "AfsTruncatedError",
    "AfsUsageError",
    "UnsafeOutputError",
]


class AfsError(Exception):
    """Root of the errors specific to `ngb_tools.afs`."""


class AfsFormatError(AfsError):
    """The bytes do not form a valid AFS (impossible magic, header or TOC)."""


class AfsTruncatedError(AfsFormatError):
    """The file ends before what the header or the TOC declare."""


class AfsUsageError(AfsError):
    """The request makes no sense for this file (e.g. a nonexistent index)."""
