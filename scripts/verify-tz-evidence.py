#!/usr/bin/env python3
"""Verify private, offline Ghidra evidence against the fixed recovery TZ original.

No device commands or firmware execution. Capstone and LLVM only decode bytes.
Local proprietary TZ/evidence are required; never fetch or publish them in CI.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import tempfile

TZ_SHA256 = '989389bcf4527b2b8e445998aba49454e556ac554c18d80e9eb999a8cbbdb114'


def load_segments(blob):
    if len(blob) < 64 or blob[:6] != b'\x7fELF\x02\x01':
        raise ValueError('expected little-endian ELF64')
    if struct.unpack_from('<H', blob, 18)[0] != 183:
        raise ValueError('expected AArch64')
    offset = struct.unpack_from('<Q', blob, 32)[0]
    entsize, count = struct.unpack_from('<HH', blob, 54)
    if entsize != 56 or count > 128 or offset + entsize * count > len(blob):
        raise ValueError('invalid program header bounds')
    segments = []
    for i in range(count):
        kind, flags, off, va, _, size, memsize, _ = struct.unpack_from('<IIQQQQQQ', blob, offset + 56 * i)
        if kind != 1:
            continue
        if size > memsize or off + size > len(blob) or va + memsize > 1 << 64:
            raise ValueError('invalid LOAD bounds')
        segments.append((off, va, size, flags))
    return segments


def map_range(segments, address, size):
    if size <= 0:
        raise ValueError('empty or negative range')
    hits = [(off + address - va, flags) for off, va, length, flags in segments
            if va <= address and address + size <= va + length]
    if len(hits) != 1:
        raise ValueError('range not uniquely file-backed')
    return hits[0]


def verify(blob, evidence):
    import capstone
    digest = hashlib.sha256(blob).hexdigest()
    if len(blob) != 4 * 1024 * 1024 or digest != TZ_SHA256 or evidence['executable_sha256'] != digest:
        raise ValueError('fixed TZ identity mismatch')
    segments = load_segments(blob)
    decoder = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
    count = 0
    summaries = []
    for function in evidence['functions']:
        start = int(function['analyst_body_start'], 16)
        end = int(function['analyst_body_end_inclusive'], 16)
        if start != int(function['entry'], 16) or not 0 < end + 1 - start <= 8192:
            raise ValueError('invalid analyst function bounds')
        cursor = start
        direct = []
        for instruction in function['instructions']:
            address = int(instruction['address'], 16)
            raw = bytes.fromhex(instruction['bytes'])
            if address != cursor or len(raw) != 4:
                raise ValueError('missing/overlapping/non-AArch64 instruction')
            offset, flags = map_range(segments, address, 4)
            if not flags & 1 or blob[offset:offset + 4] != raw:
                raise ValueError('instruction bytes/mapping mismatch')
            decoded = list(decoder.disasm(raw, address))
            if len(decoded) != 1:
                raise ValueError('Capstone cannot decode instruction')
            ins = decoded[0]
            # Independently confirm immediate call/branch destinations against Ghidra.
            if ins.mnemonic in ('bl', 'b') and ins.op_str.startswith('#0x'):
                target = int(ins.op_str[1:], 16)
                refs = [int(r['to'], 16) for r in instruction['references_from']
                        if 'CALL' in r['type'] or 'JUMP' in r['type']]
                if target not in refs:
                    raise ValueError('direct branch reference mismatch')
                direct.append({'site': hex(address), 'target': hex(target)})
            cursor += 4
            count += 1
        if cursor != end + 1 or not function['decompile_completed']:
            raise ValueError('incomplete seed coverage/decompilation')
        summaries.append({'entry': hex(start), 'instructions': (end + 1 - start) // 4,
                          'direct_branches': direct})
    for data in evidence['static_data']:
        raw = bytes.fromhex(data['bytes'])
        off, _ = map_range(segments, int(data['address'], 16), len(raw))
        if blob[off:off + len(raw)] != raw:
            raise ValueError('static data bytes mismatch')
    def read(address, size):
        off, _ = map_range(segments, address, size)
        return blob[off:off + size]
    table_count = struct.unpack('<I', read(0x1c11c1f0, 4))[0]
    if table_count != 15:
        raise ValueError('unexpected descriptor count')
    descriptors = [{'index': i, 'address': hex(0x1c13edf8 + i * 0x238),
                    'id': struct.unpack('<I', read(0x1c13edf8 + i * 0x238 + 0x1f8, 4))[0]}
                   for i in range(table_count)]
    pas13 = [d for d in descriptors if d['id'] == 13]
    if len(pas13) != 1 or pas13[0]['address'] != '0x1c13f910':
        raise ValueError('PAS13 descriptor mismatch')
    entry = struct.unpack('<IIIIQ', read(0x1c130458, 24))
    if entry != (0, 0x02000207, 1, 1, 0x1c04f444):
        raise ValueError('supported7 dispatch entry mismatch')
    arg1_target = struct.unpack('<Q', read(0x1c11d188 + 8, 8))[0]
    if arg1_target != 0x1c057290:
        raise ValueError('one-argument dispatch arm mismatch')
    return {'result': 'PASS_STATIC_BYTES_ONLY', 'tz_sha256': digest,
            'functions': summaries, 'instruction_count': count,
            'static_data_ranges': len(evidence['static_data']), 'descriptors': descriptors,
            'pas7_entry': list(entry), 'arg1_arm': hex(arg1_target),
            'limits': ['No runtime firmware identity or live descriptor state established',
                       'No secure calls, authentication or GPU acceleration tested',
                       'Decompiler warning/unknown types and indirect CFG remain in source evidence']}


def llvm_check(blob, evidence):
    """Assemble .inst copies into a disposable ELF, decode only; never execute."""
    lines = ['.text', '.globl tz_seed_bytes', 'tz_seed_bytes:']
    words = []
    for function in evidence['functions']:
        for ins in function['instructions']:
            raw = bytes.fromhex(ins['bytes'])
            word = int.from_bytes(raw, 'little')
            words.append(word)
            lines.append(f'.inst 0x{word:08x}')
    with tempfile.TemporaryDirectory(prefix='tz-llvm-static-') as tmp:
        output = Path(tmp) / 'seed.o'
        subprocess.run(['clang', '--target=aarch64-none-elf', '-x', 'assembler', '-c', '-o', str(output), '-'],
                       input=('\n'.join(lines) + '\n').encode(), check=True, capture_output=True, timeout=30)
        listing = subprocess.run(['llvm-objdump', '-d', str(output)], check=True,
                                 capture_output=True, timeout=30).stdout.decode()
    # LLVM releases print either one word or four little-endian bytes.
    decoded = []
    for line in listing.splitlines():
        match = re.match(r'^\s*[0-9a-f]+:\s+([0-9a-f]{8})\s+\S', line)
        if match:
            decoded.append(int(match[1], 16))
            continue
        match = re.match(r'^\s*[0-9a-f]+:\s+((?:[0-9a-f]{2} ){3}[0-9a-f]{2})\s+\S', line)
        if match:
            decoded.append(int.from_bytes(bytes.fromhex(match[1]), 'little'))
    if decoded != words or '<unknown>' in listing:
        raise ValueError('LLVM instruction-copy decode mismatch/unknown')
    return {'instruction_count': len(words), 'listing_sha256': hashlib.sha256(listing.encode()).hexdigest(),
            'scope': 'disposable .inst copy; branch addresses are rebased, no runtime execution'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tz', required=True, type=Path)
    parser.add_argument('--evidence', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('preserve previous output')
    blob = args.tz.read_bytes()
    evidence = json.loads(args.evidence.read_text())
    result = verify(blob, evidence)
    result['evidence_sha256'] = hashlib.sha256(args.evidence.read_bytes()).hexdigest()
    result['verifier_sha256'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    result['llvm'] = llvm_check(blob, evidence)
    with args.output.open('x') as out:
        json.dump(result, out, indent=2)
        out.write('\n')
    print(f"TZ static evidence PASS: {result['instruction_count']} instructions, "
          f"{result['static_data_ranges']} data ranges; Capstone + LLVM decode only")


if __name__ == '__main__':
    main()
