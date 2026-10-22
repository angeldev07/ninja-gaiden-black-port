"""Ninja Gaiden Black audio to PCM16 WAV: ADX (CRI), Xbox ADPCM (`.wva`) and XACT (`.xwb`).

Modules, bottom up:

- `pcm`, `wav`, `png`: channel interleaving, WAV writer and minimal PNG writer.
- `adx`: header, predictor coefficients and CRI ADPCM decoder.
- `xbox_adpcm`: decoder for the XDK's IMA variant (WAVE_FORMAT 0x0069).
- `wva`: Team Ninja effect banks (INFERRED format).
- `xwb`: XACT wave banks, versions 2 and 3.
- `analysis`: validation without listening (RMS per second, clipping, waveform and
  spectrogram as PNG).
- `extract`: batch extraction with a manifest, always outside any git repo.
- `cli`: `python -P -s -m ngb_tools.audio {adx,wva,xwb,analyze}`.
"""

from __future__ import annotations

from ngb_tools.audio.adx import AdxHeader, AdxLoop, compute_coefficients
from ngb_tools.audio.errors import (
    AudioError,
    AudioFormatError,
    AudioTruncatedError,
    AudioUsageError,
    EncryptedAdxError,
    UnsupportedCodecError,
)

__all__ = [
    "AdxHeader",
    "AdxLoop",
    "AudioError",
    "AudioFormatError",
    "AudioTruncatedError",
    "AudioUsageError",
    "EncryptedAdxError",
    "UnsupportedCodecError",
    "compute_coefficients",
]
