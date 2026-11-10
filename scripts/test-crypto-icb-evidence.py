#!/usr/bin/env python3
"""Synthetic route/ABI guards without proprietary inputs or device access."""
import importlib.util
from pathlib import Path
import struct
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location(
    'crypto_verify', Path(__file__).with_name('verify-crypto-icb-evidence.py'))
VERIFY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VERIFY)


def fixture():
    blob = bytearray(0x41000)
    blob[:2] = b'MZ'
    struct.pack_into('<I', blob, 60, 64)
    blob[64:68] = b'PE\0\0'
    struct.pack_into('<HHIIIHH', blob, 68, 0xaa64, 1, 0, 0, 0, 32, 0)
    struct.pack_into('<H', blob, 88, 0x20b)
    struct.pack_into('<8sIIII', blob, 120, b'.data', 0x40000, 0x1000, 0x40000, 256)
    reads = {}

    def put(address, data):
        offset = 256 + address - 0x1000
        blob[offset:offset + len(data)] = data
        reads[address, len(data)] = 'synthetic-' + hex(address)

    put(0x2dfd8, struct.pack('<I', 1))
    put(0x2dfe0, struct.pack('<Q', 0x2dfb8))
    put(0x2dfb8, struct.pack('<Q', 0x30ed8))
    put(0x30ed8, struct.pack('<I12xQ', 2, VERIFY.TREE_PATH[0]))
    target = 23 << 32
    for index, (address, key) in enumerate(zip(VERIFY.TREE_PATH, VERIFY.TREE_KEYS)):
        next_node = VERIFY.TREE_PATH[index + 1] if index < 7 else 0
        left, right = (next_node, 0) if key > target else (0, next_node)
        put(address, struct.pack('<4Q', key, 0x3cb28 if index == 7 else 0, left, right))
    put(0x3cb28, struct.pack('<I4xQ', 4, 0x3cb78))
    put(0x3cb78, struct.pack('<8Q', *(item[0] for pair in VERIFY.ROUTE_NODES for item in pair)))
    pointer = 0x2000
    for pair in VERIFY.ROUTE_NODES:
        for side, (address, name, node_id, width, ports) in zip(('master', 'slave'), pair):
            raw = bytearray(48)
            struct.pack_into('<QI', raw, 0, pointer, node_id)
            struct.pack_into('<I', raw, 24, width)
            struct.pack_into('<I', raw, 40 if side == 'master' else 28, ports)
            put(address, raw)
            put(pointer, name.encode('ascii') + b'\0')
            pointer += 64
    return blob, VERIFY.ICB.NPA.pe_segments(blob), reads, put


class RouteTests(unittest.TestCase):
    def test_configured_crypto_route(self):
        blob, segments, reads, _ = fixture()
        result = VERIFY.crypto_route(blob, segments, reads)
        self.assertEqual(result['configured_drv'], 2)
        self.assertEqual(len(result['tree_path']), 8)
        self.assertEqual(len(result['entries']), 4)
        self.assertEqual(result['entries'][0]['master']['id'], 23)
        self.assertEqual(result['entries'][-1]['slave']['name'], 'ebi')

    def test_cycle(self):
        blob, segments, reads, put = fixture()
        put(VERIFY.TREE_PATH[0] + 16, struct.pack('<Q', VERIFY.TREE_PATH[0]))
        with self.assertRaisesRegex(ValueError, 'cycle'):
            VERIFY.crypto_route(blob, segments, reads)

    def test_budget(self):
        blob, segments, reads, _ = fixture()
        for budget in (0, 65, 7):
            with self.assertRaisesRegex(ValueError, 'budget'):
                VERIFY.lookup(blob, segments, reads, VERIFY.TREE_PATH[0], 23 << 32, budget)

    def test_signed_subtraction_wraparound(self):
        blob, segments, reads, put = fixture()
        put(0x3000, struct.pack('<4Q', 0, 0, 0x3100, 0))
        put(0x3100, struct.pack('<4Q', (1 << 64) - 1, 0x3200, 0, 0))
        route, path = VERIFY.lookup(blob, segments, reads, 0x3000, (1 << 64) - 1)
        self.assertEqual((route, len(path)), (0x3200, 2))

    def test_missing_target_and_null_route(self):
        for key, route, expected in ((23 << 32 | 1, 0x3cb28, 'not found'),
                                      (23 << 32, 0, 'null')):
            blob, segments, reads, put = fixture()
            put(VERIFY.TREE_PATH[-1], struct.pack('<4Q', key, route, 0, 0))
            with self.assertRaisesRegex(ValueError, expected):
                VERIFY.crypto_route(blob, segments, reads)

    def test_changed_key_and_topology(self):
        for address, data in ((0x2dfd8, struct.pack('<I', 2)),
                              (0x30ed8, struct.pack('<I', 0)),
                              (VERIFY.TREE_PATH[0], struct.pack('<Q', VERIFY.TREE_KEYS[0] + 1))):
            blob, segments, reads, put = fixture()
            put(address, data)
            with self.assertRaisesRegex(ValueError, 'unexpected'):
                VERIFY.crypto_route(blob, segments, reads)

    def test_changed_route_count_and_pair_pointer(self):
        for count, pointer in ((0xffffffff, 0x3cb78), (4, 0x3cb80)):
            blob, segments, reads, put = fixture()
            put(0x3cb28, struct.pack('<I4xQ', count, pointer))
            with self.assertRaisesRegex(ValueError, 'count/pairs'):
                VERIFY.crypto_route(blob, segments, reads)

    def test_changed_node_fields(self):
        for offset in (8, 24, 40):
            blob, segments, reads, put = fixture()
            put(0x3c8e8 + offset, struct.pack('<I', 99))
            with self.assertRaisesRegex(ValueError, 'node fields'):
                VERIFY.crypto_route(blob, segments, reads)

    def test_unbacked_pointer_and_unaligned_record(self):
        for address in (0x50000, 0x30f31):
            blob, segments, reads, put = fixture()
            put(0x30f40, struct.pack('<Q', address))
            with self.assertRaisesRegex(ValueError, 'coverage|unaligned'):
                VERIFY.crypto_route(blob, segments, reads)

    def test_every_route_read_required(self):
        blob, segments, reads, _ = fixture()
        for missing in reads:
            changed = reads.copy()
            del changed[missing]
            with self.assertRaisesRegex(ValueError, 'coverage'):
                VERIFY.crypto_route(blob, segments, changed)

    def test_coverage_rejects_spliced_or_empty_read(self):
        for address, size in ((0x1000, 8), (0x1000, 0)):
            with self.assertRaisesRegex(ValueError, 'coverage'):
                VERIFY.covered({(0x1000, 4): 'a', (0x1004, 4): 'b'}, address, size)


class AbiTests(unittest.TestCase):
    def fixture(self):
        blob, segments, reads, put = fixture()
        reads.clear()
        put(0xb010, struct.pack('<2Q', 1600000000, 55250000))
        put(0xb020, b'/clk/ce1\0')
        put(0xbeb0, b'/icb/arbiter\0')
        for address, size in ((0x5138, 196), (0x9e08, 36), (0x9e34, 172),
                              (0x9d90, 120), (0x9ee0, 132), (0xdc64, 76),
                              (0xdd7c, 52), (0xcc24, 300)):
            put(address, bytes(size))
        return blob, segments, reads, put

    def capture(self, blob, segments, address, mnemonic, operands):
        return {'address': hex(address), 'mnemonic': mnemonic, 'operands': operands}

    def test_request_and_parser_checks(self):
        blob, segments, reads, _ = self.fixture()
        with patch.object(VERIFY.ICB, 'instruction', side_effect=self.capture):
            request = VERIFY.hash_request(blob, segments, reads)
            parser = VERIFY.clock_abi(blob, segments, reads)
        self.assertEqual((request['request_bytes'], request['issue_vector_words']), (32, 8))
        self.assertEqual((request['ib'], request['ab'], request['latency']), (1600000000, 55250000, 0))
        checked = {(item['address'], item['operands']) for item in request['instructions']}
        self.assertIn(('0x9eb8', 'x10, [x10, #0x10]'), checked)
        self.assertIn(('0x9f4c', 'x10, [x10, #0x90]'), checked)
        self.assertEqual(parser['key_formula'], '(master << 32) | slave')

    def test_wrong_constants_and_client_name(self):
        for address, data in ((0xb010, struct.pack('<2Q', 0, 55250000)),
                              (0xb020, b'/bad/ce1\0')):
            blob, segments, reads, put = self.fixture()
            put(address, data)
            with self.assertRaisesRegex(ValueError, 'unexpected Hash'):
                VERIFY.hash_request(blob, segments, reads)

    def test_instruction_mutations_propagate(self):
        blob, segments, reads, _ = self.fixture()
        for function in (VERIFY.hash_request, VERIFY.clock_abi):
            with patch.object(VERIFY.ICB, 'instruction', side_effect=ValueError('changed opcode')):
                with self.assertRaisesRegex(ValueError, 'changed opcode'):
                    function(blob, segments, reads)

    def test_short_protocol_wrapper_rejected(self):
        blob, segments, reads, _ = self.fixture()
        for address, size, short in ((0x9e34, 172, 136), (0x9ee0, 132, 104)):
            changed = reads.copy()
            del changed[address, size]
            changed[address, short] = 'short'
            with patch.object(VERIFY.ICB, 'instruction', side_effect=self.capture):
                with self.assertRaisesRegex(ValueError, 'coverage'):
                    VERIFY.hash_request(blob, segments, changed)

    def test_optimization_keeps_guard(self):
        with self.assertRaisesRegex(ValueError, 'guard'):
            VERIFY.require(False, 'explicit guard')


if __name__ == '__main__':
    unittest.main()
