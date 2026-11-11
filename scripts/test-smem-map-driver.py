#!/usr/bin/env python3
"""Exercise actual driver entry and parse its synthetic output; no firmware execution."""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--uefi', required=True, type=Path)
    args = parser.parse_args()
    include = args.uefi.resolve() / 'MU_BASECORE/MdePkg/Include'
    platform = args.uefi.resolve() / 'Platforms/AndromedaPkg/Include'
    text = (ROOT / 'firmware/CepheusSmemMapProbe.c').read_text()
    start = text.index('STATIC CE_SMEM_CPU_MAP\nCeSmemCpuMap (VOID)')
    end = text.index('STATIC BOOLEAN\nCeSmemCpuValid', start)
    mock = text[:start] + 'extern CE_SMEM_CPU_MAP CeSmemCpuMap (VOID);\nextern UINT64 CeSmemPar (UINT64);\n' + text[end:]
    helpers = (ROOT / 'firmware/test_smem_map_probe.c').read_text().split('STATIC EFI_STATUS Probe (VOID)', 1)[0]
    helpers = helpers.replace('STATIC CE_SMEM_MAP_EVIDENCE Evidence;\n', '')
    with tempfile.TemporaryDirectory(prefix='smem-driver-') as directory:
        work = Path(directory)
        (work / 'map-probe-host.c').write_text(mock)
        (work / 'map-probe-host-tests.c').write_text(helpers)
        binary = work / 'driver'
        subprocess.run(['cc', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror', '-fshort-wchar',
                        '-fno-builtin', '-fsanitize=address,undefined', '-I', str(include),
                        '-I', str(include / 'X64'), '-I', str(platform), '-I', str(work),
                        '-I', str(ROOT / 'firmware'), str(ROOT / 'firmware/test_smem_map_driver.c'),
                        '-o', str(binary)], check=True)
        raw = subprocess.check_output([str(binary)])
        log, output = work / 'synthetic.log', work / 'result.json'
        log.write_bytes(raw)
        command = [sys.executable, str(ROOT / 'scripts/analyze-smem-map-log.py'),
                   str(log), '--output', str(output)]
        subprocess.run(command, check=True, capture_output=True)
        saved = output.read_bytes()
        if json.loads(saved)['status'] != 'MAPPING_DIAGNOSTIC_PASS':
            raise ValueError('CLI did not parse actual synthetic output')
        if subprocess.run(command, capture_output=True).returncode == 0 or output.read_bytes() != saved:
            raise ValueError('CLI overwrote existing evidence')
        log.write_bytes(raw[:-1])
        invalid = work / 'invalid.json'
        command[-1] = str(invalid)
        if subprocess.run(command, capture_output=True).returncode == 0 or invalid.exists():
            raise ValueError('failed CLI published a result')
    spec = importlib.util.spec_from_file_location('smem_map_log', ROOT / 'scripts/analyze-smem-map-log.py')
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    result = module.analyze(raw)
    if result['status'] != 'MAPPING_DIAGNOSTIC_PASS' or result['smem_access_authorized']:
        raise ValueError('unexpected parsed verdict')
    bad = [raw[:-1], raw + raw, raw.replace(b'index=100 ', b'index=99 '),
           raw.replace(b'pages=512', b'pages=511'), raw.replace(b'base=86000000', b'base=86001000', 1),
           raw.replace(b'par=4400000086000000', b'par=ff00000086000000'),
           raw.replace(b'type=5 attr=800', b'type=0 attr=800'),
           raw.replace(b'stable=1', b'stable=0')]
    for case in bad:
        try:
            module.analyze(case)
        except ValueError:
            continue
        raise ValueError('accepted malformed transcript')
    for count in range(518):
        case = b'\n'.join(raw.splitlines()[:count]) + b'\n'
        try:
            module.analyze(case)
        except ValueError:
            continue
        raise ValueError('accepted truncated transcript')
    failed = raw.replace(b'end probe=0', b'end probe=8000000000000011')
    if module.analyze(failed)['status'] != 'DIAGNOSTIC_FAILED':
        raise ValueError('failed probe accepted')
    print('SMEM map driver/log: actual entry+synthetic output, 518 truncations and malformed cases PASS; no device')


if __name__ == '__main__':
    main()
