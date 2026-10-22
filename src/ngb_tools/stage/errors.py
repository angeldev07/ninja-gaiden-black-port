"""Error hierarchy of the stage tool.

The CLI maps each family to an exit code, like `ngb_tools.kmd`: unreadable data is a
finding (1); a nonexistent zone or an I/O failure is a usage error (2). Errors from
`ngb_tools.kmd` (`KmdError`) and `ngb_tools.xpr` (`XprError`) propagate as they are: a stage
is read with their parsers.
"""

from __future__ import annotations


class StageError(Exception):
    """Root of all `ngb_tools.stage` errors."""


class StageFormatError(StageError):
    """The bytes do not form a readable stage KMD (descriptor, HITMDL, pairing)."""


class ZoneNotFoundError(StageError):
    """There is no AFS or part for the requested zone or parts."""
