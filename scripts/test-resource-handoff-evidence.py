#!/usr/bin/env python3
"""Synthetic DXE identity, event GUID and instruction guards, no device."""
import hashlib
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch
import uuid

SPEC = importlib.util.spec_from_file_location(
    'handoff_verify', Path(__file__).with_name('verify-resource-handoff-evidence.py'))
VERIFY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VERIFY)
SPEC = importlib.util.spec_from_file_location(
    'crypto_tests', Path(__file__).with_name('test-crypto-icb-evidence.py'))
FIXTURE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(FIXTURE)


class CoreTests(unittest.TestCase):
    def fixture(self):
        blob, segments, reads, put = FIXTURE.fixture()
        reads.clear()
        for address, size in VERIFY.CORE_RANGES:
            put(address, bytes(size))
        before = uuid.UUID('8be0e274-3970-4b44-80c5-1ab9502f3bfc').bytes_le
        failed = uuid.UUID('4f6c5507-232f-4787-b95e-72f862490cb1').bytes_le
        put(0x18708, before + failed)
        put(0x18778, uuid.UUID('27abf055-b1b8-4c26-8048-748f37baa2df').bytes_le)
        return blob, segments, reads, put

    def test_core_order_is_not_live_resource_ownership(self):
        blob, segments, reads, _ = self.fixture()
        with patch.object(VERIFY.ICB, 'instruction', side_effect=FIXTURE.AbiTests().capture):
            result = VERIFY.core_order(blob, segments, reads)
        self.assertIn('Highest pending TPL', result['order'])
        self.assertIn('reverse registration', result['order'])
        self.assertIn('only on successful validation', result['retry'])
        self.assertIn('No last-writer snapshot', result['limitations'][-1])

    def test_guid_mutations(self):
        for address in (0x18708, 0x18718, 0x18778):
            blob, segments, reads, put = self.fixture()
            put(address, bytes(16))
            with self.assertRaisesRegex(ValueError, 'GUID mismatch'):
                VERIFY.core_order(blob, segments, reads)

    def test_every_bounded_read_is_required(self):
        blob, segments, reads, _ = self.fixture()
        for missing in VERIFY.CORE_RANGES:
            changed = reads.copy()
            del changed[missing]
            with self.assertRaisesRegex(ValueError, 'coverage'):
                VERIFY.core_order(blob, segments, changed)

    def test_changed_instruction_rejected(self):
        blob, segments, reads, _ = self.fixture()
        with patch.object(VERIFY.ICB, 'instruction', side_effect=ValueError('changed event opcode')):
            with self.assertRaisesRegex(ValueError, 'changed event opcode'):
                VERIFY.core_order(blob, segments, reads)

    def test_actual_synthetic_bytes_are_checked_without_mock(self):
        blob, segments, reads, _ = self.fixture()
        with self.assertRaisesRegex(ValueError, 'unexpected instruction'):
            VERIFY.core_order(blob, segments, reads)

    def test_core_identity_and_embedded_pe_guards(self):
        original = b'synthetic PE'
        section = (len(original) + 4).to_bytes(3, 'little') + b'\x10' + original
        section += bytes(-len(section) % 4)
        inner = {VERIFY.CORE_GUID: bytes(24) + section}
        with patch.object(VERIFY, 'CORE_SHA', hashlib.sha256(original).hexdigest()):
            self.assertTrue(VERIFY.bind_core(inner, original)['embedded_bytes_equal'])
            with self.assertRaisesRegex(ValueError, 'missing'):
                VERIFY.bind_core({}, original)
            with self.assertRaisesRegex(ValueError, 'embedded'):
                VERIFY.bind_core({VERIFY.CORE_GUID: bytes(24) + section * 2}, original)
            with self.assertRaisesRegex(ValueError, 'identity'):
                VERIFY.bind_core(inner, original + b'changed')


if __name__ == '__main__':
    unittest.main()
