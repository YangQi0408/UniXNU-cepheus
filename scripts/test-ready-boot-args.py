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


if __name__ == '__main__':
    unittest.main()
