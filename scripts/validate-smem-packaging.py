#!/usr/bin/env python3
"""Save focused packaging fixture evidence; not a real-kernel boot candidate."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def identity(path):
    return {'path':str(path),'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('baseline','fd','efi','fv-receipt','output','pair-fd','pair-efi','pair-receipt',
                 'tail-fd','tail-efi','tail-receipt','tail-core'):
        parser.add_argument('--'+name,required=True,type=Path)
    parser.add_argument('--container',required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    if out.exists():
        parser.error('preserve prior validation')
    files = {name:value.resolve() for name,value in vars(args).items() if isinstance(value,Path) and name!='output'}
    for file in files.values():
        if not file.is_file():
            parser.error('missing input '+str(file))
    image = subprocess.check_output(['docker','image','inspect',args.container,'--format','{{.Id}}'],text=True).strip()
    sources = [ROOT/'scripts'/name for name in ('smem-packaging-evidence.py','verify-smem-map-kernel.py',
        'make-scm-pair-boot.py','verify-scm-pair-boot.py','repack-scm-pair-fv.py','adt.py',
        'build-scm-pair-local.py','test-smem-packaging.py','test-scm-pair-packaging.py','validate-smem-packaging.py',
        'verify-smem-packaged-adt.py','verify-ci-scm-pair.py','test-ci-kernel-state.py')]
    source_hashes = {str(file.relative_to(ROOT)):identity(file)['sha256'] for file in sources}
    receipt = {'time_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
               'commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
               'scope':'synthetic packaging and local runner mocks only; actual supplied nested FV equality',
               'sources':source_hashes,'inputs':{name:identity(file) for name,file in files.items()},
               'container_id':image,'checks':[],'phone_operations':False,'gpu_execution':False,
               'firmware_execution':False,'full_kernel_verified':False,'boot_candidate_created':False}
    wrapper = ['docker','run','--rm','--network','none','--user',f'{os.getuid()}:{os.getgid()}',
               '-v',f'{ROOT.parent}:{ROOT.parent}','-w',str(ROOT),image,'python3']
    cases = [('smem','test-smem-packaging.py',['baseline','fd','efi','fv_receipt']),
             ('pair-regression','test-scm-pair-packaging.py',['baseline','pair_fd','pair_efi','pair_receipt']),
             ('tail-regression','test-scm-pair-packaging.py',['baseline','tail_fd','tail_efi','tail_receipt'])]
    out.mkdir(parents=True)
    for name,script,keys in cases:
        command = [*wrapper,'scripts/'+script,*[str(files[key]) for key in keys]]
        if name=='tail-regression':
            command += ['--core',str(files['tail_core'])]
        result = subprocess.run(command,cwd=ROOT,capture_output=True,timeout=240)
        log = out/(name+'.log');log.write_bytes(result.stdout+result.stderr)
        receipt['checks'].append({'name':name,'command':command,'exit_code':result.returncode,'log':identity(log)})
        print(name,'PASS' if result.returncode==0 else 'FAIL',flush=True)
    receipt['source_hashes_unchanged'] = source_hashes == {str(file.relative_to(ROOT)):identity(file)['sha256'] for file in sources}
    receipt['input_hashes_unchanged'] = receipt['inputs'] == {name:identity(file) for name,file in files.items()}
    receipt['status'] = 'PASS' if receipt['source_hashes_unchanged'] and receipt['input_hashes_unchanged'] and all(
        check['exit_code']==0 for check in receipt['checks']) else 'FAIL'
    target = out/'receipt.json';target.write_text(json.dumps(receipt,indent=2)+'\n')
    print(receipt['status'],identity(target)['sha256'])
    return 0 if receipt['status']=='PASS' else 1


if __name__=='__main__':
    sys.exit(main())
