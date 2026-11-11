#!/usr/bin/env python3
"""Focused local handoff receipt; no boot packaging, device or GPU execution."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('build', 'repository', 'uefi', 'output'):
        parser.add_argument('--' + name, required=True, type=Path)
    args = parser.parse_args()
    out = args.output.resolve()
    if out.exists():
        parser.error('output exists; preserve earlier evidence')
    inputs = ['--repository', str(args.repository.resolve()), '--uefi', str(args.uefi.resolve())]
    native = ['--build', str(args.build.resolve()), *inputs]
    checks = []
    for optimized in (False, True):
        python = [sys.executable, *(['-O'] if optimized else [])]
        suffix = '-opt' if optimized else ''
        for name, extra in (('test-smem-map-handoff', inputs), ('test-smem-handoff-contract', []),
                            ('test-smem-kernel-verifier', []),
                            ('test-smem-handoff-firmware', native),
                            ('verify-smem-handoff-firmware', [*native, '--output', str(out / ('linked' + suffix + '.json'))])):
            checks.append((name + suffix, [*python, 'scripts/' + name + '.py', *extra]))
    for name in ('test-scm-hook', 'test-scm-pair-hook', 'test-rpmh-tail-hook', 'test-soc-delay'):
        checks.append((name, [sys.executable, 'scripts/' + name + '.py']))
    checks.append(('integration', [sys.executable, 'scripts/test-gpu-integration.py',
        'Integration.test_soc_canonical_tail_headers_staged', 'Integration.test_workflow_copies_every_statically_staged_source',
        'Integration.test_boot_sources_do_not_invoke_gpu_engines', 'Integration.test_no_security_bypass_in_concrete_cp_adapters']))
    paths = {ROOT / 'scripts/validate-smem-handoff.py', ROOT / 'scripts/stage-smem-map-firmware.py',
             ROOT / 'scripts/stage-scm-pair-firmware.py', ROOT / 'scripts/analyze-smem-map-log.py',
             ROOT / 'scripts/verify-npa-state-evidence.py', ROOT / 'scripts/adt.py',
             ROOT / 'scripts/verify-scm-pair-boot.py', ROOT / 'scripts/repack-scm-pair-fv.py',
             ROOT / 'scripts/build-kernel-patches.sh', ROOT / 'scripts/build-smem-handoff-firmware.sh',
             ROOT / 'scripts/analyze-smem-handoff-log.py', ROOT / 'scripts/stage-smem-handoff-firmware.py',
             ROOT / 'scripts/verify-gpu-kernel.py', ROOT / 'scripts/validate-gpu-offline.py',
             ROOT / 'scripts/verify-smem-map-kernel.py', ROOT / 'scripts/verify-ci-scm-pair.py',
             ROOT / 'scripts/smem-packaging-evidence.py', ROOT / 'scripts/test-smem-packaging.py',
             ROOT / 'scripts/build-scm-pair-local.py', ROOT / 'scripts/make-scm-pair-boot.py',
             ROOT / 'scripts/verify-smem-packaged-adt.py',
             ROOT / 'scripts/check-kernel-sources.sh', ROOT / '.github/workflows/touch-kernel.yml',
             ROOT / 'xnu/0085-stage-cepheus-soc.sh', ROOT / 'xnu/cepheus_soc.c',
             ROOT / 'xnu/cepheus_smem_map_handoff.h', ROOT / 'xnu/test_smem_map_handoff.c',
             ROOT / 'firmware/CepheusSmemMapHandoff.h', ROOT / 'firmware/CepheusSmemMapProbe.c',
             ROOT / 'firmware/CepheusSmemMapProbe.h', ROOT / 'firmware/test_smem_map_handoff.c',
             ROOT / 'firmware/test_smem_map_probe.c'}
    for _, command in checks:
        paths.add(ROOT / command[2 if '-O' in command else 1])
    sources = {str(path.relative_to(ROOT)): digest(path) for path in sorted(paths)}
    out.mkdir(parents=True)
    receipt = {'time_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'parent_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        'source_sha256': sources, 'checks': [], 'scope': 'mocked firmware/AT/DT, actual native EFI static verification only',
        'phone_operations': False, 'gpu_execution': False, 'firmware_execution': False, 'kernel_link': False}
    for name, command in checks:
        run = subprocess.run(command, cwd=ROOT, capture_output=True, timeout=180, env=os.environ.copy())
        log = out / (name + '.log')
        log.write_bytes(run.stdout + run.stderr)
        receipt['checks'].append({'name': name, 'command': command, 'exit_code': run.returncode,
                                 'log': log.name, 'log_sha256': digest(log)})
        print(name, 'PASS' if not run.returncode else 'FAIL', flush=True)
    unchanged = sources == {str(path.relative_to(ROOT)): digest(path) for path in sorted(paths)}
    receipt['source_hashes_unchanged'] = unchanged
    receipt['status'] = 'PASS' if unchanged and all(not check['exit_code'] for check in receipt['checks']) else 'FAIL'
    for suffix in ('', '-opt'):
        verification = out / ('linked' + suffix + '.json')
        if verification.exists():
            receipt['linked' + suffix + '_sha256'] = digest(verification)
    artifact = args.build / 'build/DarwinBoot/RELEASE_CLANGPDB/AARCH64/DarwinBoot.efi'
    receipt['efi'] = {'bytes': artifact.stat().st_size, 'sha256': digest(artifact)}
    build_log = args.build.with_name(args.build.name + '-build.log')
    receipt['build_log'] = {'path': str(build_log), 'sha256': digest(build_log)}
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(receipt['status'], digest(out / 'receipt.json'))
    return 0 if receipt['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
