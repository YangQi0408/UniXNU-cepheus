#!/usr/bin/env python3
"""Build firmware and package a verified pair candidate locally from an XNU kernel.

Needs EDK2/Clang and mtools on PATH. A local build container may supply mtools.
The provided kernel must already be a compiled, verified pair XNU artifact.
"""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]


def identity(path):
    return {'path':str(path),'bytes':path.stat().st_size,
            'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--kernel',required=True,type=Path)
    p.add_argument('--kernel-receipt',required=True,type=Path,
                   help='successful verify-ci-scm-pair.py receipt for the exact kernel')
    p.add_argument('--output',required=True,type=Path)
    p.add_argument('--baseline',type=Path,default=ROOT.parent/'dist/boot-cepheus-zapmem-b5006b5.img')
    p.add_argument('--container',help='local image providing Python and mtools; packaging runs network-disabled')
    a=p.parse_args();out=a.output.resolve();kernel=a.kernel.resolve();baseline=a.baseline.resolve()
    if out.exists():p.error('output exists; preserve earlier build results')
    if not kernel.is_file() or not baseline.is_file():p.error('missing kernel or baseline')
    verification=json.loads(a.kernel_receipt.read_text())
    if verification.get('status')!='PASS' or verification.get('kernel_sha256')!=identity(kernel)['sha256']:
        p.error('kernel verification receipt does not match a passed artifact')
    if verification.get('native_cpu_capture_and_smc_verified') is not True:
        p.error('compiled CPU/SMC callback verification is required')
    data=kernel.read_bytes()
    if data[:8]!=bytes.fromhex('cffaedfe0c000001') or len(data)<2000000:
        p.error('kernel must be substantial ARM64 Mach-O')
    if any(s not in data for s in (b'CEPHEUS SCM PAIR: UEFI',b'CEPHEUS SCM PAIR: XNU begin',
                                  b'CEPHEUS SCM PAIR: end',b'CEPHEUS SCM %s CPU:')):
        p.error('kernel lacks paired diagnostics')
    out.mkdir(parents=True)
    efi=out/'firmware/build/DarwinBoot/RELEASE_CLANGPDB/AARCH64/DarwinBoot.efi'
    fd=out/'fv/scm-pair.fd';fv_receipt=out/'fv/verification.json';image=out/'boot-scm-pair.img'
    receipt={'time_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
             'kernel':identity(kernel),'kernel_verification':identity(a.kernel_receipt.resolve()),
             'baseline':identity(baseline),
             'commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
             'steps':[],'full_xnu_compiled_here':False,'phone_operations':False}
    steps=[
        ['bash',str(ROOT/'scripts/build-scm-pair-firmware.sh'),str(out/'firmware')],
        [sys.executable,str(ROOT/'scripts/repack-scm-pair-fv.py'),
         str(ROOT.parent/'uefi/Build/SurfaceDuo1Pkg/RELEASE_CLANGPDB/FV'),str(efi),
         str(ROOT.parent/'uefi/MU_BASECORE/BaseTools/Source/C/bin'),str(out/'fv')],
        ['python3',str(ROOT/'scripts/make-scm-pair-boot.py'),str(baseline),str(kernel),str(fd),str(image)],
        ['python3',str(ROOT/'scripts/verify-scm-pair-boot.py'),str(image),str(baseline),str(kernel),
         str(fd),str(efi),str(fv_receipt),str(out/'boot-verification.json')],
    ]
    for index,command in enumerate(steps):
        if a.container and index>=2:
            command=['docker','run','--rm','--network','none','--user',
                     subprocess.check_output(['id','-u'],text=True).strip()+':'+
                     subprocess.check_output(['id','-g'],text=True).strip(),
                     '-v',str(ROOT.parent)+':'+str(ROOT.parent),
                     '-w',str(ROOT),a.container,*command]
        log=out/f'{index:02d}.log'
        with log.open('wb') as stream:
            try:code=subprocess.run(command,cwd=ROOT,stdout=stream,stderr=subprocess.STDOUT,timeout=900).returncode
            except subprocess.TimeoutExpired:code=124
        receipt['steps'].append({'command':command,'exit_code':code,'log':log.name})
        receipt['result']='FAIL' if code else 'RUNNING'
        (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
        print(f'{index+1}/4 {"FAIL" if code else "PASS"}: {log}',flush=True)
        if code:
            print(log.read_text(errors='replace')[-5000:]);return code
    receipt.update(result='PASS',image=identity(image),efi=identity(efi),fd=identity(fd))
    (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print('Verified local candidate:',image)
    return 0


if __name__=='__main__':sys.exit(main())
