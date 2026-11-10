#!/usr/bin/env python3
"""Exercise actual transformed legacy writers and scope lifecycle, without MMIO."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def module(name, file):
    spec = importlib.util.spec_from_file_location(name, ROOT/'scripts'/file)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--patches', type=Path, default=ROOT.parent/'inputs/ios-sweet-ordered-touch/xnu/patches')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    temporary = tempfile.TemporaryDirectory() if not args.output else None
    out = args.output.resolve() if args.output else Path(temporary.name)
    if args.output:
        out.mkdir(parents=True, exist_ok=False)
    source_names = ('xnu/cepheus_rpmh_scope.h', 'xnu/cepheus_rpmh_scope_private.h',
                    'scripts/prepare-touch-power-patch.py', 'scripts/test-rpmh-scope.py',
                    'tests/rpmh-scope-source-host.c')
    receipt = {'result': 'RUNNING', 'phone_operations': False, 'gpu_execution': False,
               'resource_owner': False, 'source_sha256': {name: hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
                                                        for name in source_names}, 'steps': []}

    def run(label, command):
        log = out/(label+'.log')
        with log.open('wb') as stream:
            result = subprocess.run([str(item) for item in command], cwd=ROOT, stdout=stream,
                                    stderr=subprocess.STDOUT, timeout=120)
        text = log.read_text()
        status = result.returncode or int('warning:' in text or 'error:' in text)
        receipt['steps'].append({'label': label, 'exit_code': status,
                                 'command': [str(item) for item in command], 'log': log.name,
                                 'sha256': hashlib.sha256(log.read_bytes()).hexdigest()})
        receipt['result'] = 'FAIL' if status else 'RUNNING'
        (out/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
        print(label, 'FAIL' if status else 'PASS', flush=True)
        if status:
            raise SystemExit(text)

    tree = out/'applied'
    tree.mkdir()
    run('init', ['git', 'init', '-q', tree])
    for patch in sorted(args.patches.resolve().glob('*.patch')):
        if patch.name == '0086-cepheus-gpu-arc.patch':
            continue
        run('apply-'+patch.stem, ['git', '-C', tree, 'apply', '--whitespace=nowarn',
                                '--include=bsd/dev/sweet_rpmh.c', patch])
    power, audit = module('scope_power', 'prepare-touch-power-patch.py'), module('scope_body', 'audit-pas13-source.py')
    transformed = power.transform((tree/power.TARGET).read_text())
    (out/'transformed-rpmh.c').write_text(transformed)
    harness = (ROOT/'tests/rpmh-scope-source-host.c').read_text()
    for marker, name, signature in (
        ('APPLIED_LEGACY_VREG', 'sweet_rpmh_vreg_enable', 'int sweet_rpmh_vreg_enable(const char *res,unsigned mv)'),
        ('APPLIED_TOUCH_POWER', 'cepheus_touch_power_enable', 'int cepheus_touch_power_enable(void)')):
        body = audit.body(transformed, name)
        assert body.index('ceph_rpmh_scope_legacy(') < body.index('rh_map(')
        harness = harness.replace('/* '+marker+' */', signature+'\n'+body)
    file = out/'source-host.c'
    file.write_text(harness)
    for label, compiler, flags in (
        ('host', os.environ.get('CC', 'cc'), ['-O2', '-fsanitize=undefined']),
        ('sanitized', 'clang', ['-O1', '-g', '-fsanitize=address,undefined'])):
        exe = out/(label+'-scope')
        run(label+'-compile', [compiler, '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                              '-I', ROOT/'xnu', *flags, file, '-o', exe])
        run(label+'-run', [exe])
    run('analysis', ['clang', '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror', '-I', ROOT/'xnu',
                     '--analyze', '-Xanalyzer', '-analyzer-output=text', file, '-o', os.devnull])
    run('full-native', ['python3', ROOT/'scripts/test-arc-active-native.py', args.patches.resolve()])
    receipt.update(result='PASS', actual_transformed_legacy_bodies=True,
                   transformed_sha256=hashlib.sha256(transformed.encode()).hexdigest(),
                   limitations=['synthetic idle token/mappings; no native resource owner',
                                'inherited requests/global writers not established'])
    (out/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
    print('RPMh participating-writer scope PASS; not resource owner/runtime')
    if temporary:
        temporary.cleanup()


if __name__ == '__main__':
    main()
