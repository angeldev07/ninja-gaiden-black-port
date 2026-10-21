"""Jerarquía de errores de la herramienta AFS.

La CLI traduce cada familia a un código de salida: los errores de formato son un
hallazgo sobre los datos (1) y los de uso son un fallo de quien invoca (2).

`UnsafeOutputError` (salida dentro de un repo) es común a todas las herramientas y vive en
`ngb_tools._paths`; se reexporta aquí y la CLI la trata como error de uso (2).
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
    """Raíz de los errores propios de `ngb_tools.afs`."""


class AfsFormatError(AfsError):
    """Los bytes no forman un AFS válido (magia, cabecera o TOC imposibles)."""


class AfsTruncatedError(AfsFormatError):
    """El archivo termina antes de lo que declaran la cabecera o la TOC."""


class AfsUsageError(AfsError):
    """La petición no tiene sentido para este archivo (p. ej. un índice inexistente)."""
