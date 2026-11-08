#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('zaplog',Path(__file__).with_name('analyze-zap-memory-log.py'))
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
LINES=['CEPHEUS ZAP TEST: scheduled delay_s=90','CEPHEUS ZAP TEST: begin version=1 cpu_only=1',
       'CEPHEUS ZAP MEM: translated pa=0x120004000 bytes=0x4000 attr=0 first_par=0x120004000 pages=4',
       'CEPHEUS ZAP MEM: translated pa=0x120100000 bytes=0x100000 attr=0x44 first_par=0x4400000120100000 pages=256',
       'CEPHEUS ZAP TEST: pattern=PASS mdt_bytes=16384 image_bytes=1048576 logical_bytes=4096',
       'CEPHEUS ZAP MEM: restored pa=0x120004000 bytes=0x4000 attr=0xff',
       'CEPHEUS ZAP TEST: end result=0 retained_pages=0 cpu_only=1']
class Transcript(unittest.TestCase):
    def status(self,lines):return module.analyze('\n'.join(lines))['status']
    def test_success(self):
        self.assertEqual(self.status(LINES),'PASS')
        self.assertEqual(self.status(['time prefix '+line for line in LINES]),'PASS')
    def test_missing_duplicate_reorder(self):
        for index in range(len(LINES)):
            self.assertEqual(self.status(LINES[:index]+LINES[index+1:]),'NOT_PASS')
            self.assertEqual(self.status(LINES[:index]+[LINES[index]]+LINES[index:]),'NOT_PASS')
        self.assertEqual(self.status(LINES[::-1]),'NOT_PASS')
        self.assertEqual(self.status(LINES+LINES),'NOT_PASS')
        self.assertEqual(self.status([]),'NOT_PASS')
    def test_faults(self):
        text='\n'.join(LINES)
        for old,new in (('result=0','result=-1'),('pattern=PASS','pattern=FAIL'),
                        ('retained_pages=0','retained_pages=4'),('logical_bytes=4096','logical_bytes=1048576'),
                        ('pages=256','pages=255'),('first_par=0x120004000','first_par=0x120004001'),
                        ('first_par=0x120004000','first_par=0x4400000120004000'),
                        ('attr=0 first','attr=0x44 first'),('restored pa=0x120004000','restored pa=0x120008000'),
                        ('pa=0x120100000','pa=0x120104000'),('delay_s=90','delay_s=240')):
            self.assertEqual(module.analyze(text.replace(old,new))['status'],'NOT_PASS',(old,new))
    def test_refused_or_mixed(self):
        for line in ('CEPHEUS ZAP MEM: cache restore failed; allocation retained',
                     'CEPHEUS ZAP TEST: refused mixed or invalid profile','CEPHEUS SCM INFO: begin',
                     'CEPHEUS OSM: sample0','CEPHEUS GPU GX: probe','CEPHEUS ARC: probe'):
            self.assertEqual(self.status(LINES+[line]),'NOT_PASS')
if __name__=='__main__':unittest.main()
