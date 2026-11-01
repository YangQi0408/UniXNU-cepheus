#!/usr/bin/env python3
"""Offline regression guards; these do not measure on-device performance."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('perf', Path(__file__).with_name('prepare-performance-patch.py'))
perf = importlib.util.module_from_spec(spec)
spec.loader.exec_module(perf)
boot_spec = importlib.util.spec_from_file_location('perf_boot', Path(__file__).with_name('make-performance-boot.py'))
boot = importlib.util.module_from_spec(boot_spec)
boot_spec.loader.exec_module(boot)
summary_spec = importlib.util.spec_from_file_location('summary', Path(__file__).with_name('summarize-performance.py'))
summary = importlib.util.module_from_spec(summary_spec)
summary_spec.loader.exec_module(summary)


class PerformancePolicy(unittest.TestCase):
    def test_blank_replaces_fade_and_keeps_clear_serialized(self):
        result = perf.replace(perf.FADE_BLOCK, perf.FADE_BLOCK, perf.BLANK_BLOCK)
        self.assertNotIn('row[x]', result)
        self.assertNotIn('fFadeStep', result)
        self.assertEqual(result.count('bzero('), 1)
        self.assertLess(result.index('bzero('), result.index('IOLockUnlock'))
        self.assertLess(result.index('fBlanked = true'), result.index('IOLockUnlock'))
        self.assertIn('bool blank = !fb->fBlanked', result)
        self.assertIn('fb->finishTick(sender)', result)
        self.assertIn('SWEET BLANK:', result)

    def test_log_parser_skips_incomplete_udp_lines(self):
        text = ('prefix SWEET PERF: ms=10000 submitted=22 presented=20 overwritten=3 '
                'copy_us=51424 max_us=4838 bytes=192067200 missed=0 off=0\n'
                'SWEET PERF: ms=10000 submitted=1 presented=1\n')
        records = summary.windows(text)
        self.assertEqual(len(records), 1)
        self.assertEqual(records[0]['copy_us'], 51424)
        self.assertEqual(records[0]['presented'], 20)

    def test_boot_args_stay_bounded_and_keep_safe_inputs(self):
        source = ('sweettouch=0 sweetkeys=1 sweetfb_noconsole=1 sweetusb=2 '
                  'sweethidtest=10 sweetfb_hz=30 startup_debug=1 -enable_kprintf_spam rd=ufsroot0')
        result = boot.make_args(source, 1).split()
        self.assertIn('sweettouch=0', result)
        self.assertIn('sweetkeys=1', result)
        self.assertIn('sweetkeyspononly=1', result)
        self.assertIn('sweethidonce=1', result)
        self.assertIn('sweethidtest=90', result)
        self.assertIn('sweetfb_hz=30', result)
        self.assertIn('sweetusb=2', result)
        self.assertNotIn('startup_debug=1', result)
        self.assertNotIn('-enable_kprintf_spam', result)
        self.assertEqual(len(result), len(set(t.split('=')[0] for t in result)))
        self.assertIn('sweetfb_fixedtimer=0', boot.make_args(source, 0))

    def test_rejects_changed_source(self):
        with self.assertRaises(ValueError):
            perf.transform(perf.FB, 'unrelated source')

    def test_rejects_duplicate_anchor(self):
        with self.assertRaises(ValueError):
            perf.replace('anchor anchor', 'anchor', 'replacement')

    def test_absolute_deadline_arithmetic(self):
        # Mirror finishTick's integer calculation: strictly future, no drift
        # while fast, no unbounded catch-up when a frame is slow.
        period = 100
        for previous, now, expected in ((100, 110, 200), (100, 200, 300),
                                        (100, 450, 500), (500, 501, 600)):
            deadline = previous + period
            if deadline <= now:
                deadline += ((now - deadline) // period + 1) * period
            self.assertEqual(deadline, expected)
            self.assertGreater(deadline, now)

    def test_one_shot_edge_bound(self):
        for once, limit in ((True, 2), (False, 60)):
            edges = []
            count = 0
            while True:
                edges.append(count % 2 == 0)
                count += 1
                if count >= (2 if once else 60):
                    break
            self.assertEqual(len(edges), limit)
            self.assertEqual(edges[-1], False)


if __name__ == '__main__':
    unittest.main()
