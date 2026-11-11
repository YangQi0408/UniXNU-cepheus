#!/usr/bin/env python3
"""Check fixed TZ monitor evidence statically; never execute firmware or a device."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct

SPEC = importlib.util.spec_from_file_location(
    'tz_verify', Path(__file__).with_name('verify-tz-evidence.py'))
VERIFY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VERIFY)

CODE = (
    (0x1c0180f0, 380), (0x1c01c75c, 2804), (0x1c01d6b0, 104),
    (0x1c01d4c4, 292), (0x1c01dde4, 272), (0x1c01d9f0, 132),
    (0x1c01d8a4, 88), (0x1c01db00, 60), (0x1c01c5e0, 380),
    (0x1c01dd34, 176), (0x1c01d5e8, 12), (0x1c03ba58, 60),
    (0x1c056d74, 1240), (0x14690330, 236), (0x1c0540ec, 104),
    (0x14690a84, 204),
)
DATA = ((0x1c020438, 576), (0x1c020670, 56),
        (0x1c0206a8, 32), (0x1c1303f8, 3648))


def require(condition, message):
    if not condition:
        raise ValueError(message)


def verify(blob, bundle):
    from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
    require(len(blob) == 4194304 and hashlib.sha256(blob).hexdigest()
            == VERIFY.TZ_SHA256, 'fixed TZ identity mismatch')
    records = bundle['records']
    require(len(records) == 481, 'unexpected evidence bundle scope')
    segments = VERIFY.load_segments(blob)
    decoder = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    decoded, bindings = {}, []

    def original(address, size):
        offset, flags = VERIFY.map_range(segments, address, size)
        return blob[offset:offset + size], flags

    for address, size in CODE + DATA:
        matches = [r for r in records if r['operation'] == 'read_bytes'
                   and int(r['normalized_result']['address'], 16) == address
                   and r['normalized_result']['requested_bytes'] == size]
        require(len(matches) == 1, 'missing/ambiguous byte evidence')
        record = matches[0]
        observed = record['normalized_result']
        raw, flags = original(address, size)
        require(record['subject']['digest']['sha256'] == VERIFY.TZ_SHA256
                and observed['complete'] and observed['returned_bytes'] == size
                and bytes.fromhex(observed['bytes_hex']) == raw, 'byte binding mismatch')
        bindings.append({'address': hex(address), 'bytes': size,
                         'sha256': hashlib.sha256(raw).hexdigest(),
                         'evidence_id': record['evidence_id']})
        if (address, size) in CODE:
            instructions = list(decoder.disasm(raw, address))
            require(flags & 1 and len(instructions) * 4 == size, 'incomplete code decode')
            decoded.update((i.address, i) for i in instructions)

    # The creator has analyzed assembly but no retained read_bytes record.
    creator, flags = original(0x1c0671a4, 120)
    creator_instructions = list(decoder.disasm(creator, 0x1c0671a4))
    require(flags & 1 and len(creator_instructions) == 30, 'incomplete envelope creator')
    dossier = [r for r in records if r['operation'] == 'analyze_function'
               and r['parameters'].get('procedure') == '0x1c0671a4']
    require(len(dossier) == 1 and dossier[0]['subject']['digest']['sha256']
            == VERIFY.TZ_SHA256, 'missing/foreign envelope creator dossier')
    assembly = dossier[0]['normalized_result']['assembly']
    require(len(assembly) == 30 and all(line.startswith(hex(i.address) + ': ')
            for line, i in zip(assembly, creator_instructions)), 'creator assembly extent mismatch')
    decoded.update((i.address, i) for i in creator_instructions)
    bindings.append({'address': '0x1c0671a4', 'bytes': 120,
                     'sha256': hashlib.sha256(creator).hexdigest(),
                     'evidence_id': dossier[0]['evidence_id'],
                     'scope': 'original-file decode and analyzed assembly extent; no REA byte record'})

    expected = {
        0x1c018104: ('cmp', 'x4, #0x17'),
        0x1c018110: ('cmp', 'x4, #0x13'),
        0x1c0181c0: ('mrs', 'x1, scr_el3'),
        0x1c0181c4: ('mov', 'x4, #0xe00'),
        0x1c0181c8: ('msr', 'scr_el3, x4'),
        0x1c0181d0: ('bl', '#0x1c01c75c'),
        0x1c01c784: ('mov', 'w20, w1'),
        0x1c01c7bc: ('tbnz', 'w20, #0, #0x1c01c848'),
        0x1c01c930: ('bl', '#0x1c01dc40'),
        0x1c01c938: ('bl', '#0x1c01db3c'),
        0x1c01c984: ('mov', 'x0, x22'),
        0x1c01c988: ('bl', '#0x1c01d8a4'),
        0x1c01c998: ('ldr', 'x2, [x8, #0xe0]'),
        0x1c01c9a4: ('bl', '#0x1c01dde4'),
        0x1c01d6c0: ('add', 'x9, x9, #0x6a8'),
        0x1c01d6c8: ('add', 'x0, x0, #0x138'),
        0x1c01d6cc: ('ldr', 'x8, [x9, x8, lsl #3]'),
        0x1c01d6d8: ('add', 'x0, x0, #0x338'),
        0x1c01d6ec: ('add', 'x0, x0, #0x538'),
        0x1c01d708: ('add', 'x9, x9, #0x738'),
        0x1c01d70c: ('add', 'x0, x9, x8, lsl #9'),
        0x1c01d53c: ('mov', 'w10, #1'),
        0x1c01d578: ('strb', 'w10, [x8, #0x130]'),
        0x1c01d5c4: ('mov', 'w12, #0x108'),
        0x1c01d5d8: ('str', 'xzr, [x10]'),
        0x1c01d5dc: ('str', 'x12, [x11, #8]'),
        0x1c01de18: ('add', 'x0, x23, #0x108'),
        0x1c01de1c: ('add', 'x1, x19, #0x108'),
        0x1c01de20: ('mov', 'w2, #0xf0'),
        0x1c01de2c: ('bl', '#0x14682d48'),
        0x1c01de4c: ('bl', '#0x1c01d9f0'),
        0x1c01de6c: ('orr', 'x9, x9, #0x1c0'),
        0x1c01debc: ('bl', '#0x1c01d8a4'),
        0x1c01da1c: ('str', 'x2, [x0, #0x1b8]'),
        0x1c01da20: ('add', 'x8, x8, #0xf8'),
        0x1c01da28: ('str', 'xzr, [x19, x8]'),
        0x1c01da38: ('ldr', 'x2, [x20, w0, uxtw #3]'),
        0x1c01da40: ('mov', 'w8, #0x100'),
        0x1c01da60: ('str', 'x2, [x19, x8]'),
        0x1c01d8c4: ('add', 'x0, x19, #0x108'),
        0x1c01d8c8: ('bl', '#0x1c018470'),
        0x1c01ca90: ('and', 'w8, w8, #0x7f'),
        0x1c01ca94: ('sub', 'w8, w8, #1'),
        0x1c01caa4: ('add', 'x9, x9, #0x670'),
        0x1c01d234: ('bl', '#0x1c01db00'),
        0x1c01db00: ('ldr', 'x8, [x1, #8]'),
        0x1c01db04: ('str', 'x8, [x0, #0x18]'),
        0x1c01db08: ('ldr', 'x8, [x1, #0x10]'),
        0x1c01db0c: ('str', 'x8, [x0, #0x20]'),
        0x1c01db10: ('ldr', 'x8, [x1, #0x18]'),
        0x1c01db14: ('str', 'x8, [x0, #0x28]'),
        0x1c01d244: ('bl', '#0x1c01c5e0'),
        0x1c01c640: ('ldr', 'x8, [x19, #0x30]'),
        0x1c01c644: ('cbz', 'x8, #0x1c01c65c'),
        0x1c01c64c: ('ldr', 'x9, [x9]'),
        0x1c01c650: ('add', 'x9, x9, #0x30'),
        0x1c01c658: ('str', 'x8, [x21, x9]'),
        0x1c01d000: ('ldrb', 'w8, [x19, #8]'),
        0x1c01d004: ('tbz', 'w8, #7, #0x1c01d018'),
        0x1c01d014: ('bl', '#0x1c01dd34'),
        0x1c01d028: ('bl', '#0x1c01dc68'),
        0x1c01d030: ('bl', '#0x1c01d8a4'),
        0x1c01d09c: ('orr', 'w9, w9, #1'),
        0x1469040c: ('bl', '#0x1c0540ec'),
        0x1c054138: ('bl', '#0x1c03b95c'),
        0x1c054150: ('b', '#0x1c0671a4'),
        0x1c0671b8: ('add', 'x9, sp, #8'),
        0x1c0671bc: ('mov', 'x19, x0'),
        0x1c0671d8: ('mov', 'w8, #1'),
        0x1c0671dc: ('str', 'x19, [sp, #0x38]'),
        0x1c0671e0: ('mov', 'w9, #0x87'),
        0x1c0671e4: ('add', 'x0, sp, #8'),
        0x1c0671e8: ('str', 'x8, [sp, #8]'),
        0x1c0671ec: ('stp', 'x9, x8, [sp, #0x10]'),
        0x1c0671f0: ('bl', '#0x1c103540'),
        0x14690b4c: ('eret', ''),
        0x1c03ba5c: ('add', 'x8, x8, #0xaa8'),
        0x1c03ba60: ('add', 'x8, x8, #8'),
        0x1c03ba64: ('mov', 'x9, #-1'),
        0x1c03ba6c: ('cmp', 'w10, #4'),
        0x1c03ba70: ('b.eq', '#0x1c03ba8c'),
        0x1c03ba78: ('add', 'x8, x8, #0x160'),
        0x1c03ba7c: ('cmp', 'x9, #7'),
        0x1c03ba80: ('b.le', '#0x1c03ba68'),
        0x1c03ba8c: ('mov', 'w0, #1'),
        0x1c056fc4: ('bl', '#0x1c03ba58'),
        0x1c056fc8: ('ldr', 'w8, [x22, #0xc]'),
        0x1c056fcc: ('tbz', 'w0, #0, #0x1c056ff4'),
        0x1c056fd0: ('tbnz', 'w8, #9, #0x1c056ff4'),
        0x1c056fe8: ('b', '#0x1c0571a4'),
        0x1c0571a4: ('mov', 'w24, #-0xc'),
    }
    for address, pair in expected.items():
        ins = decoded[address]
        require((ins.mnemonic, ins.op_str) == pair,
                f'critical instruction mismatch at {address:#x}')

    def table(address, count):
        return struct.unpack('<' + 'Q' * count, original(address, count * 8)[0])

    require(table(0x1c020438, 1) == (0x1c01ca8c,), 'outer command1 mapping mismatch')
    subs = table(0x1c020670, 7)
    require(subs[2] == 0x1c01d1c4 and subs[6] == 0x1c01d22c,
            'secure response mapping mismatch')
    require(table(0x1c0206a8, 4) ==
            (0x1c01d6e4, 0x1c01d6d4, 0x1c01d6e8, 0x1c01d6f4),
            'context selector mapping mismatch')
    pas7 = struct.unpack('<IIIIQ', original(0x1c130458, 24)[0])
    require(pas7 == (0, 0x02000207, 1, 1, 0x1c04f444), 'PAS7 entry mismatch')
    llvm = VERIFY.llvm_check(blob, {'functions': [{'instructions': [
        {'bytes': decoded[a].bytes.hex()} for a in sorted(expected)]}]})
    return {
        'status': 'PASS_STATIC_BYTES_ONLY', 'bindings': bindings,
        'critical_instructions': len(expected), 'llvm': llvm,
        'monitor_contexts': ['0x1c029138', '0x1c029338', '0x1c029538',
                             '0x1c029738 + cpu * 0x200'],
        'interrupt_token_envelope_offset': '0x30',
        'interrupt_token_normal_context_offset_when_initialized': '0x30 (x6)',
        'pas7_entry': list(pas7), 'pending_state4_prediction': 'BUSY (-12)',
        'phone_operations': False, 'firmware_execution': False, 'gpu_execution': False,
        'limits': [
            'Static initialization values are not measured running values.',
            'Monitor CPU contexts and system registers are not scheduler thread identity.',
            'Envelope token is sp+0x38 minus envelope sp+8, hence +0x30, not +0x38.',
            'BUSY prediction assumes the fixed dispatcher/entry and persistent pending state4.',
            'No full monitor/bootstrap/EL2/EL3 or secure authentication execution.',
            'No running TZ identity, live descriptor, caller priority or false root cause proved.',
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tz', type=Path, required=True)
    parser.add_argument('--bundle', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    blob, bundle_raw = args.tz.read_bytes(), args.bundle.read_bytes()
    receipt = verify(blob, json.loads(bundle_raw))
    receipt.update(tz_sha256=hashlib.sha256(blob).hexdigest(),
                   bundle_sha256=hashlib.sha256(bundle_raw).hexdigest(),
                   script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    with args.output.open('x') as stream:
        json.dump(receipt, stream, indent=2)
        stream.write('\n')
    print(f"PASS: {len(CODE)} REA code ranges, 4 tables, "
          f"{receipt['critical_instructions']} critical instructions; static only")


if __name__ == '__main__':
    main()
