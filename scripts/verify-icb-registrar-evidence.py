#!/usr/bin/env python3
"""Verify the accepted boot's DAL -> Clock ICB static registration chain.

No firmware execution, protocol invocation, device, SMC or MMIO access.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct

SPEC = importlib.util.spec_from_file_location('npa_verify', Path(__file__).with_name('verify-npa-state-evidence.py'))
NPA = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(NPA)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def checked_reads(blob, digest, evidence):
    segments = NPA.pe_segments(blob)
    reads = {}
    for record in evidence['records']:
        require(record['subject']['digest']['sha256'] == digest, 'evidence target mismatch')
        if record['operation'] != 'read_bytes':
            continue
        result = record['normalized_result']
        address, size = int(result['address'], 16), result['requested_bytes']
        raw = bytes.fromhex(result['bytes_hex'])
        require(result['complete'] and result['returned_bytes'] == size and len(raw) == size,
                'incomplete evidence read')
        require(raw == NPA.span(blob, segments, address, size), 'evidence bytes differ from original')
        key = (address, size)
        require(key not in reads, 'duplicate evidence range')
        reads[key] = record['evidence_id']
    return segments, reads


def need_read(reads, address, size):
    require((address, size) in reads, 'missing evidence range: ' + hex(address))


def cstring(blob, segments, address):
    result = bytearray()
    for cursor in range(256):
        byte = NPA.span(blob, segments, address + cursor, 1)[0]
        if byte == 0:
            return result.decode('ascii')
        require(32 <= byte < 127, 'invalid ASCII string')
        result.append(byte)
    raise ValueError('unterminated string')


def instruction(blob, segments, address, mnemonic, operands):
    item, = NPA.decode(NPA.span(blob, segments, address, 4), address)
    require((item['mnemonic'], item['operands']) == (mnemonic, operands),
            'unexpected instruction: ' + hex(address))
    return item


def dal_chain(blob, segments):
    def word(address, width=8):
        return int.from_bytes(NPA.span(blob, segments, address, width), 'little')
    props = 0x7bf0
    prop_bin, structs, count, devices = struct.unpack('<QQI4xQ', NPA.span(blob, segments, props, 32))
    require((prop_bin, structs, count, devices) == (0x1d798, 0x1d248, 17, 0x1dd30),
            'unexpected DAL property table')
    length, name_offset = struct.unpack('<II', NPA.span(blob, segments, prop_bin, 8))
    NPA.span(blob, segments, prop_bin, length)
    matches = [word(devices + i * 40 + 12, 4) for i in range(count)
               if cstring(blob, segments, word(devices + i * 40)) == '/icb/arb']
    require(matches == [0x368], 'unexpected ICB DAL device record')
    cursor = prop_bin + matches[0]
    attr, index, end = struct.unpack('<III', NPA.span(blob, segments, cursor, 12))
    require((attr >> 24, bool(attr & 0x800000), index, end) == (0x12, True, 20, 0xff00ff00),
            'unexpected ICB packed property')
    require(cstring(blob, segments, prop_bin + name_offset + (attr & 0x7fffff)) == 'icb_info',
            'unexpected ICB property name')
    entry = structs + 16 * index
    require(word(entry) == 8, 'unexpected sized structure entry')
    info = word(entry + 8)
    require(info == 0x2dfd0, 'unexpected ICB config pointer')
    node, fabrics, fabric_table, standalone_count, standalone_table = struct.unpack(
        '<QI4xQI4xQ', NPA.span(blob, segments, info, 40))
    require((node, fabrics, fabric_table, standalone_count, standalone_table) ==
            (0x2df78, 1, 0x2dfb8, 2, 0x2dfc0), 'unexpected ICB config')
    resource_count, resources = word(node + 0x30, 4), word(node + 0x38)
    require(resource_count == 1 and resources == 0x30e80, 'unexpected ICB resource table')
    require(cstring(blob, segments, word(node)) == '/icb/arbiter' and
            cstring(blob, segments, word(resources)) == '/icb/arbiter', 'unexpected ICB node name')
    return {'property_table': hex(props), 'device_name': '/icb/arb', 'property_record': hex(cursor),
            'property_name': 'icb_info', 'structure_index': index, 'structure_entry': hex(entry),
            'config': hex(info), 'node': hex(node), 'resource': hex(resources),
            'name': '/icb/arbiter', 'initial_node_driver': hex(word(node + 8)),
            'initial_resource_plugin': hex(word(resources + 0x18)),
            'resource_attributes': hex(word(resources + 0x20, 4)),
            'standalone_count': standalone_count}


def verify(root):
    boot_receipt = NPA.verify(root)
    modules = {item['name']: item for item in boot_receipt['modules']}
    evidence_root = root / 'logs/pas13-instruction-replay-20261110'
    results, binaries = {}, {}
    for name, filename in (('ClockDxe', 'clock-icb-barrier-evidence.json'),
                           ('DALSys', 'dal-icb-config-evidence.json'),
                           ('RpmhDxe', 'rpmh-barrier-slot-evidence.json')):
        module = modules[name]
        blob = (root / 'uefi' / module['source']).read_bytes()
        raw = (evidence_root / filename).read_bytes()
        segments, reads = checked_reads(blob, module['sha256'], json.loads(raw))
        binaries[name] = (blob, segments, reads)
        results[name] = {'module_sha256': module['sha256'], 'evidence_sha256': hashlib.sha256(raw).hexdigest(),
                         'read_evidence_ids': list(reads.values())}
    dal, ds, dr = binaries['DALSys']
    for address, size in ((0x7bf0, 40), (0x1dd30, 680), (0x1db00, 12), (0x1d388, 16),
                          (0x2dfd0, 72), (0x2df78, 72), (0x30e80, 72), (0x1a9fd, 40)):
        need_read(dr, address, size)
    chain = dal_chain(dal, ds)
    clock, cs, cr = binaries['ClockDxe']
    for address, size in ((0xc520, 304), (0xc650, 36), (0xc658, 116), (0xc704, 556),
                          (0xcc24, 300), (0x432a0, 56), (0xa764, 100), (0xa7a0, 64),
                          (0x128fc, 124), (0xe9dc, 440)):
        need_read(cr, address, size)
    checks = ((0xc538, 'bl', '#0xe474'), (0xc574, 'bl', '#0xd9d4'),
              (0xc5d4, 'bl', '#0xa764'), (0xc628, 'bl', '#0xa764'),
              (0xa7dc, 'ldr', 'x10, [x10, #0x20]'),
              (0xc650, 'ldr', 'w0, [x0, #0x30]'), (0xc654, 'ret', ''),
              (0xc754, 'lsr', 'w24, w11, #3'),
              (0xc794, 'add', 'x0, x23, x28, lsl #5'), (0xc79c, 'bl', '#0xcc24'),
              (0xc8ec, 'ldr', 'w1, [x19, #0x80]'), (0xc8f0, 'bl', '#0xdb80'),
              (0xeb78, 'bl', '#0x128fc'), (0x12960, 'ldr', 'x10, [x10, #0xb8]'),
              (0x12964, 'blr', 'x10'), (0x12968, 'ldp', 'x29, x30, [sp, #0x20]'),
              (0x12974, 'ret', ''))
    decoded = [instruction(clock, cs, *item) for item in checks]
    plugin = struct.unpack('<7Q', NPA.span(clock, cs, 0x432a0, 56))
    require(plugin == (0xc704, 0x1400, 0, 0xc930, 0xca0c, 0, 0xcd50), 'unexpected ICB plugin table')
    rpmh, rs, rr = binaries['RpmhDxe']
    need_read(rr, 0xe0f8, 200)
    need_read(rr, 0x18dc, 16)
    require(struct.unpack('<Q', NPA.span(rpmh, rs, 0xe0f8, 8))[0] == 0x10001 and
            struct.unpack('<Q', NPA.span(rpmh, rs, 0xe0f8 + 0xb8, 8))[0] == 0x18dc,
            'unexpected RPMh barrier slot')
    require(NPA.span(rpmh, rs, 0x18dc, 8) == bytes.fromhex('e00b41b2c0035fd6'),
            'unexpected RPMh unsupported return')
    barrier = NPA.decode(NPA.span(rpmh, rs, 0x18dc, 8), 0x18dc)
    return {'status': 'PASS_STATIC_BYTES_ONLY', 'boot_sha256': boot_receipt['boot_sha256'],
            'modules': boot_receipt['modules'], 'evidence': results, 'dal_chain': chain,
            'clock_plugin': {'address': '0x432a0', 'fields_u64': [hex(x) for x in plugin]},
            'rpmh_barrier': {'protocol_revision': '0x10001', 'slot': '0xb8', 'target': '0x18dc',
                             'return': '0x8000000000000003', 'instructions': barrier,
                             'authority': 'original bytes; name correlated with pinned EFIRpmh.h'},
            'exact_instructions': decoded,
            'observations': ['DAL supplies the /icb/arbiter definition via /icb/arb -> icb_info -> structure 20',
                             'Clock 0xc520 installs the ICB plugin and forwards node definitions to NPA slot 0x20',
                             'Clock default node driver 0xc650 returns requested_state; not hardware ACK',
                             'ICB plugin processes eight-word/32-byte requests and forwards attributes to 0xdb80',
                             'Completion 0xe9dc invokes 0x128fc on a conditional branch',
                             'RPMh slot 0xb8 returns EFI_UNSUPPORTED and Clock wrapper does not check its result',
                             'This branch cannot establish hardware completion; no repair or alternate path is executed'],
            'unknowns': ['Runtime registration and active configuration are not observed',
                         'Request ABI units and master/slave IDs require independent correlation',
                         'Complete hardware success/failure and all RPMh completion paths are not verified',
                         'ARC rail registrar, EBS final writers and XNU ownership remain unverified',
                         'No PAS authentication or GPU execution is authorized by this receipt']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    require(not args.output.exists(), 'output exists; preserve prior receipts')
    result = verify(args.root.resolve())
    with args.output.open('x') as output:
        json.dump(result, output, indent=2)
        output.write('\n')
    print(result['status'])


if __name__ == '__main__':
    main()
