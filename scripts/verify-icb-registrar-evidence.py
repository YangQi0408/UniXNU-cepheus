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
import uuid

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


def implicit_churn(blob, segments, reads):
    need_read(reads, 0xe0f8, 200)
    require(struct.unpack('<Q', NPA.span(blob, segments, 0xe0f8 + 0x78, 8))[0] == 0x17f4,
            'unexpected RPMh issue slot')
    for address, size in ((0x17f4, 68), (0x2ec8, 64), (0x2a1c, 148),
                          (0x4014, 160), (0x3f54, 48), (0x561c, 80), (0x735c, 236)):
        need_read(reads, address, size)
    checks = ((0x181c, 'bl', '#0x2cdc'),
              (0x2ec8, 'mov', 'w2, #1'), (0x2ecc, 'mov', 'w3, #1'),
              (0x2ed0, 'mov', 'x0, x19'), (0x2ed4, 'mov', 'w1, w20'),
              (0x2ed8, 'bl', '#0x2a1c'), (0x2a80, 'bl', '#0x4014'),
              (0x4044, 'bl', '#0x3e8c'), (0x4074, 'bl', '#0x3f54'),
              (0x4088, 'bl', '#0x4014'), (0x3f68, 'bl', '#0x561c'),
              (0x3f70, 'cbz', 'w8, #0x3f64'), (0x3f80, 'b', '#0x2b20'),
              (0x5648, 'bl', '#0x735c'),
              (0x73e8, 'add', 'w8, w8, #0xd04'), (0x73f0, 'ldr', 'w9, [x8]'),
              (0x73f4, 'and', 'w9, w9, #0xf'),
              (0x7404, 'movk', 'w10, #2, lsl #16'), (0x7410, 'ldr', 'w11, [x9]'),
              (0x7414, 'and', 'w10, w11, #0xff'))
    decoded = [instruction(blob, segments, *item) for item in checks]
    return {'issue_slot': '0x78', 'issue_wrapper': '0x17f4', 'inner_issue': '0x2cdc',
            'return_wait': '0x2a1c(handle, request_id, 1, 1)',
            'pending_record_lookup': '0x3e8c', 'wait': '0x4014',
            'poll': '0x3f54 -> 0x561c -> 0x735c', 'completion_handler': '0x2b20',
            'hal_irq_offsets': ['base+0xd04 & 0xf', 'base+0x20d04 & 0xff'],
            'instructions': decoded,
            'observation': 'Normal command-set return includes handle-scoped implicit churn; outer barrier failure alone does not establish missing completion',
            'limitations': ['Static bytes, not observed phone execution or a live ACK',
                            'The wait retries while pending records remain; no timeout in this polling loop',
                            'HAL geometry, final EBS writers, resource votes and XNU ownership still require verification']}


def ebs_clear(blob, segments, reads):
    for address, size in ((0xe028, 16), (0x14dc, 152), (0x149c, 20),
                          (0x14b0, 44), (0x38f4, 136), (0x7d2c, 232), (0x7e14, 232)):
        need_read(reads, address, size)
    event_guid = uuid.UUID('27abf055-b1b8-4c26-8048-748f37baa2df')
    require(NPA.span(blob, segments, 0xe028, 16) == event_guid.bytes_le,
            'unexpected EBS event GUID')
    checks = ((0x14f8, 'adrp', 'x2, #0x1000'), (0x1508, 'add', 'x2, x2, #0x49c'),
              (0x14fc, 'adrp', 'x4, #0xe000'), (0x150c, 'add', 'x4, x4, #0x28'),
              (0x1504, 'adrp', 'x5, #0xe000'), (0x1510, 'add', 'x5, x5, #0x288'),
              (0x1514, 'ldr', 'x8, [x8, #0x170]'),
              (0x1518, 'mov', 'w0, #0x200'), (0x151c, 'mov', 'w1, #8'),
              (0x1520, 'mov', 'x3, xzr'), (0x1524, 'blr', 'x8'),
              (0x14a4, 'bl', '#0x14b0'), (0x14ac, 'b', '#0x38f4'),
              (0x14c8, 'add', 'x1, x1, #0x78'), (0x14cc, 'add', 'x2, x2, #0xf8'),
              (0x14d4, 'ldr', 'x4, [x10, #0x150]'), (0x14d8, 'br', 'x4'),
              (0x3938, 'bl', '#0x7d2c'), (0x3944, 'bl', '#0x7e14'),
              (0x3948, 'add', 'w19, w19, #1'), (0x3954, 'b.hi', '#0x3930'),
              (0x3958, 'add', 'w20, w20, #1'), (0x3964, 'b.lo', '#0x391c'),
              (0x7db0, 'mov', 'w10, #0x2a0'), (0x7db8, 'madd', 'w8, w9, w10, w8'),
              (0x7dbc, 'add', 'w8, w8, #0xd14'), (0x7dc0, 'str', 'wzr, [x8]'),
              (0x7dcc, 'mov', 'w11, #0x2a0'), (0x7dd0, 'mov', 'w13, #0xd14'),
              (0x7dd4, 'madd', 'w12, w10, w11, w8'),
              (0x7dd8, 'movk', 'w13, #2, lsl #16'),
              (0x7de0, 'add', 'w9, w12, w13'), (0x7de4, 'str', 'wzr, [x9]'),
              (0x7e98, 'mov', 'w10, #0x2a0'), (0x7ea0, 'madd', 'w8, w9, w10, w8'),
              (0x7ea4, 'add', 'w8, w8, #0xd1c'), (0x7ea8, 'str', 'wzr, [x8]'),
              (0x7eb4, 'mov', 'w11, #0x2a0'), (0x7eb8, 'mov', 'w13, #0xd1c'),
              (0x7ebc, 'madd', 'w12, w10, w11, w8'),
              (0x7ec0, 'movk', 'w13, #2, lsl #16'),
              (0x7ec8, 'add', 'w9, w12, w13'), (0x7ecc, 'str', 'wzr, [x9]'))
    decoded = [instruction(blob, segments, *item) for item in checks]
    loop = NPA.decode(NPA.span(blob, segments, 0x38f4, 136), 0x38f4)
    calls = [item['operands'] for item in loop if item['mnemonic'] in ('bl', 'blr')]
    require(calls == ['#0x7d2c', '#0x7e14'], 'unexpected EBS clear loop calls')
    return {'event_guid': str(event_guid), 'event_type': '0x200', 'notify_tpl': 8,
            'callback': '0x149c', 'uninstall': '0x14b0', 'clear_loop': '0x38f4',
            'control_clear': '0x7d2c', 'command_enable_clear': '0x7e14',
            'control_offsets': ['base+tcs*0x2a0+0xd14', 'base+tcs*0x2a0+0x20d14'],
            'command_enable_offsets': ['base+tcs*0x2a0+0xd1c', 'base+tcs*0x2a0+0x20d1c'],
            'instructions': decoded, 'clear_loop_instructions': loop,
            'observation': 'One EBS callback uninstalls RPMh and clears configured TCS control/command enable; the clear loop has no completion call',
            'limitations': ['Static bytes, not observed execution or successful event registration',
                            'Not a proof that ARC rail votes are cleared',
                            'HAL geometry validation, other EBS writers and callback ordering are not fully verified',
                            'Pre-EBS software vote snapshots do not by themselves prove final XNU ownership']}


def rail_config(dal, ds, dr, clock, cs, cr):
    for address, size in ((0x1d248, 32), (0x1d798, 160), (0x1da10, 156),
                          (0x1f210, 112), (0x7c18, 74)):
        need_read(dr, address, size)
    attr, index = struct.unpack('<II', NPA.span(dal, ds, 0x1da18, 8))
    require((attr, index) == (0x12800011, 1), 'unexpected ClockRailConfig property')
    require(cstring(dal, ds, 0x1d798 + 0x40 + (attr & 0x7fffff)) == 'ClockRailConfig',
            'unexpected ClockRailConfig name')
    require(struct.unpack('<QQ', NPA.span(dal, ds, 0x1d258, 16)) == (8, 0x1f230),
            'unexpected ClockRailConfig structure')
    expected = ('/arc/client/rail_cx', '/vcs/vdd_gx', '/arc/client/rail_mmcx', '/arc/client/rail_mx')
    entries = []
    for i, name in enumerate(expected):
        pointer, value = struct.unpack('<QI4x', NPA.span(dal, ds, 0x1f230 + i * 16, 16))
        require(cstring(dal, ds, pointer) == name and value == 0x100,
                'unexpected ClockRailConfig entry')
        entries.append({'resource': name, 'configured_default': hex(value)})
    require(NPA.span(dal, ds, 0x1f270, 16) == bytes(16), 'unterminated ClockRailConfig')
    for address, size in ((0x16453, 16), (0x1b6c, 120), (0xaf5c, 156), (0x6bec, 188)):
        need_read(cr, address, size)
    require(cstring(clock, cs, 0x16453) == 'ClockRailConfig', 'unexpected Clock consumer property')
    checks = ((0xaf84, 'adrp', 'x0, #0x16000'), (0xaf88, 'add', 'x0, x0, #0x453'),
              (0xaf90, 'bl', '#0x1b6c'), (0xafa4, 'lsl', 'x8, x22, #4'),
              (0xafa8, 'ldr', 'x0, [x21, x8]'), (0xafac, 'cbz', 'x0, #0xafd0'),
              (0xafc4, 'ldr', 'w8, [x8, #8]'), (0xafc8, 'str', 'w8, [x19]'),
              (0x1ba0, 'bl', '#0x93c8'), (0x1ba8, 'ldr', 'x8, [sp, #8]'),
              (0x1bac, 'str', 'x8, [x19]'), (0x6c20, 'bl', '#0xaf5c'),
              (0x6c08, 'add', 'x20, x20, #0xa4c'), (0x6c34, 'bl', '#0xa6e0'))
    decoded = [instruction(clock, cs, *item) for item in checks]
    return {'property_record': '0x1da18', 'property_name': 'ClockRailConfig',
            'structure_index': index, 'structure_entry': '0x1d258', 'table': '0x1f230',
            'entries': entries, 'clock_lookup': '0xaf5c', 'callback': '0x6a4c',
            'instructions': decoded,
            'observation': 'DAL provides configured boot defaults; Clock reads them and registers resource-available callbacks',
            'limitations': ['Configured defaults are not current hardware votes or ARC node registration',
                            'Numeric units and runtime applicability are not independently verified',
                            'No resource acquire/lease/quiesce or XNU ownership is established']}


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
                           ('RpmhDxe', 'rpmh-implicit-churn-final-evidence.json')):
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
    churn = implicit_churn(rpmh, rs, rr)
    ebs_raw = (evidence_root / 'rpmh-ebs-final-evidence.json').read_bytes()
    _, ebs_reads = checked_reads(rpmh, modules['RpmhDxe']['sha256'], json.loads(ebs_raw))
    ebs = ebs_clear(rpmh, rs, ebs_reads)
    results['RpmhDxeEbs'] = {'module_sha256': modules['RpmhDxe']['sha256'],
                             'evidence_sha256': hashlib.sha256(ebs_raw).hexdigest(),
                             'read_evidence_ids': list(ebs_reads.values())}
    rail_reads = {}
    for name, filename in (('DALSys', 'dal-clock-rail-config-evidence.json'),
                           ('ClockDxe', 'clock-rail-consumer-final-evidence.json')):
        raw = (evidence_root / filename).read_bytes()
        blob, _, _ = binaries[name]
        _, rail_reads[name] = checked_reads(blob, modules[name]['sha256'], json.loads(raw))
        results[name + 'RailConfig'] = {'module_sha256': modules[name]['sha256'],
                                        'evidence_sha256': hashlib.sha256(raw).hexdigest(),
                                        'read_evidence_ids': list(rail_reads[name].values())}
    rails = rail_config(dal, ds, rail_reads['DALSys'], clock, cs, rail_reads['ClockDxe'])
    return {'status': 'PASS_STATIC_BYTES_ONLY', 'boot_sha256': boot_receipt['boot_sha256'],
            'modules': boot_receipt['modules'], 'evidence': results, 'dal_chain': chain,
            'clock_plugin': {'address': '0x432a0', 'fields_u64': [hex(x) for x in plugin]},
            'rpmh_barrier': {'protocol_revision': '0x10001', 'slot': '0xb8', 'target': '0x18dc',
                             'return': '0x8000000000000003', 'instructions': barrier,
                             'authority': 'original bytes; name correlated with pinned EFIRpmh.h'},
            'rpmh_implicit_churn': churn,
            'rpmh_ebs_clear': ebs,
            'clock_rail_config': rails,
            'exact_instructions': decoded,
            'observations': ['DAL supplies the /icb/arbiter definition via /icb/arb -> icb_info -> structure 20',
                             'Clock 0xc520 installs the ICB plugin and forwards node definitions to NPA slot 0x20',
                             'Clock default node driver 0xc650 returns requested_state; not hardware ACK',
                             'ICB plugin processes eight-word/32-byte requests and forwards attributes to 0xdb80',
                             'Completion 0xe9dc invokes 0x128fc on a conditional branch',
                             'RPMh slot 0xb8 returns EFI_UNSUPPORTED and Clock wrapper does not check its result',
                             'This outer barrier is not a completion guarantee',
                             'Inner command-set return includes implicit churn of its handle pending records',
                             'One EBS callback uninstalls RPMh and clears configured TCS control/command enable',
                             'No target firmware execution, live completion observation or backend repair is performed'],
            'unknowns': ['Runtime registration and active configuration are not observed',
                         'Request ABI units and master/slave IDs require independent correlation',
                         'Complete hardware success/failure and all RPMh completion paths are not verified',
                         'ARC rail registrar, remaining EBS writers/order and XNU ownership remain unverified',
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
