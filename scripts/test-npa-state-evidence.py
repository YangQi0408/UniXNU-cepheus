#!/usr/bin/env python3
"""Synthetic PE mapping tests; no private firmware or phone required."""
import importlib.util
from pathlib import Path
import struct
import unittest

SPEC = importlib.util.spec_from_file_location('npa_verify', Path(__file__).with_name('verify-npa-state-evidence.py'))
VERIFY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VERIFY)


class MappingTests(unittest.TestCase):
    def image(self):
        blob = bytearray(512)
        blob[:2] = b'MZ'
        struct.pack_into('<I', blob, 60, 64)
        blob[64:68] = b'PE\0\0'
        struct.pack_into('<HHIIIHH', blob, 68, 0xaa64, 1, 0, 0, 0, 32, 0)
        struct.pack_into('<H', blob, 88, 0x20b)
        struct.pack_into('<8sIIII', blob, 120, b'.text', 64, 0x1000, 32, 256)
        return blob

    def test_file_backed_span(self):
        blob = self.image()
        self.assertEqual(VERIFY.span(blob, VERIFY.pe_segments(blob), 0x1000, 32), bytes(32))

    def test_bss_crossing_and_invalid_span(self):
        blob = self.image()
        segments = VERIFY.pe_segments(blob)
        for address, size in ((0x1020, 1), (0x101f, 2), (0x1000, 0), (-1, 4), ((1 << 32) - 1, 4)):
            with self.assertRaises(ValueError):
                VERIFY.span(blob, segments, address, size)

    def test_duplicate_mapping(self):
        blob = self.image()
        segments = VERIFY.pe_segments(blob)
        with self.assertRaisesRegex(ValueError, 'uniquely'):
            VERIFY.span(blob, segments + segments, 0x1000, 4)

    def test_bad_header(self):
        for blob in (b'', bytes(512), self.image()[:130]):
            with self.assertRaises(ValueError):
                VERIFY.pe_segments(blob)

    def test_wrong_arch_or_base(self):
        for offset, fmt, value in ((68, '<H', 0x8664), (88, '<H', 0x10b), (112, '<Q', 0x1000)):
            blob = self.image()
            struct.pack_into(fmt, blob, offset, value)
            with self.assertRaises(ValueError):
                VERIFY.pe_segments(blob)

    def test_truncated_section_data(self):
        blob = self.image()
        struct.pack_into('<I', blob, 120 + 20, 510)
        with self.assertRaises(ValueError):
            VERIFY.pe_segments(blob)

    def test_incomplete_decode(self):
        with self.assertRaisesRegex(ValueError, 'decoding'):
            VERIFY.decode(bytes(3), 0x1000)

    def test_inventory_coverage_and_repeated_matches(self):
        def section(kind, data):
            raw = (len(data) + 4).to_bytes(3, 'little') + bytes([kind]) + data
            return raw + bytes(-len(raw) % 4)
        header = bytearray(24)
        header[18] = 7
        pe = b'/arc/client/test\0/arc/client/other\0'
        title = ('Synthetic\0').encode('utf-16le')
        inventory = VERIFY.npa_inventory({'plain': bytes(header) + section(0x10, pe) + section(0x15, title),
                                          'compressed': bytes(header) + section(1, bytes(8)),
                                          'other_type': bytes(24)})
        self.assertEqual(inventory['examined_plain_pe32_dxe_files'], 1)
        self.assertEqual(inventory['skipped_nonplain_dxe_files'], 1)
        self.assertEqual(inventory['matches'][0]['ui_names'], ['Synthetic'])
        self.assertEqual(len(inventory['matches'][0]['matches'][0]['file_offsets']), 2)


if __name__ == '__main__':
    unittest.main()
