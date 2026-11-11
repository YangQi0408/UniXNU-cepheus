#!/usr/bin/env python3
"""Parse saved mapping-diagnostic text; never authorizes SMEM/GPU access."""
import argparse
import hashlib
import json
from pathlib import Path
import re

BASE, SIZE = 0x86000000, 0x200000
CPU = ('el', 'cpu', 'sctlr', 'tcr', 'ttbr0', 'ttbr1', 'mair', 'mmfr0')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def fields(text, names, decimal=()):
    pairs = text.split()
    require(len(pairs) == len(names), 'wrong field count')
    result = {}
    for pair, name in zip(pairs, names):
        key, sep, value = pair.partition('=')
        require(key == name and sep and re.fullmatch(
            '[0-9]+' if name in decimal else '[0-9a-fA-F]+', value), 'invalid field ' + name)
        result[name] = int(value, 10 if name in decimal else 16)
        require(result[name] < 1 << 64, 'field overflow')
    return result


def analyze(raw):
    require(len(raw) <= 2 * 1024 * 1024, 'log too large')
    text = raw.decode('utf-8', errors='strict')
    require(text.endswith('\n'), 'unterminated log')
    lines = []
    for line in text.splitlines():
        if 'CEPHSMEMMAP' in line:
            require(line.startswith('CEPHSMEMMAP '), 'unexpected diagnostic prefix')
            lines.append(line[len('CEPHSMEMMAP '):])
    require(len(lines) >= 6, 'incomplete diagnostic')
    require(lines[0].startswith('begin '), 'missing begin')
    begin = fields(lines[0][6:], ('version', 'base', 'bytes', 'prepare'), ('version',))
    require(begin['version'] == 1 and begin['base'] == BASE and begin['bytes'] == SIZE,
            'foreign diagnostic profile')
    require(lines[1].startswith('cpu=before '), 'missing before CPU')
    before = fields(lines[1][11:], CPU)
    require(lines[2].startswith('resource '), 'missing resource')
    resource = fields(lines[2][9:], ('base', 'bytes', 'type', 'attr', 'matches', 'hobs', 'complete'),
                      ('matches', 'hobs', 'complete'))
    require(lines[3].startswith('gcd '), 'missing GCD')
    gcd = fields(lines[3][4:], ('base', 'bytes', 'type', 'caps', 'attr', 'image', 'device', 'complete'),
                 ('type', 'complete'))
    require(lines[-2].startswith('cpu=after ') and lines[-1].startswith('end '), 'missing end')
    after = fields(lines[-2][10:], CPU)
    end = fields(lines[-1][4:], ('probe', 'close', 'pages', 'stable'), ('pages', 'stable'))
    require(end['pages'] <= 512 and len(lines) == end['pages'] + 6, 'page count mismatch')
    pages = []
    for index, line in enumerate(lines[4:-2]):
        require(line.startswith('page '), 'unexpected record')
        page = fields(line[5:], ('index', 'va', 'par'), ('index',))
        require(page['index'] == index and page['va'] == BASE + index * 4096, 'wrong/reordered page')
        pages.append(page)
    passed = begin['prepare'] == 0 and end['probe'] == 0 and end['close'] == 0
    if passed:
        require(end['pages'] == 512 and end['stable'] == 1 and before == after, 'incomplete successful map')
        require(before['el'] == 4 and before['sctlr'] & 5 == 5 and before['mmfr0'] & 15 <= 5
                and before['mmfr0'] >> 28 & 15 == 0, 'unsupported CPU profile')
        tcr = before['tcr']
        require(16 <= tcr & 63 <= 32 and tcr >> 14 & 3 == 0
                and not tcr & sum(1 << bit for bit in (7, 37, 39, 40, 59)), 'unsupported translation')
        require(resource['base'] == BASE and resource['bytes'] == SIZE and resource['type'] == 5
                and resource['attr'] == 0x800 and resource['matches'] == 1
                and 2 <= resource['hobs'] <= 16384 and resource['complete'] == 1, 'invalid resource proof')
        require(gcd['base'] == BASE and gcd['bytes'] == SIZE and gcd['type'] == 1
                and not gcd['attr'] & 0x2000 and not gcd['image'] and not gcd['device']
                and gcd['complete'] == 1, 'invalid GCD proof')
        for page in pages:
            par = page['par']
            require(not par & 1 and par >> 56 == 0x44 and not par & 0x00ff000000000000
                    and par & 0x0000fffffffff000 == page['va'], 'invalid page translation')
    return {'status': 'MAPPING_DIAGNOSTIC_PASS' if passed else 'DIAGNOSTIC_FAILED',
            'input_sha256': hashlib.sha256(raw).hexdigest(), 'begin': begin, 'before': before,
            'resource': resource, 'gcd': gcd, 'pages': pages, 'after': after, 'end': end,
            'smem_bytes_acquired': False, 'running_secure_identity_authenticated': False,
            'smem_access_authorized': False, 'gpu_admission_changed': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('output exists; preserve prior evidence')
    with args.log.open('rb') as stream:
        result = analyze(stream.read(2 * 1024 * 1024 + 1))
    with args.output.open('x') as stream:
        stream.write(json.dumps(result, indent=2) + '\n')
    print(result['status'])


if __name__ == '__main__':
    main()
