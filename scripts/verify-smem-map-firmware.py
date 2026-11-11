#!/usr/bin/env python3
"""Verify a standalone linked mapping diagnostic, never invoke firmware/device."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shlex
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def module(name, file):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(file))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


STAGE = module('smem_stage', 'stage-smem-map-firmware.py')
PE = module('smem_build_pe', 'verify-npa-state-evidence.py')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def identity(path):
    return {'bytes': path.stat().st_size, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}


def linked(blob):
    segments = PE.pe_segments(blob)
    pe = struct.unpack_from('<I', blob, 60)[0]
    optional = struct.unpack_from('<H', blob, pe + 20)[0]
    header = pe + 24
    require(optional >= 240 and struct.unpack_from('<H', blob, header + 68)[0] == 11,
            'expected EFI boot-service driver')
    require(struct.unpack_from('<II', blob, header + 120) == (0, 0),
            'unexpected import table')
    entry = struct.unpack_from('<I', blob, header + 16)[0]
    sections = struct.unpack_from('<H', blob, pe + 6)[0]
    texts = []
    for index in range(sections):
        name, size, address = struct.unpack_from('<8sII', blob, header + optional + index * 40)
        if name.rstrip(b'\0') == b'.text':
            texts.append((address, size))
    require(len(texts) == 1, 'missing or duplicate text section')
    address, size = texts[0]
    require(address <= entry < address + size, 'entry is not executable')
    instructions = PE.decode(PE.span(blob, segments, address, size), address)
    ats = [index for index, row in enumerate(instructions) if row['mnemonic'] == 'at']
    require(len(ats) == 1, 'expected one native translation query')
    index = ats[0]
    sequence = instructions[index - 3:index + 6]
    require(len(sequence) == 9 and [row['mnemonic'] for row in sequence] ==
            ['mrs', 'msr', 'mrs', 'at', 'isb', 'mrs', 'msr', 'msr', 'isb'],
            'missing PAR/DAIF preservation sequence')
    daif = re.fullmatch(r'(x\d+), daif', sequence[0]['operands'])
    par = re.fullmatch(r'(x\d+), par_el1', sequence[2]['operands'])
    require(daif and par and daif[1] != par[1], 'invalid saved registers')
    require(sequence[1]['operands'] == 'daifset, #0xf'
            and sequence[3]['operands'].startswith('s1e1r, x')
            and sequence[6]['operands'] == 'par_el1, ' + par[1]
            and sequence[7]['operands'] == 'daif, ' + daif[1], 'incorrect restore')
    for row in instructions:
        require(row['mnemonic'] not in ('smc', 'hvc', 'tlbi', 'dc'),
                'unexpected privileged operation')
        if row['mnemonic'] == 'msr':
            require(row in sequence, 'unexpected system-register store')
    for needle in (b'CEPHSMEMMAP begin version=1', b'CEPHSMEMMAP page index=',
                   b'CEPHSMEMMAP end probe=', 'cephsmemmap=1\0'.encode('utf-16le')):
        require(needle in blob, 'missing opt-in/log marker')
    return {'entry_rva': hex(entry), 'native_at_rva': instructions[index]['address'],
            'native_par_daif_restore_verified': True, 'decoded_instructions': len(instructions),
            'imports': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', required=True, type=Path)
    parser.add_argument('--uefi', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    build, uefi, output = args.build.resolve(), args.uefi.resolve(), args.output.resolve()
    require(not output.exists(), 'preserve previous verification')
    artifact = build / 'build/SmemMapProbe/RELEASE_CLANGPDB/AARCH64/CepheusSmemMapProbe.efi'
    directory = artifact.parent / 'SmemMapProbePkg/Driver/SmemMapProbe'
    mapfile = directory / 'DEBUG/CepheusSmemMapProbe.map'
    makefile = directory / 'GNUmakefile'
    result = {'status': 'PASS_LINKED_OFFLINE_ONLY', 'efi': identity(artifact),
              'map': identity(mapfile), 'makefile': identity(makefile),
              'linked': linked(artifact.read_bytes()), 'sources': {}, 'analysis': [],
              'phone_operations': False, 'firmware_execution': False,
              'smem_bytes_acquired': False, 'smem_access_authorized': False, 'gpu_execution': False}
    with tempfile.TemporaryDirectory(prefix='smem-linked-') as temp:
        work = Path(temp)
        manifest = STAGE.stage(uefi, work / 'stage')
        require(json.loads((build / 'source/smem-map-source.json').read_text()) == manifest,
                'staging manifest mismatch')
        for relative, expected in manifest['files'].items():
            path = build / 'source' / relative
            require(identity(path)['sha256'] == expected, 'staged bytes differ: ' + relative)
            result['sources'][relative] = expected
        flags = subprocess.check_output(['make', '-s', '-f', str(makefile), 'WORKSPACE=' + str(build), '--eval',
            'smem_flags:;@printf "%s\\n" "$(CC_FLAGS) $(INC)"', 'smem_flags'],
            cwd=directory, text=True)
        result['compiler_flags_sha256'] = hashlib.sha256(flags.encode()).hexdigest()
        for name in ('CepheusSmemMapProbe.c', 'CepheusSmemMapDriver.c'):
            source = build / 'source/SmemMapProbePkg/Driver' / name
            command = ['clang', *shlex.split(flags), '-fno-lto', '--analyze',
                       '-Xanalyzer', '-analyzer-output=text', str(source), '-o', '/dev/null']
            run = subprocess.run(command, cwd=directory, capture_output=True, text=True, timeout=120)
            require(run.returncode == 0 and 'warning:' not in run.stderr and 'error:' not in run.stderr,
                    'native static analysis failed: ' + run.stderr)
            result['analysis'].append({'source': name, 'command': command,
                                      'diagnostics_sha256': hashlib.sha256(run.stderr.encode()).hexdigest(),
                                      'warnings': 0})
    text = mapfile.read_text()
    require('DxeDebugLibConstructor' not in text and 'ExitBootServicesCallback' not in text,
            'implicit DebugLib event lifecycle detected')
    require('_ModuleEntryPoint' in text and '_DriverUnloadHandler' in text and
            'CepheusSmemMapClose' in text and 'mSmemEvidence' in text, 'missing linked lifecycle')
    result['verifier_sha256'] = identity(Path(__file__))['sha256']
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('x') as stream:
        stream.write(json.dumps(result, indent=2) + '\n')
    print(result['status'], result['efi']['sha256'])


if __name__ == '__main__':
    main()
