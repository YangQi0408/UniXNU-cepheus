#!/usr/bin/env python3
"""Prepare pinned kernel patches and run build prerequisites locally.

Archives the pinned ios-sweet patch tree into a fresh output directory. Source
trees are read-only inputs; this command never uploads or accesses a phone.
It does not build the complete XNU kernel.
"""
import argparse
import datetime
import hashlib
import io
import json
import os
from pathlib import Path
import subprocess
import tarfile
import sys

ROOT = Path(__file__).resolve().parents[1]
SWEET = '45263bee51df4a0f55cdf9c649af886399488ddb'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--ios-sweet', type=Path,
                        default=ROOT.parent/'inputs/ios-sweet-ordered-touch')
    parser.add_argument('--xnu-reference', type=Path,
                        default=ROOT.parent/'kernel-test/xnu')
    args = parser.parse_args()
    out = args.output.resolve()
    source = args.ios_sweet.resolve()
    if out.exists():
        parser.error('output exists; preserve the earlier receipt')
    head = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    if head != SWEET:
        parser.error('ios-sweet must be pinned at ' + SWEET)
    reference = args.xnu_reference.resolve()
    if not (reference/'bsd/conf/files').is_file():
        parser.error('missing XNU reference bsd/conf/files')
    archive = subprocess.check_output(['git', '-C', str(source), 'archive', SWEET, 'xnu/patches'])
    out.mkdir(parents=True)
    staged = out/'ios-sweet'
    staged.mkdir()
    generated = out/'generated'
    generated.mkdir()
    with tarfile.open(fileobj=io.BytesIO(archive)) as files:
        files.extractall(staged, filter='data')
    env = os.environ.copy()
    for key in ('CEPHEUS_ZAP_ELF', 'CEPHEUS_LIVE_FDT', 'CEPHEUS_GPU_HOST_ASAN',
                'CEPHEUS_GPU_SANITIZERS', 'ASAN_OPTIONS'):
        env.pop(key, None)
    env['CEPHEUS_XNU_REFERENCE'] = str(reference)
    steps = [
        ['bash', 'scripts/build-kernel-patches.sh', str(staged)],
        ['python3', 'scripts/prepare-performance-patch.py', str(staged/'xnu/patches'),
         str(generated/'0083-cepheus-performance.patch')],
        ['python3', 'scripts/prepare-touch-power-patch.py', str(staged/'xnu/patches'),
         str(generated/'0084-cepheus-touch-power.patch')],
        ['bash', 'scripts/check-kernel-sources.sh', str(staged/'xnu/patches')],
    ]
    receipt = {
        'time_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        'dirty': bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT)),
        'ios_sweet_commit': SWEET, 'xnu_reference': str(reference),
        'full_xnu_build': False, 'phone_operations': False, 'steps': [],
        'source_sha256': {str(p.relative_to(ROOT)): sha(p) for folder in ('xnu', 'scripts')
                          for p in sorted((ROOT/folder).glob('*')) if p.is_file()},
    }
    for index, command in enumerate(steps):
        log = out/f'{index:02d}.log'
        with log.open('wb') as stream:
            try:
                code = subprocess.run(command, cwd=ROOT, env=env, stdout=stream,
                                      stderr=subprocess.STDOUT, timeout=900).returncode
            except subprocess.TimeoutExpired:
                code = 124
        receipt['steps'].append({'command': command, 'exit_code': code, 'log': log.name})
        receipt['result'] = 'FAIL' if code else 'RUNNING'
        (out/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
        print(f'{index+1}/{len(steps)}: {"FAIL" if code else "PASS"} {log}', flush=True)
        if code:
            print(log.read_text(errors='replace')[-6000:])
            return code
    receipt['patch_sha256'] = {p.name: sha(p) for p in sorted((staged/'xnu/patches').iterdir()) if p.is_file()}
    receipt['generated_sha256'] = {p.name: sha(p) for p in sorted(generated.iterdir()) if p.is_file()}
    receipt['result'] = 'PASS'
    (out/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
    print('Local kernel preflight PASS:', out/'receipt.json')
    return 0


if __name__ == '__main__':
    sys.exit(main())
