"""Jerarquía de errores de la herramienta de audio.

La CLI traduce cada familia a un código de salida: los errores de formato son un hallazgo
sobre los datos (1) y los de uso son un fallo de quien invoca (2). `UnsafeOutputError`
(salida dentro de un repo) viene de `ngb_tools._paths` y la CLI la trata como uso (2).
"""

from __future__ import annotations

from ngb_tools._paths import UnsafeOutputError

__all__ = [
    "AudioError",
    "AudioFormatError",
    "AudioTruncatedError",
    "AudioUsageError",
    "EncryptedAdxError",
    "UnsafeOutputError",
    "UnsupportedCodecError",
]


class AudioError(Exception):
    """Raíz de los errores propios de `ngb_tools.audio`."""


class AudioFormatError(AudioError):
    """Los bytes no forman un flujo de audio válido (magia, cabecera o tamaños imposibles)."""


class AudioTruncatedError(AudioFormatError):
    """El flujo termina antes de lo que declara su cabecera."""


class UnsupportedCodecError(AudioFormatError):
    """Formato reconocido, pero con un códec o variante que no se decodifica."""


class EncryptedAdxError(UnsupportedCodecError):
    """ADX cifrado (byte 0x13 = 0x08 o 0x09): se informa y se salta, nunca se descifra."""


class AudioUsageError(AudioError):
    """La petición no tiene sentido para este archivo (p. ej. un índice inexistente)."""
