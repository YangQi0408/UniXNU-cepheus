#!/usr/bin/env python3
"""Exercise production packaging guards without kernels, FAT tools or devices."""
import argparse
from contextlib import redirect_stderr
import importlib.util
import io
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('performance', Path(__file__).with_name('make-performance-boot.py'))
performance = importlib.util.module_from_spec(spec)
spec.loader.exec_module(performance)
MARKERS = (b'cephsocdelay', b'CEPHEUS SOC: %s delay_s=%u uptime_ms=%llu',
           b'baseline-end', b'CEPHEUS SOC: refused delay profile')
OPTIONS = dict(soc_probe_delay=240, soc_probe=True, touch_gpio=True, touch_diagnostic=True,
               touch_probe=False, scm_info_probe=False, gpu_arc_probe=False,
               gpu_memory_test=False, gpu_workspace_test=False, gpu_fuse_probe=False,
               gpu_gx_probe=False, cpu_trial=False, cpu_interactive=False)


class DelayPackaging(unittest.TestCase):
    def validate(self, values, markers=MARKERS):
        performance.validate_soc_delay(argparse.Namespace(**values), b'\0'.join(markers), argparse.ArgumentParser())

    def refused(self, values, markers=MARKERS):
        with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as error:
            self.validate(values, markers)
        self.assertEqual(error.exception.code, 2)

    def test_optin_and_default(self):
        self.validate(OPTIONS)
        self.validate(dict(OPTIONS, soc_probe_delay=None), ())

    def test_fixed_delay_only(self):
        for value in (-1, 0, 45, 239, 241):
            self.refused(dict(OPTIONS, soc_probe_delay=value))

    def test_basic_touch_profile_only(self):
        for key, value in OPTIONS.items():
            if key != 'soc_probe_delay':
                self.refused(dict(OPTIONS, **{key: not value}))

    def test_all_compiled_markers_required(self):
        for marker in MARKERS:
            self.refused(OPTIONS, tuple(m for m in MARKERS if m != marker))


if __name__ == '__main__':
    unittest.main()
