#!/usr/bin/env python3
"""Run actual staged DXE Pool.c against external-service fixtures, never hardware."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('pool_stage', ROOT/'scripts/stage-pool-lifetime-core.py')
STAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(STAGE)


def main(build=None, output=None):
    if output and output.exists():
        raise ValueError('output already exists')
    receipt = {'status': 'PASS', 'hardware_executed': False, 'steps': [], 'sources': {}}
    with tempfile.TemporaryDirectory() as directory:
        temp = Path(directory)
        STAGE.stage(ROOT.parent/'uefi/MU_BASECORE', temp/'source')
        source = temp/'source'/STAGE.CORE/'Mem/Pool.c'
        text = source.read_text()
        assert text.index('CePoolFreed (&') < text.index('Head = BASE_CR (Buffer')
        assert 'gBS' not in text[text.index('CoreCepheusPoolLookup ('):text.index('typedef struct')]
        original = subprocess.check_output(['git', '-C', str(ROOT.parent/'uefi/MU_BASECORE'),
                                          'show', STAGE.BASE+':'+str(STAGE.CORE/'Mem/Pool.c')]).decode()
        assert text == STAGE.integrate(original.replace('\r\n', '\n'))
        page = (temp/'source'/STAGE.CORE/'Mem/Page.c').read_text()
        assert page.index('CoreCepheusPoolRangeChanged (Start, NumberOfPages);') < page.index('  NumberOfBytes = LShiftU64')
        original_page = subprocess.check_output(['git', '-C', str(ROOT.parent/'uefi/MU_BASECORE'),
                                               'show', STAGE.BASE+':'+str(STAGE.CORE/'Mem/Page.c')]).decode()
        assert page == STAGE.integrate_page(original_page.replace('\r\n', '\n'))
        for name in ('DxeMain/DxeMain.c', 'Mem/Pool.c', 'Mem/Page.c', 'Mem/cepheus_pool_lifetime.h', 'DxeMain.inf'):
            staged = temp/'source'/STAGE.CORE/name
            if build:
                assert staged.read_bytes() == (build/'source'/STAGE.CORE/name).read_bytes(), name
            receipt['sources'][name] = hashlib.sha256(staged.read_bytes()).hexdigest()
        fixture = ROOT/'firmware/test_pool_lifetime_core.c'
        receipt['sources']['fixture'] = hashlib.sha256(fixture.read_bytes()).hexdigest()
        for name in ('scripts/test-pool-lifetime-core.py', 'scripts/stage-pool-lifetime-core.py',
                     'scripts/build-pool-lifetime-core.sh', 'firmware/PoolLifetimeCore.dsc'):
            receipt['sources'][name] = hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
        for include in ('DxeMain.h', 'Imem.h', 'HeapGuard.h'):
            anchor = f'#include "{include}"'
            assert text.count(anchor) == 1
            text = text.replace(anchor, '')
        (temp/'PoolUnderTest.c').write_text(text)
        function = 'EFI_STATUS\nCoreConvertPagesEx (\n'+page.split('EFI_STATUS\nCoreConvertPagesEx (\n', 1)[1].split('\n/**\n  Internal function.  Converts a memory range to the specified type.', 1)[0]
        (temp/'PageUnderTest.c').write_text(function)
        for optimize in ('-O0', '-O2'):
            target = temp/('test'+optimize)
            args = ['clang', '-std=c11', '-D_DEFAULT_SOURCE', optimize, '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I', str(temp),
                    '-I', str(ROOT/'firmware'), str(fixture), '-o', str(target)]
            subprocess.run(args, check=True)
            env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:abort_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
            result = subprocess.run([str(target)], env=env, capture_output=True, text=True)
            if result.returncode:
                print(result.stdout, end='')
                print(result.stderr, end='')
                result.check_returncode()
            print(result.stdout.strip())
            receipt['steps'].append({'optimization': optimize, 'output': result.stdout.strip()})
        if build:
            assert (ROOT/'firmware/PoolLifetimeCore.dsc').read_bytes() == (build/'PoolLifetimeCore.dsc').read_bytes()
            manifest = json.loads((build/'source/pool-lifetime-source.json').read_text())
            for name, digest in manifest['files'].items():
                assert hashlib.sha256((build/'source'/name).read_bytes()).hexdigest() == digest, name
            artifact = build/'build/PoolLifetimeCore/RELEASE_CLANGPDB/AARCH64/DxeCore.efi'
            blob = artifact.read_bytes()
            assert blob[:2] == b'MZ'
            buildlog = (build/'build.log').read_text()
            assert '- Done -' in buildlog and '- Failed -' not in buildlog
            module = artifact.parent/'MdeModulePkg/Core/Dxe/DxeMain'
            mapfile = module/'DEBUG/DxeCore.map'
            assert 'CoreCepheusPoolLookup' in mapfile.read_text()
            response = module/'OUTPUT/cc_resp.txt'
            analyses = {}
            for name, body in (('baseline-page', original_page), ('staged-page', page),
                               ('baseline-pool', original), ('staged-pool', source.read_text())):
                analysis_source = temp/(name+'.c')
                analysis_source.write_text(body)
                command = ['clang', '@'+str(response.resolve()), '-fno-lto', '--analyze',
                           '-Xanalyzer', '-analyzer-output=text', str(analysis_source), '-o', os.devnull]
                result = subprocess.run(command, cwd=module, capture_output=True, text=True, timeout=120)
                result.check_returncode()
                assert 'error:' not in result.stderr
                warnings = re.findall(r'warning: ([^\n]+)', result.stderr)
                analyses[name] = warnings
                receipt['steps'].append({'native_analysis': name, 'warnings': warnings,
                                         'diagnostics_sha256': hashlib.sha256(result.stderr.encode()).hexdigest()})
                if output:
                    log = output.with_name(output.stem+'-'+name+'.log')
                    if log.exists():
                        raise ValueError('analysis log already exists')
                    log.parent.mkdir(parents=True, exist_ok=True)
                    log.write_text(result.stderr)
            assert not analyses['staged-pool'] and not analyses['baseline-pool']
            assert analyses['baseline-page'] == analyses['staged-page']
            assert len(analyses['staged-page']) == 2
            receipt['build'] = {'source_matches_tested_stage': True, 'artifact': str(artifact.resolve()),
                                'bytes': len(blob), 'sha256': hashlib.sha256(blob).hexdigest(),
                                'build_log_sha256': hashlib.sha256(buildlog.encode()).hexdigest(),
                                'map_sha256': hashlib.sha256(mapfile.read_bytes()).hexdigest(),
                                'response_sha256': hashlib.sha256(response.read_bytes()).hexdigest()}
    if output:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(receipt, indent=2)+'\n')
    return receipt


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware-build', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    print(json.dumps(main(args.firmware_build, args.output), indent=2))
