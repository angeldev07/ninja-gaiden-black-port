"""Tests of header, TOC and attribute table parsing, and of streaming reads."""

from __future__ import annotations

import dataclasses
import io
import struct
import unittest

from ngb_tools.afs import (
    MAGIC_VARIANT,
    AfsArchive,
    AfsError,
    AfsFormatError,
    AfsTruncatedError,
    AttrEntry,
    AttrLocation,
    iter_entry_chunks,
    open_afs,
    read_entry_prefix,
)

from .synthetic import FILES, build_afs


def open_bytes(data: bytes, label: str = "test.afs") -> AfsArchive:
    return open_afs(io.BytesIO(data), label)


class ParseTests(unittest.TestCase):
    def test_names_after_toc(self) -> None:
        arc = open_bytes(build_afs(FILES, "after_toc"))
        self.assertEqual(arc.count, 3)
        self.assertEqual(arc.attr_ptr.location, AttrLocation.AFTER_TOC)
        self.assertEqual([e.name for e in arc.entries], ["menu.xpr", "song", "table.bin"])
        first = arc.entries[0].attr
        assert first is not None
        self.assertEqual(first.timestamp, "2004-02-10 23:56:02")
        self.assertEqual(arc.entries[1].size, len(FILES[1][1]))

    def test_names_before_first_data(self) -> None:
        arc = open_bytes(build_afs(FILES, "before_data"))
        self.assertEqual(arc.attr_ptr.location, AttrLocation.BEFORE_DATA)
        self.assertEqual(arc.entries[2].name, "table.bin")

    def test_no_names(self) -> None:
        arc = open_bytes(build_afs(FILES, "none"))
        self.assertEqual(arc.attr_ptr.location, AttrLocation.NONE)
        self.assertFalse(arc.has_names)
        self.assertTrue(all(e.name is None for e in arc.entries))

    def test_bad_magic(self) -> None:
        with self.assertRaises(AfsError):
            open_bytes(b"NOPE" + bytes(12))

    def test_variant_magic_is_accepted(self) -> None:
        data = bytearray(build_afs(FILES))
        data[3] = 0x20
        self.assertEqual(open_bytes(bytes(data)).magic, MAGIC_VARIANT)

    def test_truncated_header_is_a_format_error(self) -> None:
        with self.assertRaisesRegex(AfsFormatError, "truncated header"):
            open_bytes(b"AFS\x00\x01")

    def test_toc_that_does_not_fit_is_a_format_error(self) -> None:
        data = struct.pack("<4sI", b"AFS\x00", 1000) + bytes(16)
        with self.assertRaisesRegex(AfsFormatError, "does not fit"):
            open_bytes(data)

    def test_attr_pointer_outside_file_means_no_names(self) -> None:
        data = bytearray(build_afs(FILES, "after_toc"))
        struct.pack_into("<II", data, 8 + 8 * len(FILES), len(data), 3 * 0x30)
        self.assertEqual(open_bytes(bytes(data)).attr_ptr.location, AttrLocation.NONE)

    def test_nested_archive_offsets_are_absolute(self) -> None:
        inner = build_afs(FILES)
        padding = bytes(0x1000)
        arc = open_afs(io.BytesIO(padding + inner), "outer#0", base=len(padding), limit=len(inner))
        self.assertEqual(arc.entries[0].rel_offset, 0x800)
        self.assertEqual(arc.entries[0].offset, len(padding) + 0x800)
        self.assertEqual(arc.entries[0].name, "menu.xpr")

    def test_invalid_date_has_no_timestamp(self) -> None:
        attr = AttrEntry(name="x", date=(2004, 13, 40, 0, 0, 0), custom=0)
        self.assertIsNone(attr.timestamp)

    def test_structures_are_immutable(self) -> None:
        arc = open_bytes(build_afs(FILES))
        with self.assertRaises(dataclasses.FrozenInstanceError):
            arc.entries[0].size = 0  # type: ignore[misc]


class StreamingTests(unittest.TestCase):
    def test_chunks_reassemble_the_entry(self) -> None:
        data = build_afs(FILES)
        source = io.BytesIO(data)
        entry = open_afs(source, "test.afs").entries[2]
        chunks = list(iter_entry_chunks(source, entry, chunk_size=1000))
        self.assertTrue(all(len(c) <= 1000 for c in chunks))
        self.assertEqual(b"".join(chunks), FILES[2][1])

    def test_truncated_entry_raises(self) -> None:
        data = build_afs(FILES, "none")
        arc = open_bytes(data)
        cut = io.BytesIO(data[: arc.entries[2].rel_offset + 10])
        with self.assertRaises(AfsTruncatedError):
            list(iter_entry_chunks(cut, arc.entries[2]))

    def test_prefix_is_clamped_to_entry_size(self) -> None:
        data = build_afs(FILES)
        source = io.BytesIO(data)
        entry = open_afs(source, "test.afs").entries[0]
        self.assertEqual(read_entry_prefix(source, entry, 1 << 20), FILES[0][1])


if __name__ == "__main__":
    unittest.main()
