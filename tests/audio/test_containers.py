"""Synthetic `.wva` banks and XWB wave banks."""

from __future__ import annotations

import struct
import unittest

from ngb_tools.audio import wva, xwb
from ngb_tools.audio.errors import AudioFormatError, AudioTruncatedError, UnsupportedCodecError

from .synthetic import build_wva, build_xwb, wva_record, xbox_block

ZERO = bytes(4)


def silent_block(sample: int) -> bytes:
    return xbox_block([(sample, 0)], [[ZERO] * 8])


class WvaTests(unittest.TestCase):
    def setUp(self) -> None:
        self.long = wva_record(silent_block(7) * 2, rate=32000)
        self.short = wva_record(silent_block(-3), long_params=False)
        self.bank = build_wva([(self.long, True), None, (self.short, False)])

    def test_records_skip_empty_slots_and_follow_the_params_bit(self) -> None:
        records = wva.parse_wva(self.bank)
        self.assertEqual([r.slot for r in records], [0, 2])
        first, second = records
        table = 4 + 4 * 3
        self.assertEqual((first.offset, first.long_params), (table, True))
        self.assertEqual(first.data_offset, table + 4 + 20 + 24)
        self.assertEqual((first.data_size, first.sample_rate, first.samples), (72, 32000, 128))
        self.assertEqual(len(first.params), 24)
        self.assertEqual(second.offset, table + len(self.long))
        self.assertEqual(second.data_offset, second.offset + 4 + 20 + 8)
        self.assertEqual(wva.record_data(self.bank, second), silent_block(-3))

    def test_flags_come_from_the_top_byte_of_the_size_word(self) -> None:
        record = bytearray(self.long)
        record[3] = 0x04
        (parsed,) = wva.parse_wva(build_wva([(bytes(record), True)]))
        self.assertEqual((parsed.flags, parsed.data_size), (0x04, 72))

    def test_other_codecs_are_unsupported(self) -> None:
        (parsed,) = wva.parse_wva(build_wva([(wva_record(bytes(36), tag=1), True)]))
        with self.assertRaises(UnsupportedCodecError):
            wva.check_xbox_adpcm(parsed)

    def test_data_past_the_end_is_truncated(self) -> None:
        with self.assertRaises(AudioTruncatedError):
            wva.parse_wva(self.bank[:-10])

    def test_impossible_table_is_a_format_error(self) -> None:
        with self.assertRaises(AudioFormatError):
            wva.parse_wva(struct.pack("<I", 1_000_000))


class XwbTests(unittest.TestCase):
    def setUp(self) -> None:
        self.pcm = struct.pack("<4h", 1, -1, 300, -300)
        self.adpcm = silent_block(42)
        self.bank = build_xwb(
            [(0, 2, 48000, 16, self.pcm), (1, 1, 22050, 4, self.adpcm), (2, 1, 44100, 16, b"x")]
        )

    def test_format_dword_and_offsets(self) -> None:
        bank = xwb.parse_xwb(self.bank)
        self.assertEqual((bank.version, bank.name, len(bank.entries)), (3, "Bank", 3))
        pcm, adpcm, wma = bank.entries
        self.assertEqual((pcm.codec, pcm.channels, pcm.sample_rate, pcm.bits), (0, 2, 48000, 16))
        self.assertEqual(
            (adpcm.codec_name, adpcm.channels, adpcm.sample_rate), ("xbox_adpcm", 1, 22050)
        )
        self.assertEqual(adpcm.offset, pcm.offset + len(self.pcm))
        self.assertEqual(wma.codec_name, "wma")

    def test_entries_decode(self) -> None:
        bank = xwb.parse_xwb(self.bank)
        self.assertEqual(xwb.decode_entry(self.bank, bank.entries[0]).tolist(), [1, -1, 300, -300])
        self.assertEqual(xwb.decode_entry(self.bank, bank.entries[1]).tolist(), [42] * 64)
        with self.assertRaises(UnsupportedCodecError):
            xwb.decode_entry(self.bank, bank.entries[2])

    def test_eight_bit_pcm_is_widened(self) -> None:
        data = build_xwb([(0, 1, 8000, 8, bytes([0, 128, 255]))])
        (entry,) = xwb.parse_xwb(data).entries
        self.assertEqual(xwb.decode_entry(data, entry).tolist(), [-32768, 0, 32512])

    def test_bad_magic_and_version(self) -> None:
        with self.assertRaises(AudioFormatError):
            xwb.parse_xwb(b"DNBW" + self.bank[4:])
        with self.assertRaises(UnsupportedCodecError):
            xwb.parse_xwb(self.bank[:4] + struct.pack("<I", 46) + self.bank[8:])


if __name__ == "__main__":
    unittest.main()
