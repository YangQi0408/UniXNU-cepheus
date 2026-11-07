#!/usr/bin/env python3
"""Check saved-log boundaries and production/model differences in host RAM."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
import subprocess

SPEC = importlib.util.spec_from_file_location('replay', Path(__file__).with_name('replay-touch-log.py'))
REPLAY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(REPLAY)


def raw(n, slot, remaining, data):
    return (f'SWEET TOUCH RAW: n={n} slot={slot} remaining={remaining} skip=0 bytes='
            + ':'.join(f'{value:02x}' for value in data) + '\n')


class ReplayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        cls.executable = REPLAY.compile_replayer(Path(cls.directory.name))

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def test_short_tap_in_single_batch_is_preserved(self):
        text = raw(1, 0, 1, [0x13, 1, 100, 0, 20, 0, 0, 1])
        text += raw(2, 1, 1, [0x33, 1, 100, 0, 20, 0, 0, 0])
        result = REPLAY.replay(text, self.executable)
        self.assertEqual(result['hid_sink_counts']['production'], dict(down=1, move=0, up=1))
        self.assertEqual(result['hid_sink_counts']['ordered'], dict(down=1, move=0, up=1))
        self.assertEqual(result['batch_reporting_differences'], [])

    def test_same_slot_lift_and_repress_is_preserved(self):
        text = raw(1, 0, 0, [0x13, 1, 100, 0, 20, 0, 0, 0])
        text += raw(2, 0, 1, [0x33, 1, 100, 0, 20, 0, 0, 1])
        text += raw(3, 1, 1, [0x13, 1, 100, 0, 20, 0, 0, 0])
        result = REPLAY.replay(text, self.executable)
        self.assertEqual(result['hid_sink_counts']['production']['down'], 2)
        self.assertEqual(result['hid_sink_counts']['ordered']['down'], 2)

    def test_last_motion_before_release_matches_ordered_model(self):
        text = raw(1, 0, 0, [0x13, 1, 100, 0, 20, 0, 0, 0])
        text += raw(2, 0, 1, [0x23, 1, 200, 0, 20, 0, 0, 1])
        text += raw(3, 1, 1, [0x33, 1, 200, 0, 20, 0, 0, 0])
        result = REPLAY.replay(text, self.executable)
        self.assertEqual(result['batch_reporting_differences'], [])
        self.assertEqual(result['hid_sink_counts']['production'], dict(down=1, move=1, up=1))
        self.assertEqual(result['hid_events']['production'][-1][2], 200)

    def test_raw_limit_does_not_invent_missing_tail(self):
        text = ''.join(raw(n, 0, 0, [0x43, 1, 0xa0, 0, 1, 0, 0, 0]) for n in range(1, 128))
        text += raw(128, 0, 3, [0x13, 1, 100, 0, 20, 0, 0, 3])
        result = REPLAY.replay(text, self.executable)
        self.assertEqual(result['final_partial_batch_omitted'], [128])
        self.assertEqual(result['raw_events_replayed'], 127)

    def test_partial_tail_without_limit_is_rejected(self):
        with self.assertRaises(ValueError):
            REPLAY.batches(raw(1, 0, 1, [0x13, 1, 100, 0, 20, 0, 0, 1]))

    def test_gaps_malformed_and_wrong_depth_are_rejected(self):
        valid = raw(1, 0, 0, [0x43, 1, 0xa0, 0, 1, 0, 0, 0])
        for text in (valid.replace('n=1', 'n=2'), valid.replace('bytes=', 'bytes=zz:'),
                     valid.replace('remaining=0', 'remaining=1'), ''):
            with self.subTest(text=text), self.assertRaises(ValueError):
                REPLAY.batches(text)

    def test_unlogged_empty_continuation_cannot_be_silently_replayed(self):
        text = raw(1, 0, 0, [0x53, 2, 0x18, 0, 0, 0, 0, 0])
        text += raw(2, 0, 0, [0x13, 1, 100, 0, 20, 0, 0, 0])
        with self.assertRaises(subprocess.CalledProcessError):
            REPLAY.replay(text, self.executable)

    def test_continuation_payload_is_suppressed_in_both_modes(self):
        text = raw(1, 0, 1, [0x53, 2, 0x18, 0, 0, 0, 0, 1])
        text += raw(2, 1, 1, [0x13, 1, 100, 0, 20, 0, 0, 0]).replace('skip=0', 'skip=1')
        result = REPLAY.replay(text, self.executable)
        self.assertEqual(result['hid_sink_counts']['ordered']['down'], 0)
        self.assertEqual(result['batch_reporting_differences'], [])

    def test_interleaved_fingers_preserve_event_order(self):
        data = [[0x13, 1, 100, 0, 20, 0, 0, 3],
                [0x13, 0x11, 200, 0, 30, 0, 0, 2],
                [0x33, 1, 100, 0, 20, 0, 0, 1],
                [0x33, 0x11, 200, 0, 30, 0, 0, 0]]
        text = ''.join(raw(i + 1, i, 3, event) for i, event in enumerate(data))
        result = REPLAY.replay(text, self.executable)
        self.assertEqual([(e[0], e[1]) for e in result['hid_events']['production']],
                         [(0, 1), (1, 1), (0, 0), (1, 0)])

    def test_ready_and_error_release_existing_contacts_without_commands(self):
        for event_id in (0x03, 0xf3):
            with self.subTest(event_id=event_id):
                text = raw(1, 0, 1, [0x13, 1, 100, 0, 20, 0, 0, 1])
                text += raw(2, 1, 1, [event_id, 0, 0, 0, 0, 0, 0, 0])
                result = REPLAY.replay(text, self.executable)
                self.assertEqual(result['hid_sink_counts']['production'], dict(down=1, move=0, up=1))

    def test_full_depth_batch_preserves_sixteen_taps(self):
        data = [[0x13 if i % 2 == 0 else 0x33, 1, 100, 0, 20, 0, 0, 31 - i]
                for i in range(32)]
        text = ''.join(raw(i + 1, i, 31, event) for i, event in enumerate(data))
        result = REPLAY.replay(text, self.executable)
        self.assertEqual(result['hid_sink_counts']['production'], dict(down=16, move=0, up=16))
        self.assertEqual(result['batch_reporting_differences'], [])


if __name__ == '__main__':
    unittest.main()
