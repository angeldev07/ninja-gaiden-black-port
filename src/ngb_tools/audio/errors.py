"""Error hierarchy of the audio tool.

The CLI maps each family to an exit code: format errors are a finding about the data (1)
and usage errors are a failure of the caller (2). `UnsafeOutputError` (output inside a
repo) comes from `ngb_tools._paths` and the CLI treats it as usage (2).
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
    """Root of the errors specific to `ngb_tools.audio`."""


class AudioFormatError(AudioError):
    """The bytes do not form a valid audio stream (impossible magic, header or sizes)."""


class AudioTruncatedError(AudioFormatError):
    """The stream ends before what its header declares."""


class UnsupportedCodecError(AudioFormatError):
    """Recognized format, but with a codec or variant that is not decoded."""


class EncryptedAdxError(UnsupportedCodecError):
    """Encrypted ADX (byte 0x13 = 0x08 or 0x09): reported and skipped, never decrypted."""


class AudioUsageError(AudioError):
    """The request makes no sense for this file (e.g. a nonexistent index)."""
