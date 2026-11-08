#!/usr/bin/env python3
"""Exercise ZAP preflight profile rejection with production packaging APIs."""
import argparse
from contextlib import redirect_stderr
import importlib.util
import io
from pathlib import Path
import unittest

def module(name):
    spec=importlib.util.spec_from_file_location(name,Path(__file__).with_name(name+'.py'))
    result=importlib.util.module_from_spec(spec);spec.loader.exec_module(result);return result
make=module('make-performance-boot');verify=module('verify-ready-boot')
BASE=('sweetusb=2 sweettouch=1 sweetkeys=1 sweetkeyspononly=1 sweettouchgpio=1 '
      'sweettouchpower=1 sweettouchdelay=60 sweethidonce=1 sweetfb_noconsole=1 sweettouchdiag=1')
MARKERS=(b'cephzapmem',b'CEPHEUS ZAP TEST: scheduled delay_s=90',b'CEPHEUS ZAP TEST: begin version=1',
         b'CEPHEUS ZAP TEST: end result=',b'CEPHEUS ZAP MEM: translated',b'CEPHEUS ZAP MEM: restored')
OPTIONS=dict(zap_memory_test=True,touch_gpio=True,touch_power=True,touch_diagnostic=True,touch_probe=False,
             soc_probe=False,soc_probe_delay=None,scm_info_probe=False,gpu_arc_probe=False,gpu_memory_test=False,
             gpu_workspace_test=False,gpu_fuse_probe=False,gpu_gx_probe=False,cpu_trial=False,cpu_interactive=False)
class Packaging(unittest.TestCase):
    def validate(self,values,markers=MARKERS):
        make.validate_zap_memory(argparse.Namespace(**values),b'\0'.join(markers),argparse.ArgumentParser())
    def test_make_profile(self):
        self.validate(OPTIONS)
        self.validate(dict(OPTIONS,zap_memory_test=False),())
        for key,value in OPTIONS.items():
            if key=='zap_memory_test':continue
            changed=240 if key=='soc_probe_delay' else not value
            with redirect_stderr(io.StringIO()),self.assertRaises(SystemExit):self.validate(dict(OPTIONS,**{key:changed}))
        for marker in MARKERS:
            with redirect_stderr(io.StringIO()),self.assertRaises(SystemExit):self.validate(OPTIONS,tuple(m for m in MARKERS if m!=marker))
    def test_verify_profile(self):
        line=BASE+' cephzapmem=1';verify.validate_args(line,zap_memory=True)
        for value in (-1,0,2):
            with self.assertRaises(AssertionError):verify.validate_args(BASE+f' cephzapmem={value}',zap_memory=True)
        for flag in ('cephsoc','cephsocdelay','cephscm','cephmem','cephboost','cephgx','cephfuse','cepharc'):
            for value in (0,1,2):
                with self.assertRaises(AssertionError):verify.validate_args(line+f' {flag}={value}',zap_memory=True)
        for key in ('touch_only','soc_only','soc_fuse','soc_fuse_arc','all_probes','scm_info','soc_delay','memory_test'):
            with self.assertRaises(AssertionError):verify.validate_args(line,zap_memory=True,**{key:True})
        for key in ('touch_only','soc_only','all_probes'):
            with self.assertRaises(AssertionError):verify.validate_args(line,**{key:True})
        with self.assertRaises(AssertionError):verify.validate_args(BASE,zap_memory=True)
        with self.assertRaises(AssertionError):verify.validate_args(line+' cephzapmem=1',zap_memory=True)
if __name__=='__main__':unittest.main()
