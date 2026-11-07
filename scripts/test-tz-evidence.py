#!/usr/bin/env python3
"""RAM-only ELF mapping tests; no proprietary firmware required."""
import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location('tz_verify', Path(__file__).with_name('verify-tz-evidence.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class MappingTests(unittest.TestCase):
    def image(self):
        data = bytearray(256)
        data[:6] = b'\x7fELF\x02\x01'
        struct.pack_into('<H', data, 18, 183)
        struct.pack_into('<Q', data, 32, 64)
        struct.pack_into('<HH', data, 54, 56, 1)
        struct.pack_into('<IIQQQQQQ', data, 64, 1, 5, 128, 0x1000, 0x1000, 64, 128, 4)
        return data

    def test_valid_file_range(self):
        self.assertEqual(module.map_range(module.load_segments(self.image()), 0x103c, 4), (188, 5))

    def test_bss_is_not_file_data(self):
        with self.assertRaises(ValueError):
            module.map_range(module.load_segments(self.image()), 0x1040, 4)

    def test_crossing_is_rejected(self):
        with self.assertRaises(ValueError):
            module.map_range(module.load_segments(self.image()), 0x103e, 4)

    def test_ambiguous_loads(self):
        segments = module.load_segments(self.image())
        with self.assertRaises(ValueError):
            module.map_range(segments + segments, 0x1000, 4)

    def test_bad_header_bounds(self):
        data = self.image()
        struct.pack_into('<H', data, 56, 128)
        with self.assertRaises(ValueError):
            module.load_segments(data)

    def test_bad_segment_bounds(self):
        data = self.image()
        struct.pack_into('<Q', data, 64 + 8, 250)
        with self.assertRaises(ValueError):
            module.load_segments(data)

    def test_wrong_architecture(self):
        data = self.image()
        struct.pack_into('<H', data, 18, 62)
        with self.assertRaises(ValueError):
            module.load_segments(data)

    def test_empty_range(self):
        with self.assertRaises(ValueError):
            module.map_range(module.load_segments(self.image()), 0x1000, 0)


if __name__ == '__main__':
    unittest.main()
