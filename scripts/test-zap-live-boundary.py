#!/usr/bin/env python3
"""Accept the authenticated ZAP binding and allocation alias boundary locally.

This uses the original A640 ZAP and SQE files plus host IOKit doubles. It does
not access a phone, invoke SMC/PAS, unhalt CP or execute GPU code.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
FIRMWARE = Path('/home/yangqi/android/vendor/xiaomi/cepheus/proprietary/vendor/firmware')
SOURCES = (
    'xnu/cepheus_gpu_memory.cpp',
    'xnu/cepheus_zap_memory_private.h',
    'xnu/cepheus_zap_resource_evidence.h',
    'xnu/test_zap_resource_evidence.c',
    'xnu/cepheus_resource_owner_provider.h',
    'xnu/test_resource_owner_provider.c',
    'tests/test_zap_pas.cpp',
    'tests/test_zap_memory.cpp',
    'scripts/verify-gpu-kernel.py',
    'scripts/test-zap-live-boundary.py',
)
SANITIZER_MARKERS = (
    'runtime error:',
    'ERROR: AddressSanitizer',
    'SUMMARY: AddressSanitizer',
    'SUMMARY: UndefinedBehaviorSanitizer',
)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--zap', type=Path, default=FIRMWARE/'a640_zap.elf')
    parser.add_argument('--sqe', type=Path, default=FIRMWARE/'a630_sqe.fw')
    args = parser.parse_args()
    out = args.output.resolve()
    if out.exists():
        parser.error('output exists; preserve earlier evidence')
    zap = args.zap.resolve()
    sqe = args.sqe.resolve()
    if digest(zap) != '74a948e84f0aac830b314ed05d26006b3571c9a3f90600c708ca76c25612660d':
        parser.error('unexpected A640 ZAP artifact')
    if not sqe.is_file():
        parser.error('missing original SQE artifact')
    out.mkdir(parents=True)
    source_hashes = {name: digest(ROOT/name) for name in SOURCES}
    receipt = {
        'time_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        'source_sha256': source_hashes,
        'inputs': {
            'zap': {'path': str(zap), 'bytes': zap.stat().st_size, 'sha256': digest(zap)},
            'sqe': {'path': str(sqe), 'bytes': sqe.stat().st_size, 'sha256': digest(sqe)},
        },
        'scope': 'host IOKit doubles and real firmware bytes; no phone, SMC/PAS, CP unhalt or GPU execution',
        'phone_operations': False,
        'smc_executed': False,
        'pas_executed': False,
        'cp_unhalted': False,
        'gpu_execution': False,
        'steps': [],
        'result': 'RUNNING',
    }

    def save():
        (out/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')

    def run(label, command, env=None, timeout=180):
        log = out/(label+'.log')
        with log.open('w') as stream:
            result = subprocess.run(command, cwd=ROOT, env=env, stdout=stream,
                                    stderr=subprocess.STDOUT, timeout=timeout)
        body = log.read_text(errors='replace')
        reports = [marker for marker in SANITIZER_MARKERS if marker in body]
        code = result.returncode or int(bool(reports))
        receipt['steps'].append({
            'label': label,
            'command': [str(value) for value in command],
            'exit_code': code,
            'sanitizer_reports': reports,
            'log': log.name,
            'sha256': digest(log),
        })
        receipt['result'] = 'FAIL' if code else 'RUNNING'
        save()
        print(label, 'FAIL' if code else 'PASS', flush=True)
        if code:
            print(body[-8000:])
            raise SystemExit(code)

    env = os.environ.copy()
    env['CEPHEUS_ZAP_ELF'] = str(zap)
    env['CEPHEUS_GPU_SANITIZERS'] = 'address,undefined'
    env['ASAN_OPTIONS'] = 'detect_leaks=0:halt_on_error=1'
    env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
    run('zap-pas', ['bash', 'scripts/test-zap-pas-host.sh'], env)
    run('zap-memory', ['bash', 'scripts/test-zap-memory-host.sh'], env)
    run('cp-build-pool', ['bash', 'scripts/test-cp-build-pool.sh', str(sqe),
                          str(out/'cp-build-pool')], env)
    if source_hashes != {name: digest(ROOT/name) for name in SOURCES}:
        receipt['result'] = 'FAIL'
        receipt['source_unchanged'] = False
        save()
        raise SystemExit('source changed during acceptance')
    receipt['source_unchanged'] = True
    receipt['result'] = 'PASS'
    save()
    print('ZAP live-boundary local acceptance PASS:', out/'receipt.json')


if __name__ == '__main__':
    sys.exit(main())
