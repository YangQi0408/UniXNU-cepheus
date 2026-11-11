#!/usr/bin/env python3
"""Reconstruct saved same-boot mapping bytes; never authorize SMEM or GPU."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[1]
BYTES, PREFIX = 4576, 'CEPHEUS SMEM MAP: '
CPU = ('el', 'cpu', 'sctlr', 'tcr', 'ttbr0', 'ttbr1', 'mair', 'mmfr0')
SPEC = importlib.util.spec_from_file_location('smem_console', Path(__file__).with_name('analyze-smem-map-log.py'))
MAP = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MAP)


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def checksum(data):
    value = 14695981039346656037
    for index, byte in enumerate(data):
        value = ((value ^ (0 if 88 <= index < 96 else byte)) * 1099511628211) & ((1 << 64) - 1)
    return value


def decode(data):
    require(len(data) == BYTES, 'wrong record size')
    magic, version, size, complete, attempts = struct.unpack_from('<Q4I', data)
    require((magic, version, size, complete) == (0x3150414d4d454d53, 1, BYTES, 1), 'record identity')
    (begin, end, tree, tree_bytes, prepare, probe, close, status, check,
     base, span, offset) = struct.unpack_from('<12Q', data, 24)
    hobs, pages, matches, resource_complete, gcd_complete, stable, ebs, reserved = struct.unpack_from('<8I', data, 120)
    require(1 <= attempts <= 3 and ebs == 1 and reserved == 0 and end >= begin, 'EBS/context bounds')
    require(tree and not tree & 4095 and 0 < tree_bytes <= 0x1000000 and tree + tree_bytes < 1 << 64
            and not offset & 3 and offset + BYTES <= tree_bytes, 'tree/copy bounds')
    require(not (tree < 0x86200000 and 0x86000000 < tree + tree_bytes), 'SMEM destination alias')
    require((base, span) == (MAP.BASE, MAP.SIZE) and pages <= 512
            and resource_complete <= 1 and gcd_complete <= 1 and stable <= 1, 'mapping bounds')
    require(close == 0 and (prepare != 0 or status == probe), 'status contradiction')
    require(check == checksum(data), 'record checksum')
    identities = {name: data[152 + index * 32:184 + index * 32].hex()
                  for index, name in enumerate(('probe_source', 'profile_source', 'profile_header'))}
    require(all(value != '00' * 32 for value in identities.values()), 'missing build metadata')
    before = dict(zip(CPU, struct.unpack_from('<8Q', data, 248)))
    after = dict(zip(CPU, struct.unpack_from('<8Q', data, 312)))
    hob_type, hob_bytes, hob_reserved = struct.unpack_from('<HHI', data, 376)
    r_type, r_attr, r_base, r_bytes = struct.unpack_from('<IIQQ', data, 400)
    resource = dict(base=r_base, bytes=r_bytes, type=r_type, attr=r_attr,
                    matches=matches, hobs=hobs, complete=resource_complete)
    d_base, d_bytes, caps, attr, image, device, d_type, d_reserved = struct.unpack_from('<6Q2I', data, 424)
    require(not d_reserved, 'GCD reserved field')
    gcd = dict(base=d_base, bytes=d_bytes, type=d_type, caps=caps, attr=attr,
               image=image, device=device, complete=gcd_complete)
    par = struct.unpack_from('<512Q', data, 480)
    require(all(value == 0 for value in par[pages:]), 'unused PAR values')
    console = ['CEPHSMEMMAP begin version=1 base=86000000 bytes=200000 prepare=' + format(prepare, 'x')]
    console.append('CEPHSMEMMAP cpu=before ' + ' '.join(f'{key}={value:x}' for key, value in before.items()))
    console.append('CEPHSMEMMAP resource ' + ' '.join(f'{key}={value if key in ("matches", "hobs", "complete") else format(value, "x")}' for key, value in resource.items()))
    console.append('CEPHSMEMMAP gcd ' + ' '.join(f'{key}={value if key in ("type", "complete") else format(value, "x")}' for key, value in gcd.items()))
    console.extend(f'CEPHSMEMMAP page index={index} va={base + index * 4096:x} par={par[index]:x}' for index in range(pages))
    console.append('CEPHSMEMMAP cpu=after ' + ' '.join(f'{key}={value:x}' for key, value in after.items()))
    console.append(f'CEPHSMEMMAP end probe={probe:x} close={close:x} pages={pages} stable={stable}')
    diagnostic = MAP.analyze(('\n'.join(console) + '\n').encode())
    passed = diagnostic['status'] == 'MAPPING_DIAGNOSTIC_PASS'
    if passed:
        require((hob_type, hob_bytes, hob_reserved) == (3, 48, 0), 'successful HOB ABI')
    return {'diagnostic': diagnostic, 'diagnostic_pass': passed, 'build_metadata': identities,
            'begin_ticks': begin, 'end_ticks': end, 'tree_address': tree, 'tree_bytes': tree_bytes,
            'property_offset': offset, 'ebs_attempts': attempts, 'checksum': check,
            'raw_sha256': hashlib.sha256(data).hexdigest()}


def analyze(raw):
    require(len(raw) <= 2 * 1024 * 1024 and raw.endswith(b'\n'), 'oversized/unterminated log')
    lines = []
    for line in raw.decode('utf-8', errors='strict').splitlines():
        if 'CEPHEUS SMEM MAP' in line:
            require(line.startswith(PREFIX), 'unexpected diagnostic prefix')
            lines.append(line[len(PREFIX):])
    scheduled = bool(lines and lines[0] == 'scheduled delay_s=90')
    if scheduled:
        lines.pop(0)
    require(len(lines) == 74, 'incomplete or repeated handoff')
    begin = re.fullmatch(r'begin version=1 bytes=4576 checksum=([0-9a-f]{16})', lines[0])
    end = re.fullmatch(r'end diagnostic=([01]); no SMEM reads, SCM or GPU execution', lines[-1])
    require(begin and end, 'missing envelope markers')
    chunks = []
    for index, line in enumerate(lines[1:-1]):
        match = re.fullmatch(r'raw=(\d+) words=([0-9a-f]{16}(?::[0-9a-f]{16}){7})', line)
        require(match and int(match[1]) == index * 64, 'missing/reordered/duplicate chunk')
        chunks.append(struct.pack('<8Q', *(int(word, 16) for word in match[2].split(':'))))
    blob = b''.join(chunks)
    require(blob[BYTES:] == bytes(32), 'nonzero final chunk padding')
    decoded = decode(blob[:BYTES])
    require(decoded['checksum'] == int(begin[1], 16) and decoded['diagnostic_pass'] == bool(int(end[1])),
            'summary contradicts copied bytes')
    return {**decoded, 'status': 'HANDOFF_COMPLETE', 'scheduled_record_seen': scheduled,
            'log_sha256': hashlib.sha256(raw).hexdigest(), 'log_origin_authenticated': False,
            'same_boot_origin_authenticated': False, 'logger_delay_verified': False,
            'smem_bytes_acquired': False, 'smem_access_authorized': False,
            'running_secure_identity_authenticated': False, 'gpu_admission_changed': False,
            'gpu_execution': False}


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
