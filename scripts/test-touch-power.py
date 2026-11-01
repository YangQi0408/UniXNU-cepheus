#!/usr/bin/env python3
"""Verify generated XOB policy/guards, not physical voltage or firmware behavior."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
import sys

spec = importlib.util.spec_from_file_location('power', Path(__file__).with_name('prepare-touch-power-patch.py'))
power = importlib.util.module_from_spec(spec)
spec.loader.exec_module(power)


class PowerPolicy(unittest.TestCase):
    def test_reject_changed_source(self):
        with self.assertRaises(ValueError): power.transform('incorrect source')

    def test_pinned_patch_xob_policy(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            subprocess.run(['git', 'init', '-q', str(root)], check=True)
            for p in sorted(PATCHES.glob('*.patch')):
                subprocess.run(['git', '-C', str(root), 'apply', '--whitespace=nowarn',
                                '--include=bsd/dev/sweet_rpmh.c', str(p.resolve())], check=True)
            s = power.transform((root / power.TARGET).read_text())
            self.assertIn('const unsigned ncmd = xob ? 1u : 3u', s)
            self.assertIn('offsets[3] = {xob ? 4u : 0u, 8, 4}', s)
            self.assertIn('values[3] = {xob ? 1u : mv, 7, 1}', s)
            self.assertIn('rh_lookup("ldoc1", &address)', s)
            self.assertIn('rh_send(address, 0, true)', s)
            self.assertIn('rh_send(address, mv, false)', s)
            self.assertIn('rh_free(t)', s)
            self.assertIn('rh_poisoned = true', s)
            self.assertEqual(s.count('c < ncmd;'), 2)


if __name__ == '__main__':
    PATCHES = Path(sys.argv.pop(1)).resolve()
    unittest.main()
