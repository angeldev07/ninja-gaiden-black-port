"""WAV that `wave` reads back, valid PNG and signal statistics."""

from __future__ import annotations

import math
import struct
import tempfile
import unittest
import wave
import zlib
from array import array
from pathlib import Path

from ngb_tools.audio.analysis import _Fft, render_png, signal_stats
from ngb_tools.audio.png import encode_rgb
from ngb_tools.audio.wav import wav_bytes


class WavTests(unittest.TestCase):
    def test_wave_module_reads_back_the_file(self) -> None:
        samples = array("h", [0, 1, -1, 32767, -32768, 1234])
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "t.wav"
            path.write_bytes(wav_bytes(samples, 2, 22050))
            with wave.open(str(path), "rb") as reader:
                self.assertEqual(reader.getnchannels(), 2)
                self.assertEqual(reader.getframerate(), 22050)
                self.assertEqual(reader.getsampwidth(), 2)
                self.assertEqual(reader.getnframes(), 3)
                frames = reader.readframes(-1)
        self.assertEqual(frames, struct.pack("<6h", 0, 1, -1, 32767, -32768, 1234))

    def test_header_sizes(self) -> None:
        data = wav_bytes(array("h", [5, 6]), 1, 8000)
        self.assertEqual(data[:4] + data[8:16], b"RIFFWAVEfmt ")
        (riff_size,) = struct.unpack_from("<I", data, 4)
        self.assertEqual(riff_size, 36 + 4)
        self.assertEqual(struct.unpack_from("<I", data, 28)[0], 16000)  # bytes per second

    def test_samples_must_fill_whole_frames(self) -> None:
        with self.assertRaises(ValueError):
            wav_bytes(array("h", [1, 2, 3]), 2, 8000)


class PngTests(unittest.TestCase):
    def test_png_structure(self) -> None:
        png = encode_rgb(2, 1, [b"\xff\x00\x00\x00\xff\x00"])
        self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
        self.assertEqual(png[12:16], b"IHDR")
        self.assertEqual(struct.unpack(">II", png[16:24]), (2, 1))
        idat = png.index(b"IDAT")
        size = struct.unpack(">I", png[idat - 4 : idat])[0]
        self.assertEqual(
            zlib.decompress(png[idat + 4 : idat + 4 + size]), b"\x00\xff\x00\x00\x00\xff\x00"
        )

    def test_spectrum_peaks_at_the_tone_bin(self) -> None:
        # 1000 Hz at 8000 Hz with a 1024-point FFT: bin 1000/8000·1024 = 128.
        tone = [round(10000 * math.sin(2 * math.pi * 1000 * i / 8000)) for i in range(1024)]
        mags = _Fft(1024).magnitudes(tone)
        self.assertEqual(mags.index(max(mags)), 128)

    def test_render_has_waveform_and_spectrogram_rows(self) -> None:
        tone = array(
            "h", (round(8000 * math.sin(2 * math.pi * 440 * i / 8000)) for i in range(8000))
        )
        png = render_png(tone, 1, 8000, width=40)
        width, height = struct.unpack(">II", png[16:24])
        self.assertEqual((width, height), (40, 220 + 6 + 256))


class StatsTests(unittest.TestCase):
    def test_half_scale_square_is_minus_six_dbfs(self) -> None:
        # |16384| / 32768 = 0.5 -> 20·log10(0.5) = −6.02 dBFS in each second.
        samples = array("h", [16384, -16384] * 50)
        stats = signal_stats(samples, 1, 50)
        self.assertEqual(stats.rms_per_second_dbfs, (-6.02, -6.02))
        self.assertEqual((stats.peak, stats.clipped, stats.frames), (16384, 0, 100))

    def test_silence_and_clipping(self) -> None:
        stats = signal_stats(array("h", [0] * 10 + [32767, -32768]), 2, 4)
        self.assertEqual(stats.rms_per_second_dbfs[0], -120.0)
        self.assertEqual((stats.peak, stats.clipped, stats.frames), (32768, 2, 6))
        self.assertAlmostEqual(stats.duration, 1.5)


if __name__ == "__main__":
    unittest.main()
