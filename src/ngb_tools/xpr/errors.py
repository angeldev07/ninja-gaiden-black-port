"""Jerarquía de errores de la herramienta XPR.

La CLI traduce cada familia a un código de salida:

- 1, hallazgo de validación: un XPR0 roto (`XprFormatError`) o texturas que no se pueden
  exportar (`UnsupportedTextureError`).
- 2, error de uso o de entrada: el archivo no es lo que se pidió (`XprInputError`: no es
  un XPR0, o con `--kmd` no es un chunk con un slot XPR0), E/S (`OSError`) y salida
  dentro de un repo (`ngb_tools._paths.UnsafeOutputError`, común a las herramientas).
"""

from __future__ import annotations


class XprError(Exception):
    """Raíz de todos los errores de `ngb_tools.xpr`."""


class XprInputError(XprError):
    """La entrada no es lo pedido: no empieza por XPR0, o no es un chunk con XPR0."""


class XprFormatError(XprError):
    """Los bytes no forman un XPR0 válido (magia, tamaños o cabeceras de recurso)."""


class XprTruncatedError(XprFormatError):
    """El XPR0 o un recurso terminan más allá de los bytes disponibles."""


class UnsupportedTextureError(XprError):
    """Textura reconocida que no se sabe exportar (cubemap, volumen, formato o paleta)."""


class TextureTooLargeError(UnsupportedTextureError):
    """Dimensiones por encima del tope de píxeles: se rechaza antes de reservar memoria."""
