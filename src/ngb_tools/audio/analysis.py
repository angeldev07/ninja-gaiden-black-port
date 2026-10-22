"""Validation without listening: signal statistics and a PNG with waveform and spectrogram.

- `signal_stats`: peak, clipped samples (at full scale), global and per-second RMS in
  dBFS. White noise gives a high, flat RMS; silence, -120 dBFS; music and voice, envelopes
  that rise and fall.
- `render_png`: on top, the waveform (mono mix, min/max and RMS per column, with time
  marks); below, the spectrogram (1024-point FFT with a Hann window, 80 dB range, linear
  frequency from 0 to Nyquist with a line every 5 kHz). Voice and music show harmonics as
  horizontal bands; white noise, a uniform carpet.
"""

from __future__ import annotations

import cmath
import itertools
import math
import operator
from array import array
from collections.abc import Sequence
from dataclasses import dataclass
from typing import Final, TypedDict

from ngb_tools.audio.png import encode_rgb

FULL_SCALE: Final = 32768.0
SILENCE_DBFS: Final = -120.0
FFT_SIZE: Final = 1024
DYNAMIC_RANGE_DB: Final = 80.0
WIDTH: Final = 1200
WAVE_HEIGHT: Final = 220
SPEC_HEIGHT: Final = 256
GAP: Final = 6
_TICK_STEPS: Final = (1, 2, 5, 10, 20, 30, 60, 120, 300)
_MAX_TICKS: Final = 30
_FREQ_LINE_HZ: Final = 5000
_BG: Final = (16, 16, 22)
_AXIS: Final = (70, 70, 85)
_TICK: Final = (40, 40, 52)
_PEAK: Final = (80, 150, 250)
_RMS: Final = (200, 225, 255)
_CLIP: Final = (255, 60, 60)
# Spectrogram colormap: black, violet, red, orange and light yellow.
_COLORMAP_STOPS: Final = (
    (0.0, (0, 0, 4)),
    (0.25, (60, 15, 110)),
    (0.5, (180, 40, 90)),
    (0.75, (250, 140, 30)),
    (1.0, (252, 255, 164)),
)


class StatsRecord(TypedDict):
    channels: int
    sample_rate: int
    frames: int
    duration_s: float
    peak: int
    clipped: int
    clipped_pct: float
    rms_dbfs: float
    rms_per_second_dbfs: list[float]


@dataclass(frozen=True, slots=True)
class SignalStats:
    channels: int
    sample_rate: int
    frames: int
    peak: int
    clipped: int
    rms_dbfs: float
    rms_per_second_dbfs: tuple[float, ...]

    @property
    def duration(self) -> float:
        return self.frames / self.sample_rate if self.sample_rate else 0.0

    def as_record(self) -> StatsRecord:
        total = self.frames * self.channels
        return StatsRecord(
            channels=self.channels,
            sample_rate=self.sample_rate,
            frames=self.frames,
            duration_s=round(self.duration, 3),
            peak=self.peak,
            clipped=self.clipped,
            clipped_pct=round(100.0 * self.clipped / total, 4) if total else 0.0,
            rms_dbfs=self.rms_dbfs,
            rms_per_second_dbfs=list(self.rms_per_second_dbfs),
        )


def dbfs(sum_squares: int, count: int) -> float:
    """RMS of `count` samples in dBFS (full scale = 32768), -120 if silent."""
    if count == 0 or sum_squares == 0:
        return SILENCE_DBFS
    rms = math.sqrt(sum_squares / count) / FULL_SCALE
    return round(max(SILENCE_DBFS, 20.0 * math.log10(rms)), 2)


def _sum_squares(samples: Sequence[int]) -> int:
    return sum(map(operator.mul, samples, samples))


def signal_stats(samples: array[int], channels: int, sample_rate: int) -> SignalStats:
    """Peak, clipping and RMS (global and per second, all channels together)."""
    second = sample_rate * channels
    per_second: list[float] = []
    total = 0
    for start in range(0, len(samples), max(1, second)):
        chunk = samples[start : start + second]
        squares = _sum_squares(chunk)
        total += squares
        per_second.append(dbfs(squares, len(chunk)))
    top, bottom = (max(samples), min(samples)) if samples else (0, 0)
    peak = max(top, -bottom)  # |−32768| = 32768
    clipped = samples.count(32767) + samples.count(-32768)
    return SignalStats(
        channels=channels,
        sample_rate=sample_rate,
        frames=len(samples) // channels if channels else 0,
        peak=peak,
        clipped=clipped,
        rms_dbfs=dbfs(total, len(samples)),
        rms_per_second_dbfs=tuple(per_second),
    )


# --- Image -------------------------------------------------------------------------------


def mono_mix(samples: array[int], channels: int) -> array[int]:
    """Average of the channels (integer division rounding down)."""
    if channels == 1:
        return samples
    lanes = [samples[ch::channels] for ch in range(channels)]
    return array("h", (sum(group) // channels for group in zip(*lanes, strict=True)))


def _colormap(value: float) -> tuple[int, int, int]:
    value = min(1.0, max(0.0, value))
    for (x0, c0), (x1, c1) in itertools.pairwise(_COLORMAP_STOPS):
        if value <= x1:
            t = (value - x0) / (x1 - x0)
            r, g, b = (round(a + (z - a) * t) for a, z in zip(c0, c1, strict=True))
            return r, g, b
    return _COLORMAP_STOPS[-1][1]


def _bit_reverse(n: int) -> list[int]:
    bits = n.bit_length() - 1
    return [int(f"{i:0{bits}b}"[::-1], 2) for i in range(n)]


class _Fft:
    """Iterative radix-2 FFT of fixed size (a power of 2), with precomputed tables."""

    def __init__(self, size: int) -> None:
        self.size = size
        self.order = _bit_reverse(size)
        self.twiddles = [cmath.exp(-2j * math.pi * k / size) for k in range(size // 2)]
        self.window = [0.5 - 0.5 * math.cos(2 * math.pi * i / (size - 1)) for i in range(size)]

    def magnitudes(self, frame: Sequence[int]) -> list[float]:
        """|X[k]| of the positive half of the spectrum of `frame` (with a Hann window)."""
        size = self.size
        window = self.window
        data: list[complex] = [complex(frame[i] * window[i]) for i in self.order]
        half = 1
        while half < size:
            step = size // (2 * half)
            tw = self.twiddles[::step][:half]
            for start in range(0, size, 2 * half):
                for k in range(half):
                    a = start + k
                    b = a + half
                    t = data[b] * tw[k]
                    data[b] = data[a] - t
                    data[a] += t
            half *= 2
        return [abs(x) for x in data[: size // 2]]


def _tick_step(duration: float) -> int:
    return next((s for s in _TICK_STEPS if duration / s <= _MAX_TICKS), _TICK_STEPS[-1])


def _waveform_rows(mono: array[int], sample_rate: int, width: int) -> list[bytearray]:
    height = WAVE_HEIGHT
    rows = [bytearray(bytes(_BG) * width) for _ in range(height)]
    mid = height // 2

    def paint(x: int, y0: int, y1: int, color: tuple[int, int, int]) -> None:
        for y in range(max(0, min(y0, y1)), min(height - 1, max(y0, y1)) + 1):
            rows[y][3 * x : 3 * x + 3] = bytes(color)

    n = len(mono)
    duration = n / sample_rate if sample_rate else 0.0
    if duration:
        step = _tick_step(duration)
        for t in range(0, int(duration) + 1, step):
            paint(min(width - 1, int(t / duration * width)), 0, height - 1, _TICK)
    for x in range(width):
        paint(x, mid, mid, _AXIS)

    def to_y(value: float) -> int:
        return mid - round(value / FULL_SCALE * (mid - 2))

    for x in range(width):
        segment = mono[x * n // width : max(x * n // width + 1, (x + 1) * n // width)]
        if not segment:
            continue
        lo, hi = min(segment), max(segment)
        rms = math.sqrt(_sum_squares(segment) / len(segment))
        clipped = hi >= 32767 or lo <= -32768
        paint(x, to_y(hi), to_y(lo), _CLIP if clipped else _PEAK)
        paint(x, to_y(rms), to_y(-rms), _RMS)
    return rows


def _spectrogram_rows(mono: array[int], sample_rate: int, width: int) -> list[bytearray]:
    fft = _Fft(FFT_SIZE)
    bins = FFT_SIZE // 2
    n = len(mono)
    columns: list[list[float]] = []
    for x in range(width):
        center = (2 * x + 1) * n // (2 * width)
        start = max(0, min(n - FFT_SIZE, center - FFT_SIZE // 2))
        frame = mono[start : start + FFT_SIZE]
        if len(frame) < FFT_SIZE:
            frame = frame + array("h", bytes(2 * (FFT_SIZE - len(frame))))
        mags = fft.magnitudes(frame)
        per_row = bins // SPEC_HEIGHT
        columns.append(
            [
                20.0 * math.log10(max(mags[r * per_row : (r + 1) * per_row]) + 1e-9)
                for r in range(SPEC_HEIGHT)
            ]
        )
    top = max((max(col) for col in columns), default=0.0)
    floor = top - DYNAMIC_RANGE_DB
    rows: list[bytearray] = []
    line_rows = set()
    if sample_rate:
        hz_per_row = sample_rate / 2 / SPEC_HEIGHT
        line_rows = {
            round(f / hz_per_row) for f in range(_FREQ_LINE_HZ, sample_rate // 2, _FREQ_LINE_HZ)
        }
    for r in range(SPEC_HEIGHT - 1, -1, -1):  # row 0 (lows) goes at the bottom
        row = bytearray()
        for col in columns:
            row += bytes(_colormap((col[r] - floor) / DYNAMIC_RANGE_DB))
        if r in line_rows:  # dotted reference line every 5 kHz
            for x in range(0, len(columns), 8):
                row[3 * x : 3 * x + 3] = bytes(_AXIS)
        rows.append(row)
    return rows


def render_png(samples: array[int], channels: int, sample_rate: int, width: int = WIDTH) -> bytes:
    """PNG of the waveform (top) and spectrogram (bottom) of the mono mix."""
    mono = mono_mix(samples, channels)
    if not mono:
        mono = array("h", bytes(2))
    rows = _waveform_rows(mono, sample_rate, width)
    rows += [bytearray(bytes(_AXIS) * width) for _ in range(GAP)]
    rows += _spectrogram_rows(mono, sample_rate, width)
    return encode_rgb(width, len(rows), rows)
