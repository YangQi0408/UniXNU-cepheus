#!/usr/bin/env python3
"""Bind saved NPA evidence to the accepted boot's immutable firmware modules.

Offline file decoding only. No protocol invocation, device, SMC or MMIO access.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import uuid
import zlib

SPEC = importlib.util.spec_from_file_location('pair_boot', Path(__file__).with_name('verify-scm-pair-boot.py'))
PAIR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PAIR)
BOOT_SHA = '2c93dc3333665bdbc8f1c835c0e59226f5b2a0cfda2678d4bb121a3fc86903d2'
MODULES = (
    ('NpaDxe', 'CB29F4D1-7F37-4692-A416-93E82E219711',
     'Platforms/SurfaceDuo1Pkg/Device/xiaomi-cepheus/Binaries/QcomPkg/Drivers/NpaDxe/NpaDxe.efi',
     'a08453756c58db8d187eaa515c8bfb938226b3b8c3f4273e68d9cfc6afc49640'),
    ('RpmhDxe', 'CB29F4D1-7F37-4692-A416-93E82E219766',
     'Platforms/SurfaceDuo1Pkg/Device/xiaomi-cepheus/Binaries/QcomPkg/Drivers/RpmhDxe/RpmhDxe.efi',
     '03ccd266ce0c9e268ac199c0f27903c9dbdac675c9374c797e4178673564a7fa'),
    ('ClockDxe', '4DB5DEA6-5302-4D1A-8A82-677A683B0D29',
     'Platforms/SurfaceDuo1Pkg/Device/xiaomi-cepheus/Binaries/QcomPkg/Drivers/ClockDxe/ClockDxe.efi',
     'babe80a2b671823b1c0296dc0adb26e3642aa70d6ed58562d1c6cfea88e81dfe'),
    ('CPRDxe', '1B52138D-3FA3-4E50-B958-20887353F809',
     'Binaries/Cambria/BOOT.XF.3.0.1-00243-SC8180XWZB-2/QcomPkg/Drivers/CPRDxe/CPRDxe.efi',
     '63a777c11fc8006ca540886e2ea3ebdafa56bea752d716ea09f7514756821fbb'),
    ('EnvDxe', '90A49AFD-422F-08AE-9611-E788D3804845',
     'Platforms/SurfaceDuo1Pkg/Device/xiaomi-cepheus/Binaries/QcomPkg/Drivers/EnvDxe/EnvDxe.efi',
     'bdb328c49aece6f16132ef27f2ad89c12bd28b52565ec11a5eae3c63668862ee'),
    ('PmicDxe', '5776232E-082D-4B75-9A0E-FE1D13F7A5D9',
     'Platforms/SurfaceDuo1Pkg/Device/xiaomi-cepheus/Binaries/QcomPkg/Drivers/PmicDxe/PmicDxeLa.efi',
     '9282a7fe7431ac2f6ae125ebe13b1f00afeea39d6713672d41002d4e7ecc3010'),
    ('HashDxe', '3ADF8DDA-1850-44C5-8C63-BB991849BC6F',
     'Binaries/Epsilon/BOOT.XF.3.0-00527-SM8150LZB-1/QcomPkg/Drivers/HashDxe/HashDxe.efi',
     '0fd065458824be63d61a851d039b5d3198d556a6bee3ecfec423b9a24e3be266'),
    ('CipherDxe', '5F0937CD-73F9-445E-B320-BB6FD5D3CBDE',
     'Binaries/Epsilon/BOOT.XF.3.0-00527-SM8150LZB-1/QcomPkg/Drivers/CipherDxe/CipherDxe.efi',
     '94ed2f295e533705aac16aa57133b3831f31abd687b136da6e61190657584c67'),
    ('DALSys', '8E9BD160-B184-11DF-94E2-0800200C9A66',
     'Platforms/SurfaceDuo1Pkg/Device/xiaomi-cepheus/PatchedBinaries/DALSYSDxe.efi',
     '97231a87c321ed3d764f97b2f687b0f29c21787c56f013aadf0f01aa50effcbd'),
    ('LimitsDxe', 'F09AD92E-4E91-490F-AB99-E69939B840B2',
     'Binaries/Epsilon/BOOT.XF.3.0-00527-SM8150LZB-1/QcomPkg/Drivers/LimitsDxe/LimitsDxe.efi',
     '550617c51be7f95e4681944518c724a47ad8cd6242d9d861c7dfd1b5e819ede1'),
    ('CmdDbDxe', 'ABA01FF8-2CCB-4E12-8B2E-CD3F4A742993',
     'Platforms/SurfaceDuo1Pkg/Device/xiaomi-cepheus/Binaries/QcomPkg/Drivers/CmdDbDxe/CmdDbDxe.efi',
     'f1fe60c53ca0eb2613cec20aa6c69d8375543c76bbc226ba3a8c41bfc5535849'),
)


def pe_segments(blob):
    if len(blob) < 64 or blob[:2] != b'MZ':
        raise ValueError('invalid PE header')
    pe = struct.unpack_from('<I', blob, 60)[0]
    if pe + 24 > len(blob) or blob[pe:pe + 4] != b'PE\0\0':
        raise ValueError('invalid PE bounds')
    machine, count, _, _, _, optional, _ = struct.unpack_from('<HHIIIHH', blob, pe + 4)
    if machine != 0xaa64 or optional < 32 or pe + 24 + optional + 40 * count > len(blob):
        raise ValueError('invalid ARM64 section bounds')
    if struct.unpack_from('<H', blob, pe + 24)[0] != 0x20b or struct.unpack_from('<Q', blob, pe + 48)[0] != 0:
        raise ValueError('expected PE32+ zero image base')
    result = []
    for i in range(count):
        _, memsize, address, size, offset = struct.unpack_from('<8sIIII', blob, pe + 24 + optional + i * 40)
        if offset + size > len(blob) or address + memsize > 1 << 32:
            raise ValueError('invalid section data bounds')
        result.append((offset, address, size))
    return result


def span(blob, segments, address, size):
    if address < 0 or size <= 0 or address + size > 1 << 32:
        raise ValueError('invalid requested span')
    offsets = [off + address - va for off, va, length in segments
               if va <= address and address + size <= va + length]
    if len(offsets) != 1:
        raise ValueError('span not uniquely file-backed')
    return blob[offsets[0]:offsets[0] + size]


def decode(raw, address):
    import capstone
    decoder = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
    items = list(decoder.disasm(raw, address))
    if len(items) * 4 != len(raw):
        raise ValueError('incomplete instruction decoding')
    return [{'address': hex(i.address), 'mnemonic': i.mnemonic, 'operands': i.op_str,
             'bytes_hex': i.bytes.hex()} for i in items]


def npa_inventory(inner):
    guid = uuid.UUID('79d6c870-725e-489e-a0a1-27e7a5d0cb35').bytes_le
    result, examined, skipped = [], 0, 0
    for ident, file in inner.items():
        if file[18] != 7:
            continue
        items = PAIR.fv.sections(file[24:])
        binaries = [data for kind, data in items if kind == 0x10]
        if len(binaries) != 1:
            skipped += 1
            continue
        examined += 1
        binary = binaries[0]
        title = [data.decode('utf-16le').rstrip('\0') for kind, data in items if kind == 0x15]
        matches = []
        for needle in (guid, b'/arc/client/', b'cx.lvl', b'mx.lvl', b'/icb/arbiter'):
            cursor = 0
            offsets = []
            while (cursor := binary.find(needle, cursor)) != -1:
                offsets.append(hex(cursor))
                cursor += len(needle)
            if offsets:
                matches.append({'needle': 'NPA protocol GUID' if needle == guid else needle.decode(),
                                'file_offsets': offsets})
        if matches:
            result.append({'ffs_guid': ident, 'ui_names': title,
                           'pe_sha256': hashlib.sha256(binary).hexdigest(), 'matches': matches})
    return {'examined_plain_pe32_dxe_files': examined, 'skipped_nonplain_dxe_files': skipped,
            'matches': result}


def verify(root):
    work = root / 'gpu-work'
    boot = (work / 'out/scm-pair-final-6ce4c7c/boot-scm-pair.img').read_bytes()
    if hashlib.sha256(boot).hexdigest() != BOOT_SHA:
        raise ValueError('accepted boot identity mismatch')
    size, _, _, _, _, _, _, page = struct.unpack_from('<8I', boot, 8)
    if boot[:8] != b'ANDROID!' or page != 4096:
        raise ValueError('unexpected Android boot header')
    raw = zlib.decompressobj(31).decompress(boot[page:page + size], 0x300081)
    if len(raw) != 0x300080:
        raise ValueError('unexpected firmware payload size')
    _, inner = PAIR.inner_files(raw[128:])
    identities, blobs = [], {}
    for name, guid, relative, expected in MODULES:
        original = (root / 'uefi' / relative).read_bytes()
        if hashlib.sha256(original).hexdigest() != expected:
            raise ValueError('module identity mismatch: ' + name)
        observed = [body for kind, body in PAIR.fv.sections(inner[guid][24:]) if kind == 0x10]
        if observed != [original]:
            raise ValueError('embedded module mismatch: ' + name)
        identities.append({'name': name, 'guid': guid, 'source': relative, 'sha256': expected,
                           'bytes': len(original), 'embedded_bytes_equal': True})
        blobs[name] = original
    evidence_path = root / 'logs/pas13-instruction-replay-20261110/npa-query-evidence.json'
    evidence_raw = evidence_path.read_bytes()
    evidence = json.loads(evidence_raw)
    npa = blobs['NpaDxe']
    segments = pe_segments(npa)
    decoded, records = {}, []
    for record in evidence['records']:
        if record['subject']['digest']['sha256'] != MODULES[0][3]:
            raise ValueError('evidence target mismatch')
        if record['operation'] != 'read_bytes':
            continue
        observed = record['normalized_result']
        address = int(observed['address'], 16)
        size = observed['requested_bytes']
        raw = bytes.fromhex(observed['bytes_hex'])
        if not observed['complete'] or observed['returned_bytes'] != size or raw != span(npa, segments, address, size):
            raise ValueError('evidence bytes differ from original PE')
        records.append(record['evidence_id'])
        if address != 0x11058:
            decoded[hex(address)] = decode(raw, address)
    required = {'0x1670', '0x1a74', '0x89c4'}
    if set(decoded) != required or len(records) != 4:
        raise ValueError('missing exact wrapper/query evidence')
    def instruction(address, mnemonic, operands):
        found = [i for group in decoded.values() for i in group if int(i['address'], 16) == address]
        if len(found) != 1 or (found[0]['mnemonic'], found[0]['operands']) != (mnemonic, operands):
            raise ValueError('unexpected exact instruction: ' + hex(address))
    instruction(0x1680, 'bl', '#0x8b14')
    instruction(0x16ac, 'bl', '#0x8bb4')
    instruction(0x8a58, 'ldr', 'w8, [x20, #0x34]')
    instruction(0x1a98, 'ldr', 'x4, [x10, #0x150]')
    instruction(0x1a9c, 'br', 'x4')
    return {'status': 'PASS_STATIC_BYTES_ONLY', 'boot_sha256': BOOT_SHA, 'modules': identities,
            'exact_embedded_npa_inventory': npa_inventory(inner),
            'evidence_sha256': hashlib.sha256(evidence_raw).hexdigest(),
            'read_evidence_ids': records, 'decoded_original_ranges': decoded,
            'observations': ['Query/QueryByName forward to 0x8b14/0x8bb4 and store status separately from EFI return',
                             'Default query case 0 loads resource+0x34 after resource-specific query fallback',
                             'NpaDeinit tail-calls BootServices+0x150 (UninstallMultipleProtocolInterfaces)'],
            'limits': ['Embedded identity does not prove module dispatch, resource registration or live state',
                       'Current-state ABI/name mapping inferred from pinned SDM670 reference, not exact source provenance',
                       'Default query is software state, not hardware completion or this DRV old vote proof',
                       'Indirect custom query and actual CX/MX driver semantics remain unknown',
                       'Literal inventory only examines plain PE32 sections in DXE FFS files, not indirect or compressed payloads',
                       'No resource ownership, final EBS barrier or runtime protocol usability established'],
            'phone_operations': False, 'firmware_execution': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('preserve previous output')
    report = verify(args.root)
    report['script_sha256'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    with args.output.open('x') as output:
        json.dump(report, output, indent=2)
        output.write('\n')
    print(f'PASS: {len(MODULES)} accepted-boot modules, four original PE evidence ranges, exact NPA query/deinit instructions')


if __name__ == '__main__':
    main()
