"""ADX: coefficients, hand-computed blocks, header, loops and encryption."""

from __future__ import annotations

import unittest

from ngb_tools.audio import adx
from ngb_tools.audio.errors import (
    AudioFormatError,
    AudioTruncatedError,
    EncryptedAdxError,
    UnsupportedCodecError,
)

from .synthetic import adx_frame, adx_header


def decode_one(frame: bytes, coefs: tuple[int, int], *, split_shift: bool = True) -> list[int]:
    return adx.decode_channel(frame, 0, 18, 1, coefs, split_shift=split_shift).tolist()


class CoefficientTests(unittest.TestCase):
    def test_cutoff_500_at_44100(self) -> None:
        # By hand: cos(2π·500/44100) = 0.9974631, a = 0.4167505, b = 0.4142136,
        # c = (a − √(a² − b²))/b = 0.8952898; 2c·4096 = 7334.21 and −c²·4096 = −3283.12.
        self.assertEqual(adx.compute_coefficients(500, 44100), (7334, -3283))

    def test_cutoff_500_at_22050_truncates_towards_zero(self) -> None:
        # c = 0.8019254: 2c·4096 = 6569.37 and −c²·4096 = −2634.07 (truncated toward zero).
        self.assertEqual(adx.compute_coefficients(500, 22050), (6569, -2634))

    def test_impossible_cutoff_is_rejected(self) -> None:
        with self.assertRaises(AudioFormatError):
            adx.compute_coefficients(30000, 44100)


class FrameDecodeTests(unittest.TestCase):
    def test_without_prediction_each_sample_is_nibble_times_scale_plus_one(self) -> None:
        # Scale 1 -> 2. Bytes 0x12 0x7F 0x88 0xF0: nibbles 1, 2, 7, −1, −8, −8, −1, 0.
        frame = adx_frame(1, [1, 2, 7, -1, -8, -8, -1, 0])
        self.assertEqual(frame[2:6], b"\x12\x7f\x88\xf0")
        expected = [2, 4, 14, -2, -16, -16, -2, 0] + [0] * 24
        self.assertEqual(decode_one(frame, (0, 0)), expected)

    def test_coef1_of_one_integrates(self) -> None:
        # coef1 = 4096 (1.0): each sample is the previous one plus nibble·2.
        frame = adx_frame(1, [1, 2, 7, -1, -8, -8, -1, 0])
        expected = [2, 6, 20, 18, 2, -14, -16, -16] + [-16] * 24
        self.assertEqual(decode_one(frame, (4096, 0)), expected)

    def test_real_coefficients_with_version_3_shifts(self) -> None:
        # Scale 16 -> 17, first nibble 7, coefs (7334, −3283), each product >> 12:
        # s0 = 119; s1 = 872746>>12 = 213; s2 = (1562142>>12) + (−390677>>12) = 381 − 96
        # = 285; s3 = (2090190>>12) + (−699279>>12) = 510 − 171 = 339.
        samples = decode_one(adx_frame(16, [7]), (7334, -3283))
        self.assertEqual(samples[:4], [119, 213, 285, 339])

    def test_real_coefficients_with_version_4_shift_of_the_sum(self) -> None:
        # Same, but shifting the sum: s2 = 1171465>>12 = 286; s3 = 1398245>>12 = 341.
        samples = decode_one(adx_frame(16, [7]), (7334, -3283), split_shift=False)
        self.assertEqual(samples[:4], [119, 213, 286, 341])

    def test_output_saturates_to_16_bits(self) -> None:
        # Scale 0xFFFF -> 65536: 7·65536 and −8·65536 saturate.
        self.assertEqual(decode_one(adx_frame(0xFFFF, [7, -8]), (0, 0))[:2], [32767, -32768])


class HeaderTests(unittest.TestCase):
    def test_fields_are_read_big_endian(self) -> None:
        header = adx.parse_header(adx_header(channels=2, rate=22050, total=1000, cutoff=400))
        self.assertEqual(header.data_offset, 0x24)
        self.assertEqual((header.channels, header.sample_rate), (2, 22050))
        self.assertEqual((header.total_samples, header.cutoff), (1000, 400))
        self.assertEqual((header.version, header.flags, header.loop), (3, 0, None))

    def test_version_3_loop_is_exported(self) -> None:
        header = adx.parse_header(adx_header(loop=(4410, 88200)))
        self.assertEqual(header.loop, adx.AdxLoop(4410, 0x100, 88200, 0x200))

    def test_missing_copyright_is_a_format_error(self) -> None:
        raw = bytearray(adx_header())
        raw[0x1E:0x24] = b"(c)XXX"
        with self.assertRaises(AudioFormatError):
            adx.parse_header(bytes(raw))

    def test_bad_magic_is_a_format_error(self) -> None:
        with self.assertRaises(AudioFormatError):
            adx.parse_header(b"\x00\x00" + adx_header()[2:])

    def test_encryption_flags_are_detected_and_not_decoded(self) -> None:
        for flags in (0x08, 0x09):
            raw = adx_header(version=4, flags=flags) + adx_frame(0, [])
            self.assertTrue(adx.parse_header(raw).encrypted)
            with self.assertRaises(EncryptedAdxError):
                adx.decode(raw)

    def test_other_encodings_are_unsupported(self) -> None:
        raw = adx_header(encoding=4) + adx_frame(0, [])
        with self.assertRaises(UnsupportedCodecError):
            adx.decode(raw)


class StreamDecodeTests(unittest.TestCase):
    def test_stereo_frames_interleave_and_trim_to_total(self) -> None:
        # Blocks c0, c1, c0, c1. Scale 3 -> 4: c0 starts at 2·4 = 8 and c1 at −3·4 = −12.
        raw = adx_header(channels=2, total=40) + b"".join(
            (adx_frame(3, [2]), adx_frame(3, [-3]), adx_frame(0, []), adx_frame(0, []))
        )
        header, pcm = adx.decode(raw)
        self.assertEqual(header.frames, 2)
        self.assertEqual(len(pcm), 80)
        self.assertEqual(pcm[:2].tolist(), [8, -12])

    def test_truncated_audio_is_reported(self) -> None:
        with self.assertRaises(AudioTruncatedError):
            adx.decode(adx_header(total=64) + adx_frame(0, []))

    def test_looks_like_adx(self) -> None:
        self.assertTrue(adx.looks_like_adx(adx_header()))
        self.assertFalse(adx.looks_like_adx(b"RIFF" + bytes(60)))


if __name__ == "__main__":
    unittest.main()
