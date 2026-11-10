#!/usr/bin/env python3
"""Synthetic accepted-PE identity and exit-path guards, no device."""
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


class ExitPathTests(unittest.TestCase):
    def fixture(self, name):
        blob, segments, reads, put = FIXTURE.fixture()
        reads.clear()
        ranges = VERIFY.DARWIN_RANGES if name == 'darwin' else VERIFY.USB_RANGES
        for address, size in ranges:
            put(address, bytes(size))
        if name == 'usb':
            put(0x14028, uuid.UUID('27abf055-b1b8-4c26-8048-748f37baa2df').bytes_le)
            put(0xf2cc, b'\x00\x01')
        return blob, segments, reads, put

    def capture(self, name):
        captured = []

        def instruction(blob, segments, address, mnemonic, operands):
            captured.append((address, mnemonic, operands))
            return FIXTURE.AbiTests().capture(blob, segments, address, mnemonic, operands)

        analyze = VERIFY.darwin_exit if name == 'darwin' else VERIFY.usb_exit
        blob, segments, reads, _ = self.fixture(name)
        with patch.object(VERIFY.ICB, 'instruction', side_effect=instruction):
            result = analyze(blob, segments, reads)
        return result, captured

    def test_darwin_retry_and_transition_are_not_resource_ownership(self):
        result, checks = self.capture('darwin')
        self.assertIn('three EBS attempts', result['retry'])
        self.assertIn('0x8000000000000002', result['retry'])
        self.assertIn('not a resource lease', result['limitations'][1])
        for check in ((0x27dc, 'cmp', 'x21, #3'),
                      (0x1368, 'sub', 'x27, x28, #0x1f'),
                      (0x2830, 'tbnz', 'x0, #0x3f, #0x28e0'),
                      (0x28e4, 'b.eq', '#0x28ec'),
                      (0xb04c, 'dc', 'civac, x4'),
                      (0xb090, 'br', 'x1')):
            self.assertIn(check, checks)

    def test_usb_cleanup_is_not_gpu_smmu_or_vote_clear(self):
        result, checks = self.capture('usb')
        self.assertEqual(result['registration']['tpl'], 8)
        self.assertEqual(result['core_vtable'], ['0x935c', '0x9e98'])
        self.assertIn('not GPU/global SMMU teardown', result['limitations'][1])
        self.assertIn('does not prove ICB votes were cleared', result['limitations'][2])
        self.assertIn((0x16f0, 'bl', '#0x4670'), checks)
        self.assertIn((0x4790, 'mov', 'w8, #1'), checks)
        self.assertIn((0x49c4, 'blr', 'x10'), checks)

    def test_every_exit_path_bounded_read_is_required(self):
        for name, ranges, analyze in (('darwin', VERIFY.DARWIN_RANGES, VERIFY.darwin_exit),
                                      ('usb', VERIFY.USB_RANGES, VERIFY.usb_exit)):
            blob, segments, reads, _ = self.fixture(name)
            for missing in ranges:
                with self.subTest(name=name, missing=missing):
                    changed = reads.copy()
                    del changed[missing]
                    with self.assertRaisesRegex(ValueError, 'coverage'):
                        analyze(blob, segments, changed)

    def test_exit_path_instruction_mutations_rejected(self):
        for name, analyze in (('darwin', VERIFY.darwin_exit), ('usb', VERIFY.usb_exit)):
            _, checks = self.capture(name)
            blob, segments, reads, _ = self.fixture(name)
            for address, _, _ in checks:
                def changed(blob, segments, observed, mnemonic, operands):
                    if observed == address:
                        raise ValueError('changed exit instruction')
                    return FIXTURE.AbiTests().capture(blob, segments, observed, mnemonic, operands)
                with self.subTest(name=name, address=hex(address)):
                    with patch.object(VERIFY.ICB, 'instruction', side_effect=changed):
                        with self.assertRaisesRegex(ValueError, 'changed exit instruction'):
                            analyze(blob, segments, reads)

    def test_synthetic_zero_instructions_fail_without_mock(self):
        for name, analyze in (('darwin', VERIFY.darwin_exit), ('usb', VERIFY.usb_exit)):
            blob, segments, reads, _ = self.fixture(name)
            with self.assertRaisesRegex(ValueError, 'unexpected instruction'):
                analyze(blob, segments, reads)

    def test_real_retry_limit_instruction_guard(self):
        blob, segments, _, put = self.fixture('darwin')
        put(0x27dc, bytes.fromhex('bf0e00f1'))
        VERIFY.ICB.instruction(blob, segments, 0x27dc, 'cmp', 'x21, #3')
        put(0x27dc, bytes.fromhex('bf0a00f1'))
        with self.assertRaisesRegex(ValueError, 'unexpected instruction'):
            VERIFY.ICB.instruction(blob, segments, 0x27dc, 'cmp', 'x21, #3')

    def test_real_usb_callback_target_guard(self):
        blob, segments, _, put = self.fixture('usb')
        put(0x16f0, bytes.fromhex('e00b0094'))
        VERIFY.ICB.instruction(blob, segments, 0x16f0, 'bl', '#0x4670')
        put(0x16f0, bytes.fromhex('e10b0094'))
        with self.assertRaisesRegex(ValueError, 'unexpected instruction'):
            VERIFY.ICB.instruction(blob, segments, 0x16f0, 'bl', '#0x4670')

    def test_usb_guid_and_file_flag_mutations_rejected(self):
        for address, data, message in ((0x14028, bytes(16), 'GUID mismatch'),
                                      (0xf2cc, b'\x01\x01', 'flags mismatch')):
            blob, segments, reads, put = self.fixture('usb')
            put(address, data)
            with self.assertRaisesRegex(ValueError, message):
                VERIFY.usb_exit(blob, segments, reads)

    def test_both_accepted_pe_identity_and_embedding_guards(self):
        original = b'synthetic PE'
        section = (len(original) + 4).to_bytes(3, 'little') + b'\x10' + original
        section += bytes(-len(section) % 4)
        digest = hashlib.sha256(original).hexdigest()
        for guid, source, name in ((VERIFY.DARWIN_GUID, VERIFY.DARWIN_SOURCE, 'DarwinBoot'),
                                   (VERIFY.USB_GUID, VERIFY.USB_SOURCE, 'UsbConfigDxe')):
            inner = {guid: bytes(24) + section}
            self.assertTrue(VERIFY.bind_pe(inner, original, guid, digest, source, name)
                            ['embedded_bytes_equal'])
            for mutated, blob, message in (({}, original, 'missing'),
                                            ({guid: bytes(24) + section * 2}, original, 'embedded'),
                                            (inner, original + b'changed', 'identity')):
                with self.assertRaisesRegex(ValueError, message):
                    VERIFY.bind_pe(mutated, blob, guid, digest, source, name)


if __name__ == '__main__':
    unittest.main()
