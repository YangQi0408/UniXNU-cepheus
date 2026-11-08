#!/usr/bin/env python3
"""Synthetic transcripts only; exercise the saved-log checker without a phone."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('query_log', Path(__file__).with_name('analyze-scm-query-log.py'))
query_log = importlib.util.module_from_spec(spec)
spec.loader.exec_module(query_log)


def registers(values):
    return ':'.join(hex(value) if value else '0' for value in values)


def transcript(pairs, error=0, convention=2, available=31, supported=1):
    lines = [query_log.BEGIN]
    for incoming, outgoing in pairs:
        lines += ['CEPHEUS SCM INFO REGS: in=' + registers(incoming),
                  'CEPHEUS SCM INFO REGS: out=' + registers(outgoing)]
    last = pairs[-1][1]
    lines += [f'CEPHEUS SCM INFO: e={error} convention={convention} available={available:#x} supported={supported} calls={len(pairs)} transport={hex(last[0]) if last[0] else "0"} secure={hex(last[1]) if last[1] else "0"}; no PAS mutations',
              f'CEPHEUS SCM QUERY7: confirmed={int(bool(available & 16))} (0 means false/error/not-reached; see REGS); normal64 calls=7; false never overridden; no PAS mutations',
              f'CEPHEUS SCM INFO: end error={error} calls={len(pairs)} supported={supported}; no power or GPU startup']
    return '\n'.join(lines) + '\n'


def normal_pairs():
    inputs = [[0xc2000601, 1, 0x82000601, 0, 0, 0, 0, 0]]
    inputs += [[0x42000601, 1, value, 0, 0, 0, 0, 0]
               for value in (0x02000201, 0x02000202, 0x02000205, 0x02000206, 0x02000207)]
    inputs += [[0x42000207, 1, 13, 0, 0, 0, 0, 0]]
    return [(incoming, [0, 1, 0, 0, 0, 0, 0, 0]) for incoming in inputs]


class QueryLog(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        cls.executable = query_log.compile_replayer(Path(cls.directory.name))

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def analyze(self, text):
        return query_log.analyze(text, self.executable)

    def test_success(self):
        result = self.analyze(transcript(normal_pairs()))
        self.assertEqual(result['status'], 'TRANSCRIPT_COMPLETE')
        self.assertTrue(result['query7_confirmed'])
        self.assertTrue(result['pas13_supported'])
        self.assertEqual(result['calls'], 7)

    def test_pas_false_is_complete_evidence_not_success(self):
        pairs = normal_pairs()
        pairs[-1][1][1] = 0
        result = self.analyze(transcript(pairs, error=-1, supported=0))
        self.assertEqual(result['status'], 'TRANSCRIPT_COMPLETE')
        self.assertTrue(result['query7_confirmed'])
        self.assertFalse(result['pas13_supported'])
        self.assertEqual(result['engine_error'], -1)

    def test_query7_false_does_not_mean_pas_false(self):
        pairs = normal_pairs()[:-1]
        pairs[-1][1][1] = 0
        result = self.analyze(transcript(pairs, error=-1, available=15, supported=0))
        self.assertEqual(result['status'], 'TRANSCRIPT_COMPLETE')
        self.assertFalse(result['query7_confirmed'])
        self.assertIsNone(result['pas13_supported'])

    def test_transport_error_is_not_false(self):
        pairs = normal_pairs()
        pairs[-1][1][0] = (1 << 64) - 12
        result = self.analyze(transcript(pairs, error=-1, supported=0))
        self.assertEqual(result['status'], 'TRANSCRIPT_COMPLETE')
        self.assertIsNone(result['pas13_supported'])

    def test_32bit_fallback(self):
        pairs = normal_pairs()
        pairs[0][1][0] = (1 << 64) - 1
        fallback = ([0x82000601, 1, 0x82000601, 0, 0, 0, 0, 0], [0, 1, 0, 0, 0, 0, 0, 0])
        for incoming, _ in pairs[1:]:
            incoming[0] &= ~0x40000000
        pairs.insert(1, fallback)
        result = self.analyze(transcript(pairs, convention=1))
        self.assertEqual(result['status'], 'TRANSCRIPT_COMPLETE')
        self.assertEqual(result['calls'], 8)
        self.assertTrue(result['pas13_supported'])

    def test_interrupt_resume_preserves_all_registers(self):
        pairs = normal_pairs()
        incoming, outgoing = pairs[-1]
        interrupted = [1, 7, 8, 9, 10, 11, 0x12345678, 13]
        pairs[-1] = (incoming, interrupted)
        pairs.append((interrupted.copy(), outgoing))
        result = self.analyze(transcript(pairs))
        self.assertEqual(result['status'], 'TRANSCRIPT_COMPLETE')
        self.assertTrue(result['pas13_supported'])
        pairs[-1][0][6] = 0
        self.assertEqual(self.analyze(transcript(pairs))['status'], 'REJECTED')

    def test_interrupted_budget_has_no_boolean_outcome(self):
        interrupted = [1, 0, 0, 0, 0, 0, 0xabcd, 0]
        pairs = [(normal_pairs()[0][0], interrupted)]
        pairs += [(interrupted.copy(), interrupted.copy()) for _ in range(15)]
        result = self.analyze(transcript(pairs, error=-1, convention=0, available=0, supported=0))
        self.assertEqual(result['status'], 'TRANSCRIPT_COMPLETE')
        self.assertEqual(result['calls'], 16)
        self.assertIsNone(result['pas13_supported'])

    def test_missing_records_never_complete(self):
        lines = transcript(normal_pairs()).splitlines()
        for index in range(len(lines)):
            result = self.analyze('\n'.join(lines[:index] + lines[index+1:]))
            self.assertNotEqual(result['status'], 'TRANSCRIPT_COMPLETE')
        # A complete pair missing from UDP must not satisfy the summary count.
        self.assertEqual(self.analyze('\n'.join(lines[:5] + lines[7:]))['status'], 'INCOMPLETE')

    def test_legacy_six_calls_incomplete(self):
        pairs = normal_pairs()
        del pairs[-2]
        text = transcript(pairs, available=15)
        text = '\n'.join(line for line in text.splitlines() if not line.startswith('CEPHEUS SCM QUERY7:'))
        self.assertEqual(self.analyze(text)['status'], 'INCOMPLETE')

    def test_wrong_id_mutation_or_pointers_rejected(self):
        for register, value in ((0, 0x42000205), (2, 12), (3, 0x80000000)):
            pairs = normal_pairs()
            pairs[-1][0][register] = value
            self.assertEqual(self.analyze(transcript(pairs))['status'], 'REJECTED')

    def test_summary_flag_or_order_tampering(self):
        text = transcript(normal_pairs())
        for changed in (text.replace('available=0x1f', 'available=0xf'),
                        text.replace('confirmed=1', 'confirmed=0'),
                        text.replace('end error=0', 'end error=-1'),
                        text + text):
            self.assertEqual(self.analyze(changed)['status'], 'REJECTED')

    def test_register_bounds_and_extra_records(self):
        text = transcript(normal_pairs())
        for changed in (text.replace('in=0xc2000601', 'in=0x100000000c2000601'),
                        text.replace('in=0xc2000601', 'in=0x10000000000000000'),
                        text + 'CEPHEUS SCM INFO: unexpected\n'):
            self.assertEqual(self.analyze(changed)['status'], 'REJECTED')

    def test_unknown_or_nonboolean_secure_result(self):
        pairs = normal_pairs()
        pairs[-1][1][1] = 2
        result = self.analyze(transcript(pairs, error=-1, supported=0))
        self.assertEqual(result['status'], 'TRANSCRIPT_COMPLETE')
        self.assertIsNone(result['pas13_supported'])
        self.assertEqual(self.analyze('')['status'], 'INCOMPLETE')


if __name__ == '__main__':
    unittest.main()
