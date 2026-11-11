#!/usr/bin/env python3
"""Build firmware and package a verified pair candidate locally from an XNU kernel.

Needs EDK2/Clang and mtools on PATH. A local build container may supply mtools.
The provided kernel must already be a compiled, verified pair XNU artifact.
"""
import argparse
import datetime
import hashlib
import json
import importlib.util
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('smem_local_evidence',ROOT/'scripts/smem-packaging-evidence.py')
smem=importlib.util.module_from_spec(spec);spec.loader.exec_module(smem)


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
    p.add_argument('--rpmh-tail',action='store_true',help='isolated diagnostic, reuse the complete local loader/core builds')
    p.add_argument('--smem-map',action='store_true',help='isolated mapping handoff, reuse verified local DarwinBoot')
    p.add_argument('--firmware-build',type=Path,help='complete tail or SMEM loader build to verify and reuse')
    p.add_argument('--core-build',type=Path,help='complete tail core build to verify and reuse')
    a=p.parse_args();out=a.output.resolve();kernel=a.kernel.resolve();baseline=a.baseline.resolve()
    if out.exists():p.error('output exists; preserve earlier build results')
    if not kernel.is_file() or not baseline.is_file():p.error('missing kernel or baseline')
    if a.smem_map:
        smem.require(__debug__ and not a.rpmh_tail and a.firmware_build and not a.core_build,
                     'SMEM requires isolated mode and complete local loader build, no core replacement')
        smem.validate(kernel,a.kernel_receipt)
    verification=json.loads(a.kernel_receipt.read_text())
    if verification.get('status')!='PASS' or verification.get('kernel_sha256')!=identity(kernel)['sha256']:
        p.error('kernel verification receipt does not match a passed artifact')
    if verification.get('native_cpu_capture_and_smc_verified') is not True:
        p.error('compiled CPU/SMC callback verification is required')
    if a.rpmh_tail:
        if not a.firmware_build or not a.core_build:p.error('tail requires both complete local firmware builds')
        if verification.get('native_tail_hook_and_caller_verified') is not True or verification.get('tail_abi_source_verified') is not True:
            p.error('compiled tail caller/helper and source ABI verification required')
    elif not a.smem_map and (a.firmware_build or a.core_build):p.error('build reuse options require --rpmh-tail or --smem-map')
    data=kernel.read_bytes()
    if data[:8]!=bytes.fromhex('cffaedfe0c000001') or len(data)<2000000:
        p.error('kernel must be substantial ARM64 Mach-O')
    markers=smem.NATIVE.MARKERS if a.smem_map else (b'CEPHEUS SCM PAIR: UEFI',b'CEPHEUS SCM PAIR: XNU begin',
                                                   b'CEPHEUS SCM PAIR: end',b'CEPHEUS SCM %s CPU:')
    if any(s not in data for s in markers):p.error('kernel lacks selected diagnostics')
    out.mkdir(parents=True)
    efi=out/'firmware/build/DarwinBoot/RELEASE_CLANGPDB/AARCH64/DarwinBoot.efi'
    fd=out/'fv/scm-pair.fd';fv_receipt=out/'fv/verification.json';image=out/'boot-scm-pair.img'
    core=None
    if a.rpmh_tail:
        efi=a.firmware_build.resolve()/'build/DarwinBoot/RELEASE_CLANGPDB/AARCH64/DarwinBoot.efi'
        core=a.core_build.resolve()/'build/PoolLifetimeCore/RELEASE_CLANGPDB/AARCH64/DxeCore.efi'
        image=out/'boot-rpmh-tail.img'
        if not efi.is_file() or not core.is_file():p.error('missing compiled EFI artifact')
    if a.smem_map:
        efi=a.firmware_build.resolve()/'build/DarwinBoot/RELEASE_CLANGPDB/AARCH64/DarwinBoot.efi'
        image=out/'boot-smem-map.img'
        if not efi.is_file():p.error('missing compiled SMEM loader')
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
    if a.rpmh_tail:
        steps=[
            ['python3',str(ROOT/'scripts/test-rpmh-tail-loader.py'),'--firmware-build',str(a.firmware_build.resolve()),
             '--output',str(out/'loader-verification.json')],
            ['python3',str(ROOT/'scripts/test-rpmh-tail-core.py'),'--firmware-build',str(a.core_build.resolve()),
             '--output',str(out/'core-verification.json')],
            steps[1]+['--core',str(core)],
            steps[2]+['--rpmh-tail'],
            steps[3]+['--core',str(core)],
        ]
        receipt.update(profile='rpmh-exit-tail',core=identity(core),gpu_execution=False)
    if a.smem_map:
        steps=[
            ['python3',str(ROOT/'scripts/verify-smem-handoff-firmware.py'),'--build',str(a.firmware_build.resolve()),
             '--repository',str(ROOT.parent/'upstream-sync/unixnu'),'--uefi',str(ROOT.parent/'uefi'),
             '--output',str(out/'loader-verification.json')],
            steps[1],
            ['python3',str(ROOT/'scripts/make-scm-pair-boot.py'),str(baseline),str(kernel),str(fd),str(image),
             '--smem-map','--kernel-receipt',str(a.kernel_receipt.resolve())],
            ['python3',str(ROOT/'scripts/verify-scm-pair-boot.py'),str(image),str(baseline),str(kernel),
             str(fd),str(efi),str(fv_receipt),str(out/'boot-verification.json'),
             '--smem-map','--kernel-receipt',str(a.kernel_receipt.resolve())],
            ['python3',str(ROOT/'scripts/verify-smem-packaged-adt.py'),'--baseline',str(baseline),
             '--build',str(a.firmware_build.resolve()),'--image',str(image),
             '--output',str(out/'adt-verification.json')],
        ]
        receipt.update(profile='smem-map-handoff',gpu_execution=False,smem_bytes_acquired=False,
                       smem_access_authorized=False,firmware_execution=False)
    for index,command in enumerate(steps):
        if a.container and index>=len(steps)-(3 if a.smem_map else 2):
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
        print(f'{index+1}/{len(steps)} {"FAIL" if code else "PASS"}: {log}',flush=True)
        if code:
            print(log.read_text(errors='replace')[-5000:]);return code
    receipt.update(result='PASS',image=identity(image),efi=identity(efi),fd=identity(fd))
    (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print('Verified local candidate:',image)
    return 0


if __name__=='__main__':sys.exit(main())
