"""Estructuras del XBE parseadas desde bytes, sin resolver direcciones virtuales."""

from __future__ import annotations

import struct
import unittest

from ngb_tools.xbe.errors import BadMagicError, TruncatedDataError
from ngb_tools.xbe.structs import (
    SECTION_FLAGS,
    Certificate,
    CodeViewRecord,
    ImageHeader,
    LibraryVersion,
    TlsDirectory,
    decode_flags,
    format_title_id,
)

from .synthetic import (
    BASE,
    CERT_OFF,
    LIBRARY_OFF,
    TITLE_ID,
    build_synthetic_xbe,
    with_u32,
)


class ImageHeaderTests(unittest.TestCase):
    def test_header_fields(self) -> None:
        header = ImageHeader.parse(build_synthetic_xbe())
        self.assertEqual(header.base_address, BASE)
        self.assertEqual(header.section_count, 1)
        self.assertEqual(header.certificate_addr, BASE + CERT_OFF)
        self.assertEqual(header.init_flags_decoded, ["MOUNT_UTILITY_DRIVE", "LIMIT_64MB"])
        self.assertEqual(header.timedate_iso, "2005-09-02T06:30:06+00:00")

    def test_bad_magic_raises(self) -> None:
        data = b"MZ\0\0" + build_synthetic_xbe()[4:]
        with self.assertRaises(BadMagicError):
            ImageHeader.parse(data)

    def test_truncated_raises(self) -> None:
        with self.assertRaises(TruncatedDataError):
            ImageHeader.parse(b"XBEH" + b"\0" * 0x20)

    def test_old_xdk_header_has_no_extension_fields(self) -> None:
        # Cabecera de 0x178 bytes (XDK < 5028): ni Unknown1 ni puntero CodeView.
        header = ImageHeader.parse(with_u32(build_synthetic_xbe(), 0x110, 0x178))
        self.assertIsNone(header.unknown1)
        self.assertIsNone(header.debug_info_addr)

    def test_extension_fields_follow_header_size(self) -> None:
        # 0x180 bytes: hay Unknown1 (8 bytes) pero aún no el campo 0x180.
        data = with_u32(build_synthetic_xbe(), 0x110, 0x180)
        header = ImageHeader.parse(with_u32(data, 0x178, 0xDDCCBBAA))
        self.assertEqual(header.unknown1, bytes.fromhex("aabbccdd00000000"))
        self.assertIsNone(header.debug_info_addr)


class CertificateTests(unittest.TestCase):
    def test_certificate(self) -> None:
        cert = Certificate.parse(build_synthetic_xbe(), CERT_OFF)
        self.assertEqual(cert.title_id, TITLE_ID)
        self.assertEqual(cert.title_id_str, "TC-013")
        self.assertEqual(cert.title_name, "NINJA GAIDEN Test")
        self.assertEqual(cert.alternate_title_ids, (0x54430003,))
        self.assertEqual(cert.allowed_media_decoded, ["DVD_X2", "MEDIA_BOARD"])
        self.assertEqual(cert.game_region_decoded, ["REST_OF_WORLD"])
        self.assertEqual((cert.disk_number, cert.version), (1, 3))
        self.assertEqual(cert.security_flags, 0)

    def test_short_certificate_omits_extended_fields(self) -> None:
        data = with_u32(build_synthetic_xbe(), CERT_OFF, 0x1D0)
        cert = Certificate.parse(data, CERT_OFF)
        self.assertIsNone(cert.original_certificate_size)
        self.assertIsNone(cert.online_service)
        self.assertIsNone(cert.online_service_text)
        self.assertIsNone(cert.security_flags)

    def test_online_service_is_a_reversed_fourcc(self) -> None:
        data = bytearray(build_synthetic_xbe())
        data[CERT_OFF + 0x1D4 : CERT_OFF + 0x1D8] = b"ALBX"
        cert = Certificate.parse(bytes(data), CERT_OFF)
        self.assertEqual(cert.online_service, 0x58424C41)
        self.assertEqual(cert.online_service_text, "XBLA")

    def test_title_id_with_unprintable_publisher_uses_hex(self) -> None:
        self.assertEqual(format_title_id(0x00010002), "0001-002")
        self.assertEqual(format_title_id(0x4D530802), "MS-2050")


class SmallStructTests(unittest.TestCase):
    def test_library_version(self) -> None:
        lib = LibraryVersion.parse(build_synthetic_xbe(), LIBRARY_OFF)
        self.assertEqual((lib.name, lib.version), ("XAPILIB", "1.0.5849"))
        self.assertEqual((lib.qfe, lib.approved_text, lib.debug_build), (1, "sí", False))

    def test_library_flags_pack_qfe_approval_and_debug(self) -> None:
        raw = b"D3D8\0\0\0\0" + struct.pack("<4H", 1, 0, 5849, 0x8000 | (3 << 13) | 5)
        lib = LibraryVersion.parse(raw, 0)
        self.assertEqual((lib.qfe, lib.approved, lib.approved_text), (5, 3, "desconocido"))
        self.assertTrue(lib.debug_build)

    def test_tls_directory_field_order(self) -> None:
        tls = TlsDirectory.parse(struct.pack("<6I", 1, 2, 3, 4, 5, 6), 0)
        self.assertEqual(
            (tls.raw_data_start_addr, tls.raw_data_end_addr, tls.index_addr),
            (1, 2, 3),
        )
        self.assertEqual(
            (tls.callbacks_addr, tls.size_of_zero_fill, tls.characteristics), (4, 5, 6)
        )

    def test_codeview_other_than_rsds_is_ignored(self) -> None:
        self.assertIsNone(CodeViewRecord.parse(b"NB10" + b"\0" * 40, 0))
        self.assertIsNone(CodeViewRecord.parse(b"RSDS" + b"\0" * 8, 0))

    def test_unknown_flag_bits_are_reported_in_hex(self) -> None:
        self.assertEqual(
            decode_flags(0x46, SECTION_FLAGS), ["PRELOAD", "EXECUTABLE", "UNKNOWN(0x40)"]
        )


if __name__ == "__main__":
    unittest.main()
