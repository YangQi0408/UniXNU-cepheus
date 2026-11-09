#!/usr/bin/env python3
"""Verify saved UEFI/XNU query evidence using the production RAM policy."""
import argparse
import ctypes
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('query', Path(__file__).with_name('analyze-scm-query-log.py'))
query = importlib.util.module_from_spec(spec)
spec.loader.exec_module(query)
HEX = r'(0x[0-9a-f]+|0)'
UEFI = re.compile(r'CEPHEUS SCM PAIR: UEFI e=(-?\d+) convention=(\d+) available='+HEX+
                  r' supported=([01]) calls=(\d+) ebs=(\d+) checksum='+HEX)
CONTEXT = re.compile(r'CEPHEUS SCM PAIR: UEFI el='+HEX+r' mpidr='+HEX+r' begin='+HEX+r' end='+HEX)
CALL = re.compile(r'CEPHEUS SCM PAIR: UEFI call=(\d+) el='+HEX+r' mpidr='+HEX+r' ticks='+HEX+r' invoke=(-?\d+)')
REGS = re.compile(r'CEPHEUS SCM PAIR: UEFI call=(\d+) (in|out)=((?:0x[0-9a-f]+|0)(?::(?:0x[0-9a-f]+|0)){7})')
CPU = re.compile(r'CEPHEUS SCM INFO CPU: el='+HEX+r' mpidr='+HEX+r' ticks='+HEX)
END = re.compile(r'CEPHEUS SCM PAIR: end uefi_supported=([01]) xnu_supported=([01]) error=(-?\d+) calls=(\d+); no GPU execution')
SCHEDULED = 'CEPHEUS SCM PAIR: scheduled delay_s=90'
BEGIN = 'CEPHEUS SCM PAIR: XNU begin; fixed INFO/PAS13 queries only'


def compile_validator(directory):
    source = directory/'handoff.c'
    source.write_text('#include <string.h>\n#include "cepheus_scm_handoff.h"\n'
                      'int validate(const void *p) { struct ceph_scm_handoff h; '
                      'memcpy(&h,p,sizeof(h)); return ceph_scm_handoff_valid(&h); }\n')
    library = directory/'handoff.so'
    subprocess.run(['cc', '-shared', '-fPIC', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=undefined', '-I', str(ROOT/'xnu'), str(source), '-o', str(library)],
                   check=True, capture_output=True)
    validate = ctypes.CDLL(str(library)).validate
    validate.argtypes = [ctypes.c_void_p]
    return validate


def analyze(text, executable, validate):
    lines = [line for line in text.splitlines() if line.startswith('CEPHEUS SCM')]
    result = {'status': 'INCOMPLETE', 'uefi_pas13': None, 'xnu_pas13': None,
              'scope': 'Saved query evidence only; no PAS authentication or GPU execution',
              'logger_delay_verified': False, 'log_origin_authenticated': False}
    def reject(reason, status='REJECTED'):
        return {**result, 'status': status, 'reason': reason}
    if not lines:
        return reject('No pair records', 'INCOMPLETE')
    if len(lines)>150 or lines.count(BEGIN)>1 or lines.count(SCHEDULED)>1:
        return reject('Oversized or duplicate pair')
    scheduled = bool(lines and lines[0] == SCHEDULED)
    if scheduled:
        lines.pop(0)
    # A missing early UDP record cannot invalidate later complete query data,
    # but the fixed delay remains unverified by the receiving host.
    result['scheduled_record_seen'] = scheduled
    if any('refused profile' in l or 'no XNU query' in l for l in lines):
        return reject('Pair refused or handoff unavailable', 'INCOMPLETE')
    if len(lines)<3:
        return reject('Missing UEFI context or XNU stage', 'INCOMPLETE')
    summary, context = UEFI.fullmatch(lines[0]), CONTEXT.fullmatch(lines[1])
    if not summary or not context:
        return reject('Missing or malformed UEFI header', 'INCOMPLETE')
    error, convention, available, supported, count, ebs, checksum = [int(v, 0) for v in summary.groups()]
    el, mpidr, begin, end = [int(v, 0) for v in context.groups()]
    if not 1<=count<=16 or error not in (0,-1) or not 1<=ebs<=3 or convention>2 or available>31:
        return reject('UEFI header bounds')
    if any(v<0 or v>=1<<64 for v in (el,mpidr,begin,end,checksum)) or end<begin:
        return reject('UEFI context bounds')
    if len(lines)<3+count*3:
        return reject('Missing UEFI calls or XNU begin', 'INCOMPLETE')
    header = struct.pack('<QIIIIiIIIIIQQQQIIQ', 0x31464f484d435343,1,2656,1,count,
                         error,convention,available,supported,1,0,el,mpidr,begin,end,ebs,1,checksum)
    payload = bytearray(header)
    pairs = []
    invokes = []
    previous = begin
    for i in range(count):
        offset = 2+i*3
        cpu = CALL.fullmatch(lines[offset])
        incoming, outgoing = REGS.fullmatch(lines[offset+1]), REGS.fullmatch(lines[offset+2])
        if not cpu or not incoming or not outgoing:
            return reject('Incomplete or malformed UEFI call', 'INCOMPLETE')
        index, call_el, call_mpidr, ticks, invoke = [int(v,0) for v in cpu.groups()]
        if index!=i or incoming[1]!=str(i) or outgoing[1]!=str(i) or incoming[2]!='in' or outgoing[2]!='out':
            return reject('Duplicate or reordered UEFI call')
        regs = [[int(v,16) for v in match[3].split(':')] for match in (incoming,outgoing)]
        if any(v>=1<<64 for v in (*regs[0],*regs[1],call_el,call_mpidr,ticks)) or not -(1<<31)<=invoke<1<<31:
            return reject('UEFI record bounds')
        if not previous<=ticks<=end:
            return reject('UEFI counter ordering')
        previous = ticks
        payload.extend(struct.pack('<19QiI', *regs[0],*regs[1],call_el,call_mpidr,ticks,invoke,1))
        pairs.append(regs)
        invokes.append(invoke)
    payload.extend(bytes(160*(16-count)))
    if len(payload)!=2656 or validate(ctypes.create_string_buffer(bytes(payload))):
        return reject('UEFI checksum or production policy mismatch')
    position = 2+count*3
    if lines[position]!=BEGIN:
        return reject('Missing XNU begin or extra UEFI records')
    if not END.fullmatch(lines[-1]):
        return reject('Missing paired end', 'INCOMPLETE')
    ending = [int(v) for v in END.fullmatch(lines[-1]).groups()]
    xnu_lines = lines[position+1:-1]
    cpus=[]
    stripped=[]
    n=0
    while n<len(xnu_lines) and query.REGS.fullmatch(xnu_lines[n]):
        incoming=query.REGS.fullmatch(xnu_lines[n])
        if incoming[1]!='in' or n+2>=len(xnu_lines):
            return reject('Missing XNU CPU/return', 'INCOMPLETE')
        cpu=CPU.fullmatch(xnu_lines[n+1]);outgoing=query.REGS.fullmatch(xnu_lines[n+2])
        if not cpu or not outgoing or outgoing[1]!='out':
            return reject('Missing or reordered XNU CPU/return', 'INCOMPLETE')
        values=[int(v,0) for v in cpu.groups()]
        if values[0]!=4 or any(v>=1<<64 for v in values) or values[2]<previous:
            return reject('XNU CPU context or counter ordering')
        previous=values[2];cpus.append(dict(zip(('el','mpidr','ticks'),values)))
        stripped.extend((xnu_lines[n],xnu_lines[n+2]));n+=3
    if len(xnu_lines[n:])!=2 or not query.SUMMARY.fullmatch(xnu_lines[n]) or not query.QUERY7.fullmatch(xnu_lines[n+1]):
        return reject('Missing XNU summary/QUERY7 or unexpected records', 'INCOMPLETE')
    claimed=query.SUMMARY.fullmatch(xnu_lines[n])
    xerror,xsupported,xcalls=int(claimed[1]),int(claimed[4]),int(claimed[5])
    synthetic='\n'.join([query.BEGIN,*stripped,*xnu_lines[n:],
        f'CEPHEUS SCM INFO: end error={xerror} calls={xcalls} supported={xsupported}; no power or GPU startup'])
    xnu=query.analyze(synthetic,executable)
    if xnu['status']!='TRANSCRIPT_COMPLETE':
        return reject('XNU: '+xnu['reason'],xnu['status'])
    if ending != [supported,xsupported,xerror,xcalls] or len(cpus)!=xcalls:
        return reject('Pair end contradicts summaries')
    # Policy validation above proves a terminal handoff, including busy/error
    # cases. Only completed boolean PAS returns establish its query outcome.
    uefi_bool=None
    for i,(incoming,outgoing) in enumerate(pairs):
        if incoming[0] in (0x42000207,0x02000207) and incoming[1:3]==[1,13]:
            while outgoing[0]&0xffffffff==1 and i+1<len(pairs):
                i+=1;outgoing=pairs[i][1]
            if invokes[i]==0 and outgoing[0]&0xffffffff==0 and outgoing[1] in (0,1):
                uefi_bool=bool(outgoing[1])
    return {**result,'status':'PAIR_COMPLETE','reason':'Both fixed query transcripts verified',
            'uefi_pas13':uefi_bool,'xnu_pas13':xnu['pas13_supported'],
            'uefi_calls':count,'xnu_calls':xcalls,'uefi_error':error,'xnu_error':xerror,
            'uefi_mpidr':mpidr,'xnu_cpus':cpus,'checksum_verified':True,
            'admission_changed':False,'gpu_execution':False}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('log',type=Path);p.add_argument('--output',required=True,type=Path)
    a=p.parse_args()
    if a.output.exists():p.error('output exists')
    if a.log.stat().st_size>32*1024*1024:p.error('log exceeds 32MiB')
    raw=a.log.read_bytes()
    with tempfile.TemporaryDirectory() as temp:
        root=Path(temp)
        result=analyze(raw.decode(errors='replace'),query.compile_replayer(root),compile_validator(root))
    result['log_sha256']=hashlib.sha256(raw).hexdigest()
    result['source_sha256']={n:hashlib.sha256((ROOT/'xnu'/n).read_bytes()).hexdigest()
                            for n in ('cepheus_scm_handoff.h','cepheus_scm_discovery.h')}
    a.output.write_text(json.dumps(result,indent=2)+'\n')
    print(result['status']+': '+result['reason'])
    return {'PAIR_COMPLETE':0,'INCOMPLETE':2,'REJECTED':1}[result['status']]


if __name__=='__main__':raise SystemExit(main())
