"""Jerarquía de errores de la herramienta de escenarios.

La CLI traduce cada familia a un código de salida, igual que `ngb_tools.kmd`: unos datos
ilegibles son un hallazgo (1); una zona inexistente o un fallo de E/S, un error de uso (2).
Los errores de `ngb_tools.kmd` (`KmdError`) y de `ngb_tools.xpr` (`XprError`) se propagan
tal cual: un escenario se lee con sus parsers.
"""

from __future__ import annotations


class StageError(Exception):
    """Raíz de todos los errores de `ngb_tools.stage`."""


class StageFormatError(StageError):
    """Los bytes no forman un KMD de escenario legible (descriptor, HITMDL, emparejamiento)."""


class ZoneNotFoundError(StageError):
    """No hay ningún AFS ni ninguna parte para la zona o las partes pedidas."""
