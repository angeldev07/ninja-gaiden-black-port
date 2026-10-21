"""Jerarquía de errores de la herramienta KMD.

La CLI traduce cada familia a un código de salida: un KMD que no se puede leer es un
hallazgo sobre los datos (1). Los fallos de quien invoca (2) no son de este paquete: E/S
(`OSError`) y salida dentro de un repo (`ngb_tools._paths.UnsafeOutputError`).

Lo que se puede leer pero incumple una invariante no es un error, sino una incidencia
(`ngb_tools.kmd.model.Issue`): la exportación sigue sin la parte afectada.
"""

from __future__ import annotations


class KmdError(Exception):
    """Raíz de todos los errores de `ngb_tools.kmd`."""


class KmdFormatError(KmdError):
    """Los bytes no forman un KMD legible (chunk, subchunks, XPR0 o descriptores)."""


class UnsupportedVertexFormatError(KmdError):
    """FVF que no se sabe decodificar (posición transformada, bits desconocidos...)."""


class PushBufferError(KmdFormatError):
    """Push buffer con comandos fuera del subconjunto admitido (BEGIN_END y ARRAY_ELEMENT16)."""
