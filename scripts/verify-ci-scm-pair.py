#!/usr/bin/env python3
"""Verify the exact successful CI kernel and native paired callback codegen."""
import argparse
import ast
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[1]


def run(command):return subprocess.check_output(command,cwd=ROOT,text=True,timeout=60)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--run',required=True,type=int);p.add_argument('--commit',required=True)
    p.add_argument('--input',required=True,type=Path);p.add_argument('--log',required=True,type=Path)
    p.add_argument('--output',required=True,type=Path)
    p.add_argument('--rpmh-tail',action='store_true',help='also verify this commit\'s tail ABI, native helper and linked caller')
    p.add_argument('--smem-map',action='store_true',help='verify copied mapping receiver and its linked thread')
    a=p.parse_args();assert not a.output.exists()
    pin=run(['git','rev-parse',a.commit]).strip()
    ci=json.loads(run(['gh','run','view',str(a.run),'--repo','YangQi0408/UniXNU-cepheus',
                       '--json','headSha,status,conclusion,jobs']))
    assert ci['headSha']==pin and ci['status']=='completed' and ci['conclusion']=='success'
    assert all(step['conclusion']=='success' for job in ci['jobs'] for step in job['steps'])
    kernel=a.input/'kernel.macho';data=kernel.read_bytes();sha=hashlib.sha256(data).hexdigest()
    assert sha==(a.input/'SHA256SUMS').read_text().split()[0]
    assert (a.input/'device-commit.txt').read_text().strip()==pin
    assert (a.input/'ios-sweet-commit.txt').read_text().strip()=='45263bee51df4a0f55cdf9c649af886399488ddb'
    source=run(['git','show',pin+':scripts/verify-gpu-kernel.py'])
    main=next(n for n in ast.parse(source).body if isinstance(n,ast.FunctionDef) and n.name=='main')
    sets={'required':set(),'memory_api':set()}
    for node in main.body:
        if isinstance(node,ast.Assign) and len(node.targets)==1 and isinstance(node.targets[0],ast.Name):
            name=node.targets[0].id
            if name in sets:sets[name]=ast.literal_eval(node.value)
        if isinstance(node,ast.AugAssign) and isinstance(node.target,ast.Name) and node.target.id in sets:
            assert isinstance(node.op,ast.BitOr)
            sets[node.target.id]|=ast.literal_eval(node.value)
    nm=run(['llvm-nm',str(kernel)])
    symbols={l.split()[-1] for l in nm.splitlines() if len(l.split())==3 and l.split()[-2].upper()!='U'}
    assert all(names<=symbols for names in sets.values())
    assert not run(['llvm-nm','-u',str(kernel)]).strip()
    source_identity={}
    for path in sorted((ROOT/'xnu').glob('*')):
        if path.is_file() and not path.name.startswith('test_'):
            recorded=subprocess.check_output(['git','show',pin+':xnu/'+path.name],cwd=ROOT)
            assert recorded==path.read_bytes(),path.name
            source_identity[path.name]=hashlib.sha256(recorded).hexdigest()
    if a.rpmh_tail:
        for name in ('cepheus_rpmh_snapshot.h','cepheus_rpmh_tail.h','cepheus_rpmh_tail_handoff.h'):
            recorded=subprocess.check_output(['git','show',pin+':firmware/'+name],cwd=ROOT)
            assert recorded==(ROOT/'firmware'/name).read_bytes(),name
            source_identity['firmware/'+name]=hashlib.sha256(recorded).hexdigest()
        for marker in (b'CEPHEUS RPMH TAIL: begin',b'CEPHEUS RPMH TAIL: raw=',b'CEPHEUS RPMH TAIL: end',b'cepheus-rpmh-tail'):
            assert marker in data,marker
    for marker in (b'CEPHEUS SCM PAIR: UEFI',b'CEPHEUS SCM PAIR: XNU begin',b'CEPHEUS SCM PAIR: end',
        b'cepheus-scm-handoff',b'CEPHEUS SCM %s CPU:',b'CEPHEUS SCM INFO REGS: in=',
        b'CEPHEUS SCM INFO REGS: out=',b'CEPHEUS SCM PAS REGS: in=',b'CEPHEUS SCM PAS REGS: out='):
        assert marker in data,marker
    assembly=run(['llvm-objdump','-d','--no-show-raw-insn',str(kernel)])
    callback=assembly.split('<_ceph_scm_info_invoke>:',1)[1].split('\n\n',1)[0]
    assert callback.count('smc\t#0')==1
    instructions=[l.split(':',1)[1].strip() for l in callback.splitlines() if re.match(r'\s*[0-9a-f]+:',l)]
    index=next(i for i,l in enumerate(instructions) if l=='smc\t#0')
    for line,register in zip(instructions[index-3:index],('CurrentEL','MPIDR_EL1','CNTPCT_EL0')):
        assert line.startswith('mrs\t') and register in line
    assert '_disable_preemption' in callback and '_enable_preemption' in callback
    if a.rpmh_tail:
        tail=assembly.split('<_soc_rpmh_tail>:',1)[1].split('\n\n',1)[0]
        thread=assembly.split('<_soc_thread>:',1)[1].split('\n\n',1)[0]
        assert '_soc_rpmh_tail' in thread
        assert '_SecureDTLookupEntry' in tail and '_SecureDTGetProperty' in tail
        assert '_gpu_probe' in tail or '_ml_io_map' in tail
        assert 'smc\t' not in tail and '_cepheus_gpu_submit' not in tail and '_cepheus_gpu_arc_raise' not in tail
    checks={}
    smem=None
    if a.smem_map:
        smem=json.loads(run(['python3',str(ROOT/'scripts/verify-smem-map-kernel.py'),str(kernel)]))
        assert smem['status']=='PASS_LINKED_SMEM_RECEIVER' and smem['kernel_sha256']==sha
    for script,flags in (('verify-touch-kernel.py',['--ordered-contacts']),('verify-pmap-cache-kernel.py',[])):
        checks[script]=run(['python3',str(ROOT/'scripts'/script),str(kernel),*flags]).strip()
    log=a.log.read_text(errors='replace')
    critical=('0003-','0018-','0028-','0038-','0046-','0058-','0064-','0065-','0067-',
              '0075-','0077-','0078-','0082-','0083-','0084-','0085-','0086-','0087-','0088-')
    assert not any('Skipping patch' in l and any(k in l for k in critical) for l in log.splitlines())
    result={'status':'PASS','run':a.run,'commit':pin,'kernel_sha256':sha,'kernel_bytes':len(data),
        'linked_core_symbols':len(sets['required']),'linked_memory_api_symbols':len(sets['memory_api']),
        'unresolved_symbols':[],'checks':checks,'native_cpu_capture_and_smc_verified':True,
        'ci_staged_source_comparisons_passed':True,'source_sha256':source_identity,
        'build_log_sha256':hashlib.sha256(a.log.read_bytes()).hexdigest(),
        'phone_operations':False,'gpu_execution':False,'run_state':ci}
    if a.rpmh_tail:result.update(tail_abi_source_verified=True,native_tail_hook_and_caller_verified=True)
    if a.smem_map:result.update(smem_map_abi_source_verified=True,native_smem_receiver_and_thread_verified=True,smem_map_verification=smem)
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ('run_state','source_sha256')},indent=2))


if __name__=='__main__':main()
