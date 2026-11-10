#!/usr/bin/env python3
"""Synthetic evidence guards; no phone or proprietary firmware required."""
import copy
import importlib.util
from pathlib import Path
import struct
import unittest
from unittest.mock import patch
import uuid

SPEC = importlib.util.spec_from_file_location('icb_verify', Path(__file__).with_name('verify-icb-registrar-evidence.py'))
VERIFY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VERIFY)


class EvidenceTests(unittest.TestCase):
    def fixture(self):
        blob = bytearray(512)
        blob[:2] = b'MZ'
        struct.pack_into('<I', blob, 60, 64)
        blob[64:68] = b'PE\0\0'
        struct.pack_into('<HHIIIHH', blob, 68, 0xaa64, 1, 0, 0, 0, 32, 0)
        struct.pack_into('<H', blob, 88, 0x20b)
        struct.pack_into('<8sIIII', blob, 120, b'.text', 64, 0x1000, 32, 256)
        blob[256:260] = bytes.fromhex('c0035fd6')
        record = {'subject': {'digest': {'sha256': 'fixture'}}, 'operation': 'read_bytes',
                  'evidence_id': 'synthetic-read', 'normalized_result': {
                      'address': '0x1000', 'requested_bytes': 4, 'returned_bytes': 4,
                      'complete': True, 'bytes_hex': 'c0035fd6'}}
        return blob, {'records': [record]}

    def test_exact_read(self):
        blob, evidence = self.fixture()
        segments, reads = VERIFY.checked_reads(blob, 'fixture', evidence)
        self.assertEqual(reads, {(0x1000, 4): 'synthetic-read'})
        VERIFY.need_read(reads, 0x1000, 4)
        self.assertEqual(VERIFY.instruction(blob, segments, 0x1000, 'ret', '')['bytes_hex'], 'c0035fd6')

    def test_reject_wrong_target(self):
        blob, evidence = self.fixture()
        with self.assertRaisesRegex(ValueError, 'target'):
            VERIFY.checked_reads(blob, 'other', evidence)

    def test_reject_incomplete_or_wrong_size(self):
        blob, original = self.fixture()
        for key, value in (('complete', False), ('returned_bytes', 3), ('requested_bytes', 8)):
            evidence = copy.deepcopy(original)
            evidence['records'][0]['normalized_result'][key] = value
            with self.assertRaisesRegex(ValueError, 'incomplete'):
                VERIFY.checked_reads(blob, 'fixture', evidence)

    def test_reject_changed_bytes(self):
        blob, evidence = self.fixture()
        evidence['records'][0]['normalized_result']['bytes_hex'] = '00000000'
        with self.assertRaisesRegex(ValueError, 'differ'):
            VERIFY.checked_reads(blob, 'fixture', evidence)

    def test_reject_duplicate(self):
        blob, evidence = self.fixture()
        evidence['records'].append(copy.deepcopy(evidence['records'][0]))
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            VERIFY.checked_reads(blob, 'fixture', evidence)

    def test_reject_missing_range_and_instruction(self):
        blob, evidence = self.fixture()
        segments, reads = VERIFY.checked_reads(blob, 'fixture', evidence)
        with self.assertRaisesRegex(ValueError, 'missing'):
            VERIFY.need_read(reads, 0x1000, 8)
        with self.assertRaisesRegex(ValueError, 'instruction'):
            VERIFY.instruction(blob, segments, 0x1000, 'nop', '')

    def test_strings(self):
        blob, _ = self.fixture()
        blob[256:260] = b'ICB\0'
        segments = VERIFY.NPA.pe_segments(blob)
        self.assertEqual(VERIFY.cstring(blob, segments, 0x1000), 'ICB')
        blob[256] = 255
        with self.assertRaisesRegex(ValueError, 'ASCII'):
            VERIFY.cstring(blob, segments, 0x1000)
        blob[256:288] = b'A' * 32
        with self.assertRaises(ValueError):
            VERIFY.cstring(blob, segments, 0x1000)

    def test_guards_under_optimization(self):
        with self.assertRaises(ValueError):
            VERIFY.require(False, 'explicit guard')


class DalChainTests(unittest.TestCase):
    def fixture(self):
        blob, _ = EvidenceTests().fixture()
        blob.extend(bytes(0x41000 - len(blob)))
        struct.pack_into('<8sIIII', blob, 120, b'.data', 0x40000, 0x1000, 0x40000, 256)
        def put(address, data):
            offset = 256 + address - 0x1000
            blob[offset:offset + len(data)] = data
        put(0x7bf0, struct.pack('<QQI4xQ', 0x1d798, 0x1d248, 17, 0x1dd30))
        put(0x1d798, struct.pack('<II', 0x594, 0x40))
        put(0x2000, b'/icb/arb\0')
        put(0x2100, b'/unrelated\0')
        put(0x2200, b'/icb/arbiter\0')
        for index in range(17):
            put(0x1dd30 + index * 40, struct.pack('<QII', 0x2000 if index == 6 else 0x2100,
                                               0, 0x368 if index == 6 else 0))
        put(0x1d798 + 0x368, struct.pack('<III', 0x1280017d, 20, 0xff00ff00))
        put(0x1d798 + 0x40 + 0x17d, b'icb_info\0')
        put(0x1d388, struct.pack('<QQ', 8, 0x2dfd0))
        put(0x2dfd0, struct.pack('<QI4xQI4xQ', 0x2df78, 1, 0x2dfb8, 2, 0x2dfc0))
        put(0x2df78, struct.pack('<Q', 0x2200))
        put(0x2df78 + 0x30, struct.pack('<I4xQ', 1, 0x30e80))
        put(0x30e80, struct.pack('<Q', 0x2200))
        return blob, put

    def test_config_chain(self):
        blob, _ = self.fixture()
        result = VERIFY.dal_chain(blob, VERIFY.NPA.pe_segments(blob))
        self.assertEqual(result['name'], '/icb/arbiter')
        self.assertEqual(result['structure_index'], 20)
        self.assertEqual(result['initial_node_driver'], '0x0')

    def test_reject_duplicate_device(self):
        blob, put = self.fixture()
        put(0x1dd30, struct.pack('<QII', 0x2000, 0, 0x368))
        with self.assertRaisesRegex(ValueError, 'device'):
            VERIFY.dal_chain(blob, VERIFY.NPA.pe_segments(blob))

    def test_reject_property_mutations(self):
        for data in (struct.pack('<III', 0x1180017d, 20, 0xff00ff00),
                     struct.pack('<III', 0x1280017d, 21, 0xff00ff00),
                     struct.pack('<III', 0x1280017d, 20, 0)):
            blob, put = self.fixture()
            put(0x1db00, data)
            with self.assertRaisesRegex(ValueError, 'property'):
                VERIFY.dal_chain(blob, VERIFY.NPA.pe_segments(blob))

    def test_reject_wrong_resource(self):
        blob, put = self.fixture()
        put(0x30e80, struct.pack('<Q', 0x2100))
        with self.assertRaisesRegex(ValueError, 'name'):
            VERIFY.dal_chain(blob, VERIFY.NPA.pe_segments(blob))

    def test_reject_unbacked_string(self):
        blob, put = self.fixture()
        put(0x1dd30, struct.pack('<Q', 0x50000))
        with self.assertRaisesRegex(ValueError, 'file-backed'):
            VERIFY.dal_chain(blob, VERIFY.NPA.pe_segments(blob))


class CompletionTests(unittest.TestCase):
    CHURN_READS = ((0xe0f8, 200), (0x17f4, 68), (0x2ec8, 64), (0x2a1c, 148),
                   (0x4014, 160), (0x3f54, 48), (0x561c, 80), (0x735c, 236))
    EBS_READS = ((0xe028, 16), (0x14dc, 152), (0x149c, 20), (0x14b0, 44),
                (0x38f4, 136), (0x7d2c, 232), (0x7e14, 232))

    def fixture(self):
        blob, put = DalChainTests().fixture()
        put(0xe0f8 + 0x78, struct.pack('<Q', 0x17f4))
        put(0xe028, uuid.UUID('27abf055-b1b8-4c26-8048-748f37baa2df').bytes_le)
        put(0x38f4, struct.pack('<II', 0x94000000 | ((0x7d2c - 0x38f4) // 4),
                               0x94000000 | ((0x7e14 - 0x38f8) // 4)))
        put(0x38fc, bytes.fromhex('1f2003d5') * 32)
        return blob, put, VERIFY.NPA.pe_segments(blob)

    def reads(self, ranges):
        return {item: 'synthetic-' + hex(item[0]) for item in ranges}

    def capture(self, blob, segments, address, mnemonic, operands):
        return {'address': hex(address), 'mnemonic': mnemonic, 'operands': operands}

    def test_churn_scope_and_poll_checks(self):
        blob, _, segments = self.fixture()
        with patch.object(VERIFY, 'instruction', side_effect=self.capture):
            result = VERIFY.implicit_churn(blob, segments, self.reads(self.CHURN_READS))
        checked = {(item['address'], item['operands']) for item in result['instructions']}
        self.assertIn(('0x2ed0', 'x0, x19'), checked)
        self.assertIn(('0x2ed4', 'w1, w20'), checked)
        self.assertIn(('0x3f70', 'w8, #0x3f64'), checked)
        self.assertIn(('0x7414', 'w10, w11, #0xff'), checked)
        self.assertIn('handle-scoped', result['observation'])

    def test_churn_requires_every_range(self):
        blob, _, segments = self.fixture()
        for missing in self.CHURN_READS:
            reads = self.reads(self.CHURN_READS)
            del reads[missing]
            with patch.object(VERIFY, 'instruction', side_effect=self.capture):
                with self.assertRaisesRegex(ValueError, 'missing'):
                    VERIFY.implicit_churn(blob, segments, reads)

    def test_churn_rejects_changed_issue_slot(self):
        blob, put, segments = self.fixture()
        put(0xe0f8 + 0x78, struct.pack('<Q', 0x18dc))
        with self.assertRaisesRegex(ValueError, 'issue slot'):
            VERIFY.implicit_churn(blob, segments, self.reads(self.CHURN_READS))

    def test_instruction_failures_propagate(self):
        blob, _, segments = self.fixture()
        for function, ranges in ((VERIFY.implicit_churn, self.CHURN_READS),
                                  (VERIFY.ebs_clear, self.EBS_READS)):
            with patch.object(VERIFY, 'instruction', side_effect=ValueError('changed opcode')):
                with self.assertRaisesRegex(ValueError, 'changed opcode'):
                    function(blob, segments, self.reads(ranges))

    def test_ebs_requires_every_range(self):
        blob, _, segments = self.fixture()
        for missing in self.EBS_READS:
            reads = self.reads(self.EBS_READS)
            del reads[missing]
            with patch.object(VERIFY, 'instruction', side_effect=self.capture):
                with self.assertRaisesRegex(ValueError, 'missing'):
                    VERIFY.ebs_clear(blob, segments, reads)

    def test_ebs_rejects_wrong_guid_endianness(self):
        blob, put, segments = self.fixture()
        put(0xe028, uuid.UUID('27abf055-b1b8-4c26-8048-748f37baa2df').bytes)
        with self.assertRaisesRegex(ValueError, 'GUID'):
            VERIFY.ebs_clear(blob, segments, self.reads(self.EBS_READS))

    def test_ebs_clear_offsets_and_scope(self):
        blob, _, segments = self.fixture()
        calls = [{'mnemonic': 'bl', 'operands': '#0x7d2c'},
                 {'mnemonic': 'bl', 'operands': '#0x7e14'}]
        with patch.object(VERIFY, 'instruction', side_effect=self.capture), \
                patch.object(VERIFY.NPA, 'decode', return_value=calls):
            result = VERIFY.ebs_clear(blob, segments, self.reads(self.EBS_READS))
        checked = {(item['address'], item['operands']) for item in result['instructions']}
        self.assertIn(('0x1514', 'x8, [x8, #0x170]'), checked)
        self.assertIn(('0x7dbc', 'w8, w8, #0xd14'), checked)
        self.assertIn(('0x7ea4', 'w8, w8, #0xd1c'), checked)
        self.assertIn(('0x7ecc', 'wzr, [x9]'), checked)
        self.assertIn('Not a proof that ARC rail votes are cleared', result['limitations'])

    def test_ebs_rejects_extra_or_missing_calls(self):
        blob, put, segments = self.fixture()
        with patch.object(VERIFY, 'instruction', side_effect=self.capture):
            VERIFY.ebs_clear(blob, segments, self.reads(self.EBS_READS))
        for address, code in ((0x38f4, '1f2003d5'), (0x38fc, '00000094'),
                               (0x38fc, '00003fd6')):
            blob, put, segments = self.fixture()
            put(address, bytes.fromhex(code))
            with patch.object(VERIFY, 'instruction', side_effect=self.capture):
                with self.assertRaisesRegex(ValueError, 'loop calls'):
                    VERIFY.ebs_clear(blob, segments, self.reads(self.EBS_READS))


if __name__ == '__main__':
    unittest.main()
