#!/usr/bin/env python3
"""Packaging argument guards only, not device acceptance."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('ready', Path(__file__).with_name('verify-ready-boot.py'))
ready = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ready)
BASE = ('sweetusb=2 sweettouch=1 sweetkeys=1 sweetkeyspononly=1 '
        'sweettouchgpio=1 sweettouchpower=1 sweettouchdelay=60 '
        'sweethidonce=1 sweetfb_noconsole=1')


class Args(unittest.TestCase):
    def test_soc_delay(self):
        line = BASE + ' sweettouchdiag=1 cephsoc=1 cephsocdelay=240'
        ready.validate_args(line, soc_only=True, soc_delay=True)
        for value in (-1, 0, 1, 45, 239, 241, 2147483647):
            with self.assertRaises(AssertionError):
                ready.validate_args(line.replace('delay=240', f'delay={value}'), soc_only=True, soc_delay=True)
        for name in ('cephfuse', 'cepharc', 'cephgx', 'cephscm', 'cephboost', 'cephmem'):
            for value in (0, 1, 2):
                with self.assertRaises(AssertionError):
                    ready.validate_args(line + f' {name}={value}', soc_only=True, soc_delay=True)
        for token in ('cephsoc=1', 'cephsocdelay=240', 'sweettouchdiag=1'):
            with self.assertRaises(AssertionError):
                ready.validate_args(line.replace(token, ''), soc_only=True, soc_delay=True)

    def test_soc_delay_requires_explicit_basic_profile(self):
        line = BASE + ' sweettouchdiag=1 cephsoc=1 cephsocdelay=240'
        for kwargs in ({}, {'soc_only': True}, {'touch_only': True}, {'all_probes': True},
                       {'soc_delay': True}, {'soc_only': True, 'soc_delay': True, 'scm_info': True},
                       {'soc_only': True, 'soc_delay': True, 'memory_test': 1}):
            with self.assertRaises(AssertionError):
                ready.validate_args(line, **kwargs)

    def test_soc_delay_two_argument_delta(self):
        old = {'entry': [{'args': BASE + ' sweettouchdiag=1'}]}
        added = ['cephsoc=1', 'cephsocdelay=240']
        new = {'entry': [{'args': old['entry'][0]['args'] + ' ' + ' '.join(added)}]}
        ready.validate_added_args(new, old, added)
        with self.assertRaises(AssertionError):
            ready.validate_added_args(new, old, added[:1])

    def test_single_argument_delta(self):
        old = {"timeout": 0, "entry": [{"name": "iOS", "args": BASE}]}
        new = {"timeout": 0, "entry": [{"name": "iOS", "args": BASE + " cephsoc=1"}]}
        ready.validate_added_args(new, old, ["cephsoc=1"])
        self.assertEqual(new["entry"][0]["args"], BASE + " cephsoc=1")
        self.assertEqual(old["entry"][0]["args"], BASE)
        for line in (BASE, BASE + " cephsoc=0", BASE + " cephsoc=1 cephsoc=1",
                     BASE + " cephsoc=1 cephfuse=1", BASE.replace("60", "30") + " cephsoc=1",
                     " ".join(reversed(BASE.split())) + " cephsoc=1"):
            with self.assertRaises(AssertionError):
                ready.validate_added_args({**new, "entry": [{"name": "iOS", "args": line}]}, old, ["cephsoc=1"])

    def test_delta_rejects_other_config_changes(self):
        old = {"timeout": 0, "entry": [{"name": "iOS", "args": BASE}]}
        for new in ({"timeout": 1, "entry": [{"name": "iOS", "args": BASE + " cephsoc=1"}]},
                    {"timeout": 0, "entry": [{"name": "other", "args": BASE + " cephsoc=1"}]},
                    {"timeout": 0, "entry": [{"name": "iOS", "args": BASE + " cephsoc=1"}, {"args": ""}]}):
            with self.assertRaises(AssertionError):
                ready.validate_added_args(new, old, ["cephsoc=1"])

    def test_delta_rejects_existing_or_duplicate_added_names(self):
        old = {"entry": [{"args": BASE}]}
        new = {"entry": [{"args": BASE + " cephsoc=1"}]}
        for added in ([], ["sweetusb=2"], ["cephsoc=1", "cephsoc=1"], ["cephsoc=1", "cephsoc=0"]):
            with self.assertRaises(AssertionError):
                ready.validate_added_args(new, old, added)

    def test_original_profiles(self):
        line = BASE + ' cephsoc=1 cephgx=1'
        ready.validate_args(line)
        for n in (1, 2):
            ready.validate_args(line + f' cephmem={n}', memory_test=n)
        with self.assertRaises(AssertionError):
            ready.validate_args(line + ' cephmem=1')

    def test_touch_only_rejects_all_probe_presence(self):
        line = BASE + ' sweettouchdiag=1'
        ready.validate_args(line, touch_only=True)
        for name in ('cephsoc', 'cepharc', 'cephfuse', 'cephgx', 'cephmem', 'cephboost', 'sweettouchreset'):
            for value in (0, 1, 2):
                with self.assertRaises(AssertionError):
                    ready.validate_args(line + f' {name}={value}', touch_only=True)
        with self.assertRaises(AssertionError):
            ready.validate_args(BASE, touch_only=True)
        with self.assertRaises(AssertionError):
            ready.validate_args(line + ' cephmem=1', memory_test=1, touch_only=True)

    def test_duplicates_and_missing_inputs(self):
        line = BASE + ' sweettouchdiag=1'
        for token in line.split():
            with self.assertRaises(AssertionError):
                ready.validate_args(line + ' ' + token, touch_only=True)
            with self.assertRaises(AssertionError):
                ready.validate_args(line.replace(token, ''), touch_only=True)

    def test_soc_only(self):
        line = BASE + ' sweettouchdiag=1 cephsoc=1'
        ready.validate_args(line, soc_only=True)
        for name in ('cepharc', 'cephfuse', 'cephgx', 'cephmem', 'cephboost', 'sweettouchreset'):
            for value in (0, 1, 2):
                with self.assertRaises(AssertionError):
                    ready.validate_args(line + f' {name}={value}', soc_only=True)
        for invalid in (line.replace('cephsoc=1', 'cephsoc=0'),
                        line.replace('cephsoc=1', ''),
                        line.replace('sweettouchdiag=1', '')):
            with self.assertRaises(AssertionError):
                ready.validate_args(invalid, soc_only=True)
        with self.assertRaises(AssertionError):
            ready.validate_args(line, touch_only=True, soc_only=True)
        with self.assertRaises(AssertionError):
            ready.validate_args(line + ' cephmem=1', memory_test=1, soc_only=True)

    def test_soc_fuse(self):
        line = BASE + ' sweettouchdiag=1 cephsoc=1 cephfuse=1'
        ready.validate_args(line, soc_fuse=True)
        for name in ('cepharc', 'cephgx', 'cephmem', 'cephboost', 'sweettouchreset'):
            for value in (0, 1, 2):
                with self.assertRaises(AssertionError):
                    ready.validate_args(line + f' {name}={value}', soc_fuse=True)
        for token in ('cephsoc=1', 'cephfuse=1', 'sweettouchdiag=1'):
            for invalid in (line.replace(token, ''), line.replace(token, token[:-1]+'0'), line+' '+token):
                with self.assertRaises(AssertionError):
                    ready.validate_args(invalid, soc_fuse=True)
        for kwargs in ({'touch_only': True}, {'soc_only': True}, {'soc_fuse_arc': True}, {'memory_test': 1}):
            with self.assertRaises(AssertionError):
                ready.validate_args(line, soc_fuse=True, **kwargs)

    def test_soc_fuse_arc(self):
        line = BASE + ' sweettouchdiag=1 cephsoc=1 cephfuse=1 cepharc=1'
        ready.validate_args(line, soc_fuse_arc=True)
        for name in ('cephgx', 'cephmem', 'cephboost', 'sweettouchreset'):
            for value in (0, 1, 2):
                with self.assertRaises(AssertionError):
                    ready.validate_args(line + f' {name}={value}', soc_fuse_arc=True)
        for token in ('cephsoc=1', 'cephfuse=1', 'cepharc=1', 'sweettouchdiag=1'):
            for invalid in (line.replace(token, ''), line.replace(token, token[:-1]+'0'), line+' '+token):
                with self.assertRaises(AssertionError):
                    ready.validate_args(invalid, soc_fuse_arc=True)
        for kwargs in ({'touch_only': True}, {'soc_only': True}, {'soc_fuse': True}, {'memory_test': 1}):
            with self.assertRaises(AssertionError):
                ready.validate_args(line, soc_fuse_arc=True, **kwargs)

    def test_all_probes(self):
        line = BASE + ' sweettouchdiag=1 cephsoc=1 cephfuse=1 cepharc=1 cephgx=1'
        ready.validate_args(line, all_probes=True)
        for name in ('cephmem', 'cephboost', 'sweettouchreset'):
            for value in (0, 1, 2):
                with self.assertRaises(AssertionError):
                    ready.validate_args(line + f' {name}={value}', all_probes=True)
        for token in line.split():
            for invalid in (line.replace(token, ''), line.replace(token, token.split('=')[0]+'=0'), line+' '+token):
                with self.assertRaises(AssertionError):
                    ready.validate_args(invalid, all_probes=True)
        for kwargs in ({'touch_only': True}, {'soc_only': True}, {'soc_fuse': True},
                       {'soc_fuse_arc': True}, {'memory_test': 1}):
            with self.assertRaises(AssertionError):
                ready.validate_args(line, all_probes=True, **kwargs)

    def test_scm_info_optin(self):
        baseline = BASE + ' sweettouchdiag=1 cephsoc=1 cephfuse=1 cepharc=1 cephgx=1'
        line = baseline + ' cephscm=1'
        ready.validate_args(line, all_probes=True, scm_info=True)
        for value in (0, 1, 2):
            with self.assertRaises(AssertionError):
                ready.validate_args(baseline + f' cephscm={value}', all_probes=True)
        for invalid in (baseline, line.replace('cephscm=1', 'cephscm=0'),
                        line.replace('cephsoc=1', ''), line+' cephscm=1',
                        line+' cephboost=1', line+' cephmem=1'):
            with self.assertRaises(AssertionError):
                ready.validate_args(invalid, all_probes=True, scm_info=True)
        with self.assertRaises(AssertionError):
            ready.validate_args(line, scm_info=True)
        with self.assertRaises(AssertionError):
            ready.validate_args(line+' cephmem=1', memory_test=1, all_probes=True, scm_info=True)


if __name__ == '__main__':
    unittest.main()
