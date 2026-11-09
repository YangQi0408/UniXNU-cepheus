#!/usr/bin/env python3
"""Negative and complete pair fixtures; no native SMC is executed."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest


def load(name,file):
    spec=importlib.util.spec_from_file_location(name,Path(__file__).with_name(file))
    mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod);return mod


pair=load('pair','analyze-scm-pair-log.py')
fixture=load('fixture','test-scm-query-log.py')


def transcript(uefi_supported=0,xnu_supported=0,pairs=None,error=None,convention=2,available=31):
    pairs=pairs or fixture.normal_pairs()
    pairs=[(i.copy(),o.copy()) for i,o in pairs]
    if error is None:
        pairs[-1][1][1]=uefi_supported;error=0 if uefi_supported else -1
    count=len(pairs)
    header=struct.pack('<QIIIIiIIIIIQQQQIIQ',0x31464f484d435343,1,2656,1,count,
        error,convention,available,uefi_supported,1,0,4,0x80000000,100,200,1,1,0)
    raw=header+b''.join(struct.pack('<19QiI',*i,*o,4,0x80000000,101+n,0,1)
                        for n,(i,o) in enumerate(pairs))+bytes(160*(16-count))
    checksum=14695981039346656037
    for value in raw:checksum=((checksum^value)*1099511628211)&((1<<64)-1)
    lines=[pair.SCHEDULED,
        f'CEPHEUS SCM PAIR: UEFI e={error} convention={convention} available={available:#x} supported={uefi_supported} calls={count} ebs=1 checksum={checksum:#x}',
        'CEPHEUS SCM PAIR: UEFI el=0x4 mpidr=0x80000000 begin=0x64 end=0xc8']
    for n,(i,o) in enumerate(pairs):
        lines += [f'CEPHEUS SCM PAIR: UEFI call={n} el=0x4 mpidr=0x80000000 ticks={101+n:#x} invoke=0',
            f'CEPHEUS SCM PAIR: UEFI call={n} in='+fixture.registers(i),
            f'CEPHEUS SCM PAIR: UEFI call={n} out='+fixture.registers(o)]
    lines += [pair.BEGIN]
    xnu=fixture.normal_pairs();xnu[-1][1][1]=xnu_supported
    xerror=0 if xnu_supported else -1
    text=fixture.transcript(xnu,error=xerror,supported=xnu_supported).splitlines()
    tick=300
    for line in text[1:-1]:
        lines.append(line)
        if 'REGS: in=' in line:
            lines.append(f'CEPHEUS SCM INFO CPU: el=0x4 mpidr=0x80000100 ticks={tick:#x}')
            tick+=1
    lines.append(f'CEPHEUS SCM PAIR: end uefi_supported={uefi_supported} xnu_supported={xnu_supported} error={xerror} calls=7; no GPU execution')
    return '\n'.join(lines)+'\n'


class PairLog(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();root=Path(cls.temp.name)
        cls.executable=pair.query.compile_replayer(root)
        cls.validate=staticmethod(pair.compile_validator(root))

    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()

    def check(self,text):return pair.analyze(text,self.executable,self.validate)

    def test_independent_boolean_results(self):
        for uefi in (0,1):
            for xnu in (0,1):
                result=self.check(transcript(uefi,xnu))
                self.assertEqual(result['status'],'PAIR_COMPLETE',result)
                self.assertIs(result['uefi_pas13'],bool(uefi))
                self.assertIs(result['xnu_pas13'],bool(xnu))
                self.assertFalse(result['admission_changed'])

    def test_each_missing_query_record_prevents_completion(self):
        lines=transcript().splitlines()
        for n in range(1,len(lines)):
            result=self.check('\n'.join(lines[:n]+lines[n+1:]))
            self.assertNotEqual(result['status'],'PAIR_COMPLETE',(n,result))

    def test_missing_scheduled_keeps_timing_unknown(self):
        result=self.check(transcript().split('\n',1)[1])
        self.assertEqual(result['status'],'PAIR_COMPLETE')
        self.assertFalse(result['scheduled_record_seen'])
        self.assertFalse(result['logger_delay_verified'])

    def test_duplicate_every_record(self):
        lines=transcript().splitlines()
        for n in range(len(lines)):
            self.assertNotEqual(self.check('\n'.join(lines[:n]+[lines[n]]+lines[n:]))['status'],'PAIR_COMPLETE',n)

    def test_checksum_ids_and_summary_tampering(self):
        text=transcript()
        for old,new in [('ebs=1','ebs=4'),('checksum=0x','checksum=0x1'),
            ('in=0xc2000601','in=0xc2000205'),('available=0x1f','available=0xf'),
            ('uefi_supported=0','uefi_supported=1'),('XNU begin;','XNU begin; changed'),
            ('mpidr=0x80000000','mpidr=0x180000000')]:
            self.assertNotEqual(self.check(text.replace(old,new,1))['status'],'PAIR_COMPLETE')

    def test_cpu_context_order_and_bounds(self):
        text=transcript()
        for old,new in [('INFO CPU: el=0x4','INFO CPU: el=0x8'),
            ('ticks=0x12c','ticks=0x1'),('ticks=0x12c','ticks=0x10000000000000000'),
            ('begin=0x64 end=0xc8','begin=0xc8 end=0x64')]:
            self.assertNotEqual(self.check(text.replace(old,new))['status'],'PAIR_COMPLETE')

    def test_busy_uefi_outcome_unknown(self):
        pairs=fixture.normal_pairs()[:1];pairs[0][1][0]=(1<<64)-12;pairs[0][1][1]=0
        result=self.check(transcript(pairs=pairs,error=-1,convention=0,available=0))
        self.assertEqual(result['status'],'PAIR_COMPLETE',result)
        self.assertIsNone(result['uefi_pas13'])

    def test_fallback_and_resume(self):
        pairs=fixture.normal_pairs();pairs[0][1][0]=(1<<64)-1;pairs[0][1][1]=0
        pairs.insert(1,([0x82000601,1,0x82000601,0,0,0,0,0],[0,1,0,0,0,0,0,0]))
        for incoming,_ in pairs[2:]:incoming[0]&=~0x40000000
        self.assertEqual(self.check(transcript(1,0,pairs=pairs,convention=1))['status'],'PAIR_COMPLETE')
        pairs=fixture.normal_pairs();incoming,outgoing=pairs[-1]
        interrupt=[1,0,0,0,0,0,0xabcd,0]
        pairs[-1]=(incoming,interrupt);pairs.append((interrupt.copy(),outgoing))
        self.assertEqual(self.check(transcript(1,0,pairs=pairs))['status'],'PAIR_COMPLETE')

    def test_refusals_and_unexpected_records(self):
        self.assertEqual(self.check('CEPHEUS SCM PAIR: invalid handoff; no XNU query')['status'],'INCOMPLETE')
        for extra in ('CEPHEUS SCM PAS REGS: in=0','CEPHEUS SCM INFO: unexpected'):
            self.assertNotEqual(self.check(transcript()+extra)['status'],'PAIR_COMPLETE')
        self.assertEqual(self.check('')['status'],'INCOMPLETE')


if __name__=='__main__':unittest.main()
