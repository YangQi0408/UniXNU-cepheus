#!/usr/bin/env python3
"""Bind private SmemDxe evidence to fixed EFI/boot bytes; never execute them."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import uuid
import zlib


def module(name, file):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(file))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


PE = module('smem_pe', 'verify-npa-state-evidence.py')
LLVM = module('smem_llvm', 'verify-tz-evidence.py')
SHA = '4553d85a7cfe19d9f7642822ce02799ac796faf17843391bc75db3b2db7c4a0e'
FFS = 'F541D663-4A48-40AA-AABF-FF158CCAE34C'
CODE = ((0x13a4, 192), (0x1e60, 616), (0x23c0, 232), (0x25c4, 292),
        (0x2aa4, 84), (0x2d58, 896), (0x30d8, 780), (0x33e4, 348))
DATA = ((0x8040, 176), (0x63c8, 56), (0x6500, 56), (0x8100, 320))


def require(condition, message):
    if not condition:
        raise ValueError(message)


def verify(blob, bundle, boot):
    require(len(blob) == 40960 and hashlib.sha256(blob).hexdigest() == SHA,
            'fixed EFI identity mismatch')
    require(hashlib.sha256(boot).hexdigest() == PE.BOOT_SHA, 'accepted boot identity mismatch')
    size = struct.unpack_from('<I', boot, 8)[0]
    stream = zlib.decompressobj(31)
    payload = stream.decompress(boot[4096:4096 + size], 0x300081)
    require(stream.eof and len(payload) == 0x300080 and not stream.unconsumed_tail,
            'firmware payload mismatch')
    _, inner = PE.PAIR.inner_files(payload[128:])
    embedded = [raw for kind, raw in PE.PAIR.fv.sections(inner[FFS][24:]) if kind == 0x10]
    require(embedded == [blob], 'embedded SmemDxe mismatch')
    segments = PE.pe_segments(blob)
    pe = struct.unpack_from('<I', blob, 60)[0]
    require(struct.unpack_from('<H', blob, pe + 24 + 68)[0] == 11,
            'not an EFI boot-service driver')
    records = bundle['records']
    require(len(records) == 35, 'unexpected evidence scope')
    decoded, bindings = {}, []
    for address, size in CODE + DATA:
        matches = [r for r in records if r['operation'] == 'read_bytes'
                   and r['normalized_result']['address'] == hex(address)
                   and r['normalized_result']['requested_bytes'] == size]
        require(len(matches) == 1, 'missing/ambiguous byte evidence')
        record = matches[0]
        observed = record['normalized_result']
        raw = PE.span(blob, segments, address, size)
        require(record['subject']['digest']['sha256'] == SHA and observed['complete']
                and observed['returned_bytes'] == size
                and bytes.fromhex(observed['bytes_hex']) == raw, 'byte binding mismatch')
        bindings.append({'address': hex(address), 'bytes': size,
                         'sha256': hashlib.sha256(raw).hexdigest(),
                         'evidence_id': record['evidence_id']})
        if (address, size) in CODE:
            decoded.update((int(i['address'], 16), i) for i in PE.decode(raw, address))

    def original(address, size):
        return PE.span(blob, segments, address, size)

    require(original(0x8048, 16) == uuid.UUID('f4e5c7d0-d239-47cb-aacd-7f66ef763238').bytes_le,
            'registered GUID mismatch')
    require(struct.unpack('<5Q', original(0x8078, 40)) ==
            (0x10001, 0x13f4, 0x1420, 0x1464, 0x1478), 'protocol table mismatch')
    require(struct.unpack('<4Q', original(0x63c8, 32)) ==
            (0x24ac, 0x25c4, 0x26e8, 0x276c), 'v11 table mismatch')
    require(struct.unpack('<4Q', original(0x6500, 32)) ==
            (0x2a44, 0x2aa4, 0x2af8, 0x2d58), 'v12 table mismatch')
    expected = {
        0x13b0: ('str', 'xzr, [sp, #8]'),
        0x13b4: ('bl', '#0x1e60'),
        0x13c8: ('add', 'x1, x1, #0x48'),
        0x13cc: ('add', 'x2, x2, #0x78'),
        0x13d8: ('ldr', 'x8, [x9, #0x148]'),
        0x13e4: ('blr', 'x8'),
        0x1434: ('add', 'x1, sp, #0xc'),
        0x143c: ('bl', '#0x1fc8'),
        0x1440: ('ldr', 'w9, [sp, #0xc]'),
        0x1448: ('mov', 'x0, xzr'),
        0x144c: ('str', 'x8, [x19]'),
        0x1450: ('str', 'w9, [x20]'),
        0x1ffc: ('bl', '#0x1e60'),
        0x2014: ('ldr', 'x8, [x8, #8]'),
        0x2030: ('blr', 'x8'),
        0x205c: ('mov', 'x21, xzr'),
        0x1f70: ('bl', '#0x30d8'),
        0x1f80: ('mov', 'w0, #3'),
        0x1f84: ('mov', 'w2, #-0x10000'),
        0x1f8c: ('bl', '#0x23c0'),
        0x23e8: ('ldr', 'x9, [x8, #0x108]'),
        0x23ec: ('add', 'x20, x9, #0x40'),
        0x2428: ('str', 'w10, [x20, #0x1c]'),
        0x2640: ('bl', '#0x3538'),
        0x265c: ('cmp', 'w11, #1'),
        0x2694: ('bl', '#0x353c'),
        0x26b4: ('add', 'x20, x9, x11'),
        0x2ad0: ('bl', '#0x2d58'),
        0x2aec: ('csel', 'x0, x8, xzr, eq'),
        0x2db4: ('str', 'w8, [x20, #0x14]'),
        0x2dc0: ('str', 'w1, [x20, #0x10]'),
        0x2e28: ('and', 'x8, x9, #0xffffffff'),
        0x2e58: ('bl', '#0x2f94'),
        0x3054: ('str', 'x10, [x1, #0x10]'),
        0x3074: ('str', 'x12, [x1, #0x10]'),
        0x30b8: ('str', 'w12, [x1, #8]'),
        0x3484: ('ldr', 'w8, [x13]'),
        0x3488: ('ldr', 'w9, [x14]'),
        0x3490: ('bfi', 'x8, x9, #0x20, #0x20'),
        0x34c8: ('str', 'x8, [x16, #8]'),
        0x3534: ('ret', ''), 0x3538: ('ret', ''), 0x353c: ('ret', ''),
    }
    for address, pair in expected.items():
        ins = decoded[address]
        require((ins['mnemonic'], ins['operands']) == pair,
                f'critical instruction mismatch at {address:#x}')
    # No store initializes the wrapper's size temporary before delegation.
    prefix = [decoded[a] for a in range(0x1420, 0x143c, 4)]
    require(all('#0xc]' not in i['operands'] for i in prefix if i['mnemonic'].startswith('st')),
            'wrapper size initialization changed')
    llvm = LLVM.llvm_check(blob, {'functions': [{'instructions': [
        {'bytes': decoded[a]['bytes_hex']} for a in sorted(expected)]}]})
    return {'status': 'PASS_STATIC_BYTES_ONLY', 'efi_sha256': SHA, 'boot_sha256': PE.BOOT_SHA,
            'embedded_bytes_equal': True, 'bindings': bindings,
            'critical_instructions': len(expected), 'llvm': llvm,
            'protocol_rva': '0x8078', 'get_rva': '0x1420',
            'phone_operations': False, 'firmware_execution': False, 'gpu_execution': False,
            'observations': ['Getter returns EFI_SUCCESS unconditionally after delegate returns.',
                             'Size temporary can remain uninitialized on early delegate failure.',
                             'Lazy initialization writes SMEM base+0x5c via item3 version setter.',
                             'Both getter lock hooks are RET-only in this exact image.',
                             'Getters return shared backing pointers, not copies or leases.',
                             'V12 getter mutates local partition cache and truncates traversal base to32 bits.'],
            'limits': ['Historical embedded identity is not actual protocol dispatch or current RAM identity.',
                       'No lifetime/coherency proof; no post-ExitBootServices protocol use authorized.',
                       'No full helper/debug call-graph side-effect audit or live pointer access.',
                       'No secure image authentication, resource owner, PAS13 or GPU admission proof.']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('efi', 'bundle', 'boot', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    require(not args.output.exists(), 'preserve previous output')
    bundle_raw = args.bundle.read_bytes()
    result = verify(args.efi.read_bytes(), json.loads(bundle_raw), args.boot.read_bytes())
    result.update(bundle_sha256=hashlib.sha256(bundle_raw).hexdigest(),
                  script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    result['helper_sha256'] = {
        name: hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest()
        for name in ('verify-npa-state-evidence.py', 'verify-tz-evidence.py',
                     'verify-scm-pair-boot.py', 'repack-scm-pair-fv.py', 'adt.py')}
    with args.output.open('x') as output:
        json.dump(result, output, indent=2)
        output.write('\n')
    print(f"PASS: 12 REA byte ranges, historical embedded EFI, {result['critical_instructions']} instructions; static only")


if __name__ == '__main__':
    main()
