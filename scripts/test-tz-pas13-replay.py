#!/usr/bin/env python3
"""Synthetic bounded-emulator tests; no proprietary firmware or phone needed."""
import importlib.util
from pathlib import Path
import struct
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('tz_replay', Path(__file__).with_name('replay-tz-pas13.py'))
REPLAY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(REPLAY)


def fixture(words=(0xd2800022, 0xf9000022, 0xa900fc3f, 0xd2800000, 0xd65f03c0)):
    # Only synthetic instructions; no private bytes are used.
    loaded = [(a, struct.pack('<I', 0xd503201f) * (n // 4)) for a, n, _ in REPLAY.CODE]
    raw = b''.join(struct.pack('<I', word) for word in words)
    loaded[0] = (loaded[0][0], raw + loaded[0][1][len(raw):])
    loaded += [(a, bytes(n)) for a, n in REPLAY.DATA]
    return loaded


class GuardTests(unittest.TestCase):
    def test_range_crossing_and_overflow(self):
        for address, size in ((0x1004, 0), (0x1003, 2), (-1, 1), ((1 << 64) - 1, 2)):
            self.assertFalse(REPLAY.contains(((0x1000, 4),), address, size))
        self.assertTrue(REPLAY.contains(((0x1000, 4),), 0x1000, 4))

    def test_identity_and_truncation(self):
        for blob in (b'', bytes(64), bytes(4194304), bytes(4194305)):
            with self.assertRaisesRegex(ValueError, 'identity'):
                REPLAY.prepared(blob)

    def test_loaded_ranges(self):
        loaded = fixture()
        for invalid in (loaded[:-1], loaded + [(0x40000000, bytes(4))],
                        [(loaded[0][0], loaded[0][1][:-1])] + loaded[1:]):
            with self.assertRaisesRegex(ValueError, 'exactly'):
                REPLAY.validate_loaded(invalid)

    def test_secure_and_exception_instructions(self):
        for word in (0xd4000003, 0xd4000002, 0xd4000001, 0xd4200000, 0xd4400000, 0xd69f03e0):
            with self.assertRaisesRegex(ValueError, 'prohibited'):
                REPLAY.validate_loaded(fixture((word,)))

    def test_invalid_instruction(self):
        with self.assertRaisesRegex(ValueError, 'decode'):
            REPLAY.validate_loaded(fixture((0xffffffff,)))

    def test_mutation_bounds(self):
        for address, data in ((REPLAY.COUNT + 3, b'xx'), (REPLAY.COUNT, b''),
                              (REPLAY.CODE[0][0], bytes(4)), (REPLAY.STACK, bytes(4))):
            with self.assertRaisesRegex(ValueError, 'mutation'):
                REPLAY.replay(fixture(), 'synthetic', 0, ((address, data),))

    def test_execution_escape_within_mapped_page(self):
        # Branch to an undeclared address on the same executable page.
        with self.assertRaisesRegex(ValueError, 'execution outside'):
            REPLAY.replay(fixture((0x14000011,)), 'synthetic', 0)

    def test_result_padding_is_not_readable(self):
        # ldr x2, [x1, #24] is mapped, but outside the declared 24-byte result.
        with self.assertRaisesRegex(ValueError, 'outside declared RAM'):
            REPLAY.replay(fixture((0xf9400c22,)), 'synthetic', 0)

    def test_unmapped_access(self):
        with self.assertRaisesRegex(ValueError, 'unmapped/protected'):
            REPLAY.replay(fixture((0xd2800002, 0xf9400043)), 'synthetic', 0)

    def test_code_write(self):
        # adr x2, .; str x0, [x2] must fail, even on a mapped code page.
        with self.assertRaises(ValueError):
            REPLAY.replay(fixture((0x10000002, 0xf9000040)), 'synthetic', 0)

    def test_nonreturn_has_no_verdict(self):
        with patch.object(REPLAY, 'INSTRUCTION_LIMIT', 32):
            case = REPLAY.replay(fixture((0x14000000,)), 'synthetic', 0)
        call = case['calls'][0]
        self.assertEqual(call['termination'], 'instruction-limit')
        self.assertFalse(call['returned'])
        self.assertIsNone(call['results'])
        self.assertIsNone(call['wrapper_return'])
        REPLAY.check_case(case, None)
        with self.assertRaisesRegex(ValueError, 'verdict'):
            REPLAY.check_case(case, 0)

    def test_result_classification_is_not_an_assert(self):
        # mov x2, #1; str x2,[x1]; stp xzr,xzr,[x1,#8]; mov x0,#0; ret
        case = REPLAY.replay(fixture((0xd2800022, 0xf9000022, 0xa900fc3f,
                                      0xd2800000, 0xd65f03c0)), 'synthetic', 0, repeat=True)
        self.assertEqual(len(case['calls']), 2)
        self.assertEqual(case['calls'][0]['termination'], 'returned')
        REPLAY.check_case(case, 1)
        with self.assertRaisesRegex(ValueError, 'verdict'):
            REPLAY.check_case(case, 0)

    def test_bad_mpidr(self):
        for mpidr in (-1, 1 << 64):
            with self.assertRaisesRegex(ValueError, 'MPIDR'):
                REPLAY.replay(fixture(), 'synthetic', mpidr)

    def test_empty_calls_cannot_pass(self):
        for verdict in (None, 0, 1):
            with self.assertRaisesRegex(ValueError, 'no calls'):
                REPLAY.check_case({'case': 'synthetic', 'calls': []}, verdict)

    def test_access_log_limit_keeps_full_counts(self):
        with patch.object(REPLAY, 'ACCESS_LOG_LIMIT', 1):
            case = REPLAY.replay(fixture(), 'synthetic', 0)
        counts = case['memory_access_counts']
        self.assertEqual(len(case['memory_accesses']), 1)
        self.assertEqual(counts['omitted'] + 1, counts['read'] + counts['write'])
        self.assertGreater(counts['write'], 1)

    def test_failed_wrapper_cannot_pass(self):
        case = REPLAY.replay(fixture((0xd2800022, 0xf9000022, 0xa900fc3f,
                                      0xd2800020, 0xd65f03c0)), 'synthetic', 0)
        with self.assertRaisesRegex(ValueError, 'verdict'):
            REPLAY.check_case(case, 1)


if __name__ == '__main__':
    unittest.main()
