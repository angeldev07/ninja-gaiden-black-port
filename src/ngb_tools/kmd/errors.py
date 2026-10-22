"""Error hierarchy of the KMD tool.

The CLI maps each family to an exit code: a KMD that cannot be read is a finding about the
data (1). Caller failures (2) do not belong to this package: I/O (`OSError`) and output inside
a repo (`ngb_tools._paths.UnsafeOutputError`).

Data that can be read but violates an invariant is not an error but an issue
(`ngb_tools.kmd.model.Issue`): the export continues without the affected part.
"""

from __future__ import annotations


class KmdError(Exception):
    """Root of all `ngb_tools.kmd` errors."""


class KmdFormatError(KmdError):
    """The bytes do not form a readable KMD (chunk, subchunks, XPR0 or descriptors)."""


class UnsupportedVertexFormatError(KmdError):
    """FVF that cannot be decoded (transformed position, unknown bits...)."""


class PushBufferError(KmdFormatError):
    """Push buffer with commands outside the supported subset (BEGIN_END and ARRAY_ELEMENT16)."""
