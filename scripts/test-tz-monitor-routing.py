#!/usr/bin/env python3
"""Private fixed-evidence rejection tests; no firmware execution or device."""
import copy
import importlib.util
import json
from pathlib import Path
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location(
    'monitor_verify', Path(__file__).with_name('verify-tz-monitor-routing.py'))
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)
ROOT = Path(__file__).resolve().parents[2]
TZ = ROOT / 'logs/recovery-tz-acquisition-20261107-clean/tz-partition.img'
BUNDLE = ROOT / 'logs/pas13-instruction-replay-20261110/tz-monitor-routing-20261112-v1-evidence.json'


class MonitorEvidenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.blob = TZ.read_bytes()
        cls.bundle = json.loads(BUNDLE.read_bytes())

    def test_original(self):
        result = MODULE.verify(self.blob, self.bundle)
        self.assertEqual(result['status'], 'PASS_STATIC_BYTES_ONLY')
        self.assertEqual(result['interrupt_token_envelope_offset'], '0x30')
        self.assertFalse(result['firmware_execution'])

    def test_foreign_image(self):
        blob = bytearray(self.blob)
        blob[-1] ^= 1
        with self.assertRaisesRegex(ValueError, 'identity'):
            MODULE.verify(blob, self.bundle)

    def mutated(self, change):
        bundle = copy.deepcopy(self.bundle)
        record = next(r for r in bundle['records'] if r['operation'] == 'read_bytes'
                      and r['normalized_result']['address'] == '0x1c03ba58')
        change(record)
        return bundle

    def test_foreign_evidence(self):
        bundle = self.mutated(lambda r: r['subject']['digest'].update(sha256='0' * 64))
        with self.assertRaisesRegex(ValueError, 'binding'):
            MODULE.verify(self.blob, bundle)

    def test_truncated_evidence(self):
        bundle = self.mutated(lambda r: r['normalized_result'].update(complete=False))
        with self.assertRaisesRegex(ValueError, 'binding'):
            MODULE.verify(self.blob, bundle)

    def test_changed_bytes(self):
        bundle = self.mutated(lambda r: r['normalized_result'].update(bytes_hex='00' * 60))
        with self.assertRaisesRegex(ValueError, 'binding'):
            MODULE.verify(self.blob, bundle)

    def test_duplicate_selection(self):
        bundle = copy.deepcopy(self.bundle)
        busy = next(r for r in bundle['records'] if r['operation'] == 'read_bytes'
                    and r['normalized_result']['address'] == '0x1c03ba58')
        other = next(i for i, r in enumerate(bundle['records']) if r['operation'] == 'xrefs')
        bundle['records'][other] = copy.deepcopy(busy)
        with self.assertRaisesRegex(ValueError, 'ambiguous'):
            MODULE.verify(self.blob, bundle)

    def test_llvm_failure_is_not_pass(self):
        with patch.object(MODULE.VERIFY, 'llvm_check', side_effect=ValueError('LLVM rejected')):
            with self.assertRaisesRegex(ValueError, 'LLVM rejected'):
                MODULE.verify(self.blob, self.bundle)


if __name__ == '__main__':
    unittest.main()
