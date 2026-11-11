#!/usr/bin/env python3
"""Check actual linked DarwinBoot mapping handoff and staged ADT parser offline."""
import argparse
import ast
from collections import Counter
import ctypes
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shlex
import struct
import subprocess
import tempfile


def module(name, file):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(file))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


STAGE = module('smem_native_stage', 'stage-smem-handoff-firmware.py')
PE = module('smem_native_pe', 'verify-npa-state-evidence.py')
ADT = module('smem_native_adt', 'adt.py')
ROOT = STAGE.ROOT


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def identity(path):
    return {'bytes': path.stat().st_size, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}


def linked(blob):
    segments = PE.pe_segments(blob)
    pe = struct.unpack_from('<I', blob, 60)[0]
    header = pe + 24
    optional = struct.unpack_from('<H', blob, pe + 20)[0]
    require(optional >= 240 and struct.unpack_from('<H', blob, header + 68)[0] == 11,
            'expected EFI boot-service driver')
    require(struct.unpack_from('<II', blob, header + 120) == (0, 0), 'unexpected imports')
    sections = struct.unpack_from('<H', blob, pe + 6)[0]
    texts = []
    for index in range(sections):
        name, size, address = struct.unpack_from('<8sII', blob, header + optional + index * 40)
        characteristics = struct.unpack_from('<I', blob, header + optional + index * 40 + 36)[0]
        if characteristics & 0x20000000:
            texts.append((address, size))
    require(len(texts) == 3, 'unexpected executable sections')
    entry = struct.unpack_from('<I', blob, header + 16)[0]
    require(any(address <= entry < address + size for address, size in texts), 'invalid entry')
    rows = []
    for address, size in texts:
        rows.extend(PE.decode(PE.span(blob, segments, address, size), address))
    sites = [index for index, row in enumerate(rows) if row['mnemonic'] == 'at']
    require(len(sites) == 1, 'wrong AT count')
    sequence = rows[sites[0] - 3:sites[0] + 6]
    require([row['mnemonic'] for row in sequence] ==
            ['mrs', 'msr', 'mrs', 'at', 'isb', 'mrs', 'msr', 'msr', 'isb'], 'native preservation missing')
    daif = re.fullmatch(r'(x\d+), daif', sequence[0]['operands'])
    par = re.fullmatch(r'(x\d+), par_el1', sequence[2]['operands'])
    require(daif and par and daif[1] != par[1] and sequence[1]['operands'] == 'daifset, #0xf'
            and sequence[3]['operands'].startswith('s1e1r, x')
            and sequence[6]['operands'] == 'par_el1, ' + par[1]
            and sequence[7]['operands'] == 'daif, ' + daif[1], 'native restore incorrect')
    for row in rows:
        require(row['mnemonic'] not in ('smc', 'hvc'), 'unexpected secure call')
        if row['mnemonic'] == 'msr' and row not in sequence:
            require(row['operands'] in ('daifset, #0xf', 'sctlr_el1, x4'), 'unexpected register write')
    # DarwinEnter has pre-existing cache/TLB/MMU teardown for the owned kernel
    # window. It is intentionally not rejected as a mapping-probe side effect.
    require(sum(row['mnemonic'] == 'dc' for row in rows) == 1 and
            sum(row['mnemonic'] == 'tlbi' for row in rows) == 1 and
            sum(row['mnemonic'] == 'ic' for row in rows) == 1, 'unexpected entry cache/TLB sites')
    for marker in (b'cepheus-smem-map\0', b'cephsmemmap\0'):
        require(marker in blob, 'missing opt-in/property marker')
    require(b'CEPHEUS SCM' not in blob and b'CEPHSMEMMAP begin' not in blob, 'foreign capture/logging route')
    return {'entry_rva': hex(entry), 'native_at_rva': rows[sites[0]]['address'],
            'par_daif_restore_verified': True, 'decoded_instructions': len(rows),
            'smc_hvc_sites': 0, 'entry_cache_tlb_sites': 1, 'imports': False}


def tree(name='chosen', size=4576, duplicate=False, nonzero=False, flags=0):
    root, chosen = ADT.Node(), ADT.Node()
    root.props = [('name', 0, b'device-tree\0')]
    value = bytes(size) if not nonzero else b'1' + bytes(size - 1)
    chosen.props = [('name', 0, name.encode() + b'\0'), ('cepheus-smem-map', flags, value)]
    if duplicate:
        chosen.props.append(('cepheus-smem-map', flags, value))
    root.children = [chosen]
    return ADT.serialize(root)


def verify(build, repository, uefi):
    artifact = build / 'build/DarwinBoot/RELEASE_CLANGPDB/AARCH64/DarwinBoot.efi'
    directory = artifact.parent / 'DarwinBootPkg/Driver/DarwinBoot'
    makefile, mapfile = directory / 'GNUmakefile', directory / 'DEBUG/DarwinBoot.map'
    result = {'status': 'PASS_LINKED_HANDOFF_OFFLINE_ONLY', 'efi': identity(artifact),
              'map': identity(mapfile), 'linked': linked(artifact.read_bytes()), 'sources': {},
              'phone_operations': False, 'firmware_execution': False, 'smem_bytes_acquired': False,
              'smem_access_authorized': False, 'gpu_execution': False}
    with tempfile.TemporaryDirectory(prefix='smem-native-') as temp:
        work = Path(temp)
        manifest = STAGE.stage(repository, uefi, work / 'source')
        require(manifest == json.loads((build / 'source/smem-handoff-source.json').read_text()), 'staging manifest differs')
        for relative, expected in manifest['files'].items():
            require(identity(build / 'source' / relative)['sha256'] == expected, 'staged bytes differ: ' + relative)
        result['sources'] = manifest['files']
        driver = work / 'source/DarwinBootPkg/Driver'
        text = (driver / 'DarwinBoot.c').read_text()
        capture = text.index('Status = CepheusSmemCapture ();')
        require(capture < text.index('Status = GetMap (&Map, &Capacity, &MapSize') <
                text.index('Status = gBS->ExitBootServices') < text.index('!CepheusSmemHandoff (TreeAddress') <
                text.index('DarwinEnter (ArgsAddress'), 'capture/handoff ordering')
        require(text.count('Status = CepheusSmemCapture ();') == 1 and 'CepheusScm' not in text, 'mixed/repeated capture')
        post = (driver / 'CepheusSmemMapHandoff.h').read_text().split('CepheusSmemHandoff (', 1)[1]
        for forbidden in ('gBS', 'gDS', 'Print', 'Allocate', 'FreePool', 'CepheusSmemMapProbe (', 'smc'):
            require(forbidden not in post, 'post-EBS service/query: ' + forbidden)
        preamble = next(ast.literal_eval(n.value) for n in ast.parse((ROOT / 'scripts/test-scm-pair.py').read_text()).body
            if isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'PREAMBLE' for t in n.targets))
        preamble = preamble.replace('#include "cepheus_scm_handoff.h"', '#include "cepheus_smem_map_handoff.h"')
        parser = 'STATIC\nBOOLEAN\nAdtNode' + text.split('STATIC\nBOOLEAN\nAdtNode', 1)[1].split('STATIC\nBOOLEAN\nValidTrustCaches', 1)[0]
        source = work / 'parser.c'
        source.write_text(preamble + parser + '''
int parse(const UINT8 *p,UINTN n) {
 UINTN o=0,r=MAX_UINTN,t=MAX_UINTN,s=MAX_UINTN;UINT64 w=0;BOOLEAN k=0;
 return AdtNode(p,n,&o,0,3,&r,&t,&w,&k,&s) && o==n && s!=MAX_UINTN;
}
''')
        library = work / 'parser.so'
        subprocess.run(['cc', '-shared', '-fPIC', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
                        '-I', str(driver), str(source), '-o', str(library)], check=True)
        parse = ctypes.CDLL(str(library)).parse
        parse.argtypes = [ctypes.c_char_p, ctypes.c_size_t]
        good = tree()
        require(parse(good, len(good)) and not parse(None, len(good)), 'actual ADT positive/null')
        for bad in (tree('elsewhere'), tree(size=4575), tree(size=4577), tree(duplicate=True),
                    tree(nonzero=True), tree(flags=1), good + b'junk'):
            require(not parse(bad, len(bad)), 'actual ADT malformed accepted')
        for size in range(len(good)):
            require(not parse(good[:size], size), 'actual ADT truncation accepted')
        result['actual_adt_parser_truncations'] = len(good)
        flags = subprocess.check_output(['make', '-s', '-f', str(makefile), 'WORKSPACE=' + str(build), '--eval',
            'smem_flags:;@printf "%s\\n" "$(CC_FLAGS) $(INC)"', 'smem_flags'], cwd=directory, text=True)
        result['native_flags_sha256'] = hashlib.sha256(flags.encode()).hexdigest()
        result['analysis'] = []
        for name in ('DarwinBoot.c', 'CepheusSmemMapProbe.c'):
            run = subprocess.run(['clang', *shlex.split(flags), '-fno-lto', '--analyze',
                '-Xanalyzer', '-analyzer-output=text', str(build / 'source/DarwinBootPkg/Driver' / name), '-o', '/dev/null'],
                cwd=directory, capture_output=True, text=True, timeout=120)
            require(run.returncode == 0, 'native analysis failed: ' + run.stderr)
            warnings = re.findall(r'warning: ([^\n]+)', run.stderr)
            baseline_warnings = []
            if name == 'DarwinBoot.c':
                STAGE.PAIR.stage(repository, work / 'baseline')
                baseline = work / 'baseline/DarwinBootPkg/Driver/DarwinBoot.c'
                base = subprocess.run(['clang', *shlex.split(flags), '-fno-lto', '--analyze',
                    '-Xanalyzer', '-analyzer-output=text', str(baseline), '-o', '/dev/null'],
                    cwd=directory, capture_output=True, text=True, timeout=120)
                require(base.returncode == 0, 'baseline analysis failed')
                baseline_warnings = re.findall(r'warning: ([^\n]+)', base.stderr)
            require(not Counter(warnings) - Counter(baseline_warnings), 'new native warning: ' + run.stderr)
            result['analysis'].append({'source': name, 'warnings': warnings, 'baseline_warnings': baseline_warnings,
                                       'diagnostics': run.stderr, 'no_additional_warning': True})
    symbols = mapfile.read_text()
    for name in ('CepheusSmemCapture', 'CepheusSmemHandoff', 'CepheusSmemMapClose', 'DarwinEnter', 'mCepheusSmem'):
        require(name in symbols, 'missing native lifecycle: ' + name)
    require('DxeDebugLibConstructor' not in symbols and 'ExitBootServicesCallback' not in symbols, 'implicit logging EBS event')
    entry_section = re.search(r'^([0-9a-f]{8}) ([0-9a-f]{8})\s+4096 \.text\.DarwinEnter$', symbols, re.M)
    require(entry_section, 'missing separate DarwinEnter code')
    address, size = (int(value, 16) for value in entry_section.groups())
    rows = PE.decode(PE.span(artifact.read_bytes(), PE.pe_segments(artifact.read_bytes()), address, size), address)
    ops = {(row['mnemonic'], row['operands']) for row in rows}
    require(('dc', 'civac, x4') in ops and ('tlbi', 'vmalle1') in ops and
            ('ic', 'iallu') in ops and ('msr', 'sctlr_el1, x4') in ops, 'entry cleanup differs')
    result['verifier_sha256'] = identity(Path(__file__))['sha256']
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('build', 'repository', 'uefi', 'output'):
        parser.add_argument('--' + name, required=True, type=Path)
    args = parser.parse_args()
    require(not args.output.exists(), 'output exists; preserve prior evidence')
    result = verify(args.build.resolve(), args.repository.resolve(), args.uefi.resolve())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('x') as stream:
        stream.write(json.dumps(result, indent=2) + '\n')
    print(result['status'], result['efi']['sha256'])


if __name__ == '__main__':
    main()
