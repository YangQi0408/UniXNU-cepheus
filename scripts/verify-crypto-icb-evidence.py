#!/usr/bin/env python3
"""Verify the accepted boot's configured crypto route and Hash ICB request.

Offline original-byte evidence only; never execute firmware or access a device.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct

SPEC = importlib.util.spec_from_file_location(
    'icb_verify', Path(__file__).with_name('verify-icb-registrar-evidence.py'))
ICB = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ICB)
require = ICB.require

TREE_PATH = (0x30f30, 0x314f0, 0x31800, 0x3aa10, 0x3b370, 0x3b568, 0x3c410, 0x3c888)
TREE_KEYS = (0x3100000125, 0x800000000, 0x2900000000, 0x1f00000000,
             0x130000001a, 0x13000000a6, 0x1700000054, 0x1700000000)
ROUTE_NODES = (
    ((0x3c8e8, 'qxm_crypto', 23, 8, 1), (0x3abd0, 'qns_a2noc_snoc', 143, 16, 1)),
    ((0x3acd0, 'qnm_aggre2_noc', 112, 16, 1), (0x3ae10, 'qns_gemnoc_sf', 104, 16, 1)),
    ((0x3af10, 'qnm_snoc_sf', 76, 16, 1), (0x31bd0, 'qns_llcc', 243, 16, 4)),
    ((0x31cd0, 'llcc_mc', 180, 4, 4), (0x31e10, 'ebi', 0, 4, 4)))


def covered(reads, address, size):
    require(size > 0 and any(start <= address and address + size <= start + length
                            for start, length in reads),
            'missing evidence coverage: ' + hex(address))


def read(blob, segments, reads, address, size):
    covered(reads, address, size)
    return ICB.NPA.span(blob, segments, address, size)


def string(blob, segments, reads, address):
    value = ICB.cstring(blob, segments, address)
    covered(reads, address, len(value) + 1)
    return value


def lookup(blob, segments, reads, root, key, budget=64):
    require(0 < budget <= 64 and 0 <= key < 1 << 64, 'invalid tree budget/key')
    path, seen = [], set()
    while root:
        require(root not in seen, 'route tree cycle')
        require(len(path) < budget, 'route tree budget exceeded')
        require(root % 8 == 0, 'unaligned route tree record')
        seen.add(root)
        stored, route, left, right = struct.unpack('<4Q', read(blob, segments, reads, root, 32))
        path.append({'address': hex(root), 'key': hex(stored), 'route': hex(route),
                     'left': hex(left), 'right': hex(right)})
        if stored == key:
            require(route != 0, 'null matched route')
            return route, path
        # Match SUB followed by signed GT, including its 64-bit wraparound.
        delta = (stored - key) & ((1 << 64) - 1)
        root = left if delta < 1 << 63 else right
    raise ValueError('crypto route not found')


def crypto_route(blob, segments, reads):
    require(struct.unpack('<I', read(blob, segments, reads, 0x2dfd8, 4))[0] == 1,
            'unexpected topology count')
    require(struct.unpack('<Q', read(blob, segments, reads, 0x2dfe0, 8))[0] == 0x2dfb8,
            'unexpected topology table')
    require(struct.unpack('<Q', read(blob, segments, reads, 0x2dfb8, 8))[0] == 0x30ed8,
            'unexpected topology pointer')
    topology = read(blob, segments, reads, 0x30ed8, 24)
    drv, root = struct.unpack_from('<I', topology)[0], struct.unpack_from('<Q', topology, 16)[0]
    require((drv, root) == (2, TREE_PATH[0]), 'unexpected configured DRV/tree root')
    route, path = lookup(blob, segments, reads, root, 23 << 32)
    require(tuple(int(item['address'], 16) for item in path) == TREE_PATH and
            tuple(int(item['key'], 16) for item in path) == TREE_KEYS,
            'unexpected crypto tree path')
    require(route == 0x3cb28, 'unexpected crypto route pointer')
    count, pairs = struct.unpack('<I4xQ', read(blob, segments, reads, route, 16))
    require((count, pairs) == (4, 0x3cb78), 'unexpected route count/pairs')
    pointers = struct.unpack('<8Q', read(blob, segments, reads, pairs, 64))
    entries = []
    for index, expected_pair in enumerate(ROUTE_NODES):
        entry = {}
        for side, (address, name, node_id, width, ports) in zip(('master', 'slave'), expected_pair):
            require(pointers[index * 2 + (side == 'slave')] == address, 'unexpected route node pointer')
            raw = read(blob, segments, reads, address, 48)
            pointer, observed_id = struct.unpack_from('<QI', raw)
            observed_width = struct.unpack_from('<I', raw, 24)[0]
            port_offset = 40 if side == 'master' else 28
            observed_ports = struct.unpack_from('<I', raw, port_offset)[0]
            require((string(blob, segments, reads, pointer), observed_id, observed_width, observed_ports)
                    == (name, node_id, width, ports), 'unexpected route node fields')
            entry[side] = {'address': hex(address), 'name': name, 'id': node_id,
                           'width_field': width, 'port_field': ports, 'port_offset': hex(port_offset)}
        entries.append(entry)
    return {'topology': '0x30ed8', 'configured_drv': drv, 'tree_key': hex(23 << 32),
            'tree_path': path, 'route': hex(route), 'pairs': hex(pairs), 'entries': entries}


def instructions(blob, segments, reads, ranges, checks):
    for address, size in ranges:
        covered(reads, address, size)
    return [ICB.instruction(blob, segments, *item) for item in checks]


def hash_request(blob, segments, reads):
    constants = struct.unpack('<2Q', read(blob, segments, reads, 0xb010, 16))
    require(constants == (1600000000, 55250000), 'unexpected Hash bandwidth constants')
    require(string(blob, segments, reads, 0xb020) == '/clk/ce1' and
            string(blob, segments, reads, 0xbeb0) == '/icb/arbiter', 'unexpected Hash client/resource')
    checks = (
        (0x5148, 'add', 'x29, sp, #0x50'), (0x514c, 'adrp', 'x8, #0xb000'),
        (0x5150, 'str', 'wzr, [sp, #0x28]'), (0x5154, 'mov', 'w9, #0x17'),
        (0x5158, 'mov', 'w10, #4'), (0x515c, 'ldr', 'q0, [x8, #0x10]'),
        (0x5160, 'movk', 'w10, #1, lsl #16'), (0x5164, 'stur', 'x9, [x29, #-0x18]'),
        (0x516c, 'str', 'w10, [sp, #0x10]'), (0x5170, 'str', 'q0, [sp]'),
        (0x5174, 'ldp', 'x10, x8, [sp]'), (0x5178, 'str', 'x8, [sp, #0x20]'),
        (0x5180, 'str', 'x10, [sp, #0x18]'),
        (0x51a8, 'adrp', 'x0, #0xb000'), (0x51ac, 'add', 'x0, x0, #0x20'),
        (0x51b0, 'sub', 'x1, x29, #0x18'), (0x51b4, 'mov', 'w2, #1'),
        (0x51bc, 'bl', '#0x9e08'), (0x51c8, 'add', 'x1, sp, #0x10'),
        (0x51cc, 'mov', 'w2, #1'), (0x51d4, 'bl', '#0x9d90'),
        (0x9e08, 'mov', 'x8, x1'), (0x9e0c, 'mov', 'x9, x0'),
        (0x9e10, 'adrp', 'x0, #0xb000'), (0x9e14, 'lsl', 'w3, w2, #3'),
        (0x9e18, 'add', 'x0, x0, #0xeb0'), (0x9e1c, 'mov', 'w2, #0x400'),
        (0x9e20, 'mov', 'x1, x9'), (0x9e24, 'mov', 'x4, x8'), (0x9e28, 'b', '#0x9e34'),
        (0x9eb8, 'ldr', 'x10, [x10, #0x10]'), (0x9ec4, 'blr', 'x10'),
        (0x9da4, 'ldr', 'x10, [x9, #0x28]'), (0x9dac, 'ldr', 'w10, [x10]'),
        (0x9db0, 'cmp', 'w10, w2'), (0x9db4, 'b.ne', '#0x9df8'),
        (0x9dc0, 'lsl', 'w1, w2, #3'), (0x9dcc, 'bl', '#0x9ee0'),
        (0x9f4c, 'ldr', 'x10, [x10, #0x90]'), (0x9f50, 'blr', 'x10'))
    decoded = instructions(blob, segments, reads,
                           ((0x5138, 196), (0x9e08, 36), (0x9e34, 172),
                            (0x9d90, 120), (0x9ee0, 132)), checks)
    return {'client_name': '/clk/ce1', 'resource': '/icb/arbiter',
            'master': 23, 'slave': 0, 'client_type': '0x400', 'pair_count': 1,
            'create_pair_bytes': 8, 'issue_vector_words': 8, 'request_bytes': 32,
            'request_type': '0x10004', 'ib': constants[0], 'ab': constants[1], 'latency': 0,
            'field_offsets': {'type': 0, 'ib': 8, 'ab': 16, 'latency': 24},
            'padding': 'Not initialized by the examined request construction; not semantic fields',
            'instructions': decoded}


def clock_abi(blob, segments, reads):
    checks = (
        (0xdc70, 'ldr', 'w9, [x8, #8]'), (0xdc78, 'b.ls', '#0xdca8'),
        (0xdc84, 'ldr', 'x10, [x8, #0x10]'), (0xdc88, 'mov', 'w8, w2'),
        (0xdc8c, 'bfi', 'x8, x1, #0x20, #0x20'),
        (0xdc94, 'ldr', 'x11, [x10, w0, uxtw #3]'),
        (0xdc98, 'ldr', 'x0, [x11, #0x10]'), (0xdc9c, 'bl', '#0xdd7c'),
        (0xdd7c, 'b', '#0xdda0'), (0xdd80, 'ldr', 'x8, [x0]'),
        (0xdd84, 'sub', 'x8, x8, x1'), (0xdd88, 'cbz', 'x8, #0xdda8'),
        (0xdd8c, 'add', 'x9, x0, #0x10'), (0xdd90, 'add', 'x10, x0, #0x18'),
        (0xdd94, 'cmp', 'x8, #0'), (0xdd98, 'csel', 'x11, x9, x10, gt'),
        (0xdd9c, 'ldr', 'x0, [x11]'), (0xdda0, 'cbnz', 'x0, #0xdd80'),
        (0xdda4, 'ret', ''), (0xdda8, 'ldr', 'x0, [x0, #8]'), (0xddac, 'ret', ''),
        (0xcc4c, 'and', 'w9, w8, #0xffff'), (0xcc50, 'cmp', 'w9, #4'),
        (0xcc54, 'b.eq', '#0xccd4'), (0xccd4, 'ldr', 'x4, [x20, #8]'),
        (0xccd8, 'str', 'x4, [x19]'), (0xccdc, 'ldr', 'x5, [x20, #0x10]'),
        (0xcce0, 'str', 'x5, [x19, #8]'), (0xcce4, 'ldrb', 'w13, [x20, #2]'),
        (0xcce8, 'tbz', 'w13, #0, #0xcd28'),
        (0xccec, 'ldr', 'w14, [x20, #0x18]'), (0xccf4, 'str', 'w14, [x19, #0x18]'))
    return {'route_lookup': '0xdc64 -> 0xdd7c -> 0xdda0',
            'search_body': '0xdd80..0xddaf (canonical entry is not body start)',
            'key_formula': '(master << 32) | slave', 'parser': '0xcc24',
            'type_mask': '0xffff', 'ib_offset': 8, 'ab_offset': 16,
            'latency_flag': '0x10000', 'latency_offset': 24,
            'instructions': instructions(blob, segments, reads,
                                         ((0xdc64, 76), (0xdd7c, 52), (0xcc24, 300)), checks)}


def verify(root):
    accepted = ICB.NPA.verify(root)
    modules = {item['name']: item for item in accepted['modules']}
    binaries, evidence = {}, {}
    for name, filename in (('DALSys', 'dal-crypto-route-final-evidence.json'),
                           ('ClockDxe', 'clock-crypto-route-final-evidence.json'),
                           ('HashDxe', 'hash-crypto-abi-final-evidence.json')):
        module = modules[name]
        blob = (root / 'uefi' / module['source']).read_bytes()
        raw = (root / 'logs/pas13-instruction-replay-20261110' / filename).read_bytes()
        segments, reads = ICB.checked_reads(blob, module['sha256'], json.loads(raw))
        binaries[name] = blob, segments, reads
        evidence[name] = {'module_sha256': module['sha256'],
                          'bundle_sha256': hashlib.sha256(raw).hexdigest(),
                          'read_evidence_ids': list(reads.values())}
    route = crypto_route(*binaries['DALSys'])
    request = hash_request(*binaries['HashDxe'])
    parser = clock_abi(*binaries['ClockDxe'])
    reference = json.loads((Path(__file__).resolve().parents[1] /
                            'docs/crypto-icb-reference.json').read_text())
    return {'status': 'PASS_STATIC_BYTES_ONLY', 'boot_sha256': accepted['boot_sha256'],
            'evidence': evidence, 'crypto_route': route, 'hash_request': request,
            'clock_abi': parser, 'reference_correlation': reference,
            'unknowns': ['Configured path and generated request are not observed live votes or ACKs',
                         'Reference source is a different build/platform, not firmware provenance',
                         'BCM mapping/units, aggregation, last EBS writes and XNU ownership remain unverified',
                         'PAS13=false, secure PA/XPU, crypto lifecycle and SMMU remain unresolved',
                         'This receipt authorizes no PAS authentication or GPU execution']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = verify(args.root.resolve())
    with args.output.open('x') as output:
        json.dump(result, output, indent=2)
        output.write('\n')
    print(result['status'])


if __name__ == '__main__':
    main()
