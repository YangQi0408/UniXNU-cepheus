#!/usr/bin/env python3
"""Compile/test actual pre-EBS map probe with explicit local EDK2; no device."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--uefi', required=True, type=Path)
    args = parser.parse_args()
    uefi = args.uefi.resolve()
    include = uefi / 'MU_BASECORE/MdePkg/Include'
    platform = uefi / 'Platforms/AndromedaPkg/Include'
    source = ROOT / 'firmware/CepheusSmemMapProbe.c'
    text = source.read_text()
    start = text.index('STATIC CE_SMEM_CPU_MAP\nCeSmemCpuMap (VOID)')
    end = text.index('STATIC BOOLEAN\nCeSmemCpuValid', start)
    mocked = text[:start] + ('extern CE_SMEM_CPU_MAP CeSmemCpuMap (VOID);\n'
                             'extern UINT64 CeSmemPar (UINT64 Address);\n\n') + text[end:]
    with tempfile.TemporaryDirectory(prefix='smem-map-probe-') as directory:
        work = Path(directory)
        (work / 'map-probe-host.c').write_text(mocked)
        flags = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-fshort-wchar', '-fno-builtin',
                 '-I', str(ROOT / 'firmware'), '-I', str(include), '-I', str(platform)]
        binary = work / 'test'
        subprocess.run(['cc', *flags, '-D_GNU_SOURCE', '-O1', '-fsanitize=address,undefined',
                        '-I', str(include / 'X64'), '-I', str(work),
                        str(ROOT / 'firmware/test_smem_map_probe.c'), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
        subprocess.run(['clang', '--target=aarch64-none-elf', '-ffreestanding', *flags,
                        '-I', str(include / 'AArch64'), '-O2', '-c', str(source),
                        '-o', str(work / 'probe.o')], check=True)
        listing = subprocess.check_output(['llvm-objdump', '-d', str(work / 'probe.o')], text=True)
        # Verify real architecture path compiles, independently of host substitutes.
        for instruction in ('at\ts1e1r', 'mrs', 'msr', 'PAR_EL1', 'DAIF'):
            if instruction.lower() not in listing.lower():
                raise ValueError('missing native instruction: ' + instruction)
        for instruction in ('smc', 'hvc', 'tlbi', 'dc\t'):
            if '\t' + instruction in listing.lower():
                raise ValueError('unexpected native instruction: ' + instruction)
        for line in listing.splitlines():
            if '\tmsr\t' in line.lower() and not any(
                    name in line.lower() for name in ('par_el1', 'daif', 'daifset')):
                raise ValueError('unexpected system-register write: ' + line)
        subprocess.run(['clang', '--target=aarch64-none-elf', '-ffreestanding', *flags,
                        '-I', str(include / 'AArch64'), '-O2', '--analyze', str(source)],
                       check=True, cwd=work)
        print(json.dumps({'status': 'PASS', 'host_asan_ubsan': True,
                          'aarch64_freestanding_compile': True,
                          'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                          'services_and_at': 'mocked on host', 'smem_loads': False,
                          'production_caller': False, 'phone_operations': False,
                          'gpu_authorization': False}))


if __name__ == '__main__':
    main()
