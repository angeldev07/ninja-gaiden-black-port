"""Audio de Ninja Gaiden Black a WAV PCM16: ADX (CRI), Xbox ADPCM (`.wva`) y XACT (`.xwb`).

Módulos, de abajo arriba:

- `pcm`, `wav`, `png`: intercalado de canales, escritor WAV y escritor PNG mínimo.
- `adx`: cabecera, coeficientes del predictor y decodificador ADPCM de CRI.
- `xbox_adpcm`: decodificador de la variante IMA del XDK (WAVE_FORMAT 0x0069).
- `wva`: bancos de efectos de Team Ninja (formato INFERRED).
- `xwb`: wave banks de XACT de las versiones 2 y 3.
- `analysis`: validación sin oír (RMS por segundo, saturación, forma de onda y
  espectrograma en PNG).
- `extract`: extracción por lotes con manifiesto, siempre fuera de cualquier repo git.
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
