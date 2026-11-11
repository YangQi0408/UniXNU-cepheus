#!/usr/bin/env python3
"""Private fixed-artifact evidence rejection tests; no device or execution."""
import copy
import importlib.util
import json
from pathlib import Path
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location(
    'smemdxe_verify', Path(__file__).with_name('verify-smemdxe-evidence.py'))
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)
ROOT = Path(__file__).resolve().parents[2]
EFI = ROOT / ('upstream-sync/uefi/Platforms/SurfaceDuo1Pkg/Device/xiaomi-cepheus/'
              'Binaries/QcomPkg/Drivers/SmemDxe/SmemDxe.efi')


class SmemEvidenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.blob = EFI.read_bytes()
        cls.boot = (ROOT / 'gpu-work/out/scm-pair-final-6ce4c7c/boot-scm-pair.img').read_bytes()
        cls.bundle = json.loads((ROOT / 'logs/smemdxe-audit-20261112-v1/evidence.json').read_bytes())

    def verify(self, bundle=None, blob=None, boot=None):
        return MODULE.verify(self.blob if blob is None else blob,
                             self.bundle if bundle is None else bundle,
                             self.boot if boot is None else boot)

    def mutated(self, change):
        bundle = copy.deepcopy(self.bundle)
        record = next(r for r in bundle['records'] if r['operation'] == 'read_bytes'
                      and r['normalized_result']['address'] == '0x13a4')
        change(record)
        return bundle

    def test_original(self):
        result = self.verify()
        self.assertTrue(result['embedded_bytes_equal'])
        self.assertEqual(len(result['bindings']), 12)
        self.assertEqual(result['critical_instructions'], 43)
        self.assertFalse(result['firmware_execution'])
        self.assertFalse(result['gpu_execution'])

    def test_foreign_efi(self):
        blob = bytearray(self.blob)
        blob[-1] ^= 1
        with self.assertRaisesRegex(ValueError, 'EFI identity'):
            self.verify(blob=blob)

    def test_foreign_boot(self):
        boot = bytearray(self.boot)
        boot[-1] ^= 1
        with self.assertRaisesRegex(ValueError, 'boot identity'):
            self.verify(boot=boot)

    def test_foreign_evidence(self):
        with self.assertRaisesRegex(ValueError, 'binding'):
            self.verify(self.mutated(lambda r: r['subject']['digest'].update(sha256='0' * 64)))

    def test_truncated_bytes(self):
        with self.assertRaisesRegex(ValueError, 'binding'):
            self.verify(self.mutated(lambda r: r['normalized_result'].update(complete=False)))

    def test_changed_bytes(self):
        with self.assertRaisesRegex(ValueError, 'binding'):
            self.verify(self.mutated(lambda r: r['normalized_result'].update(bytes_hex='00' * 192)))

    def test_duplicate_selection(self):
        bundle = copy.deepcopy(self.bundle)
        record = next(r for r in bundle['records'] if r['operation'] == 'read_bytes'
                      and r['normalized_result']['address'] == '0x13a4')
        index = next(i for i, r in enumerate(bundle['records']) if r['operation'] == 'analyze_function')
        bundle['records'][index] = copy.deepcopy(record)
        with self.assertRaisesRegex(ValueError, 'ambiguous'):
            self.verify(bundle)

    def test_missing_selection(self):
        with self.assertRaisesRegex(ValueError, 'missing'):
            self.verify(self.mutated(lambda r: r['normalized_result'].update(address='0x13a8')))

    def test_llvm_failure(self):
        with patch.object(MODULE.LLVM, 'llvm_check', side_effect=ValueError('LLVM rejected')):
            with self.assertRaisesRegex(ValueError, 'LLVM rejected'):
                self.verify()


if __name__ == '__main__':
    unittest.main()
