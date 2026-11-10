#!/usr/bin/env python3
"""Replay only the fixed acquired TZ PAS13 lookup instructions in host RAM.

Requires an explicitly supplied private TZ original. No SMC, device, firmware
startup, MMIO or authentication is allowed. Mutations are hypothetical RAM
inputs, never observations of the phone's secure memory.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct

SPEC = importlib.util.spec_from_file_location('tz_verify', Path(__file__).with_name('verify-tz-evidence.py'))
VERIFY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VERIFY)
COUNT = 0x1c11c1f0
DESCRIPTORS = 0x1c13edf8
CACHE = 0x1c165618
LOCKS = 0x1c132100
STACK = 0x30000000
RESULT = 0x30010000
STOP = 0x30011000
PAS13 = DESCRIPTORS + 5 * 0x238
CODE = (
    (0x1c04f444, 0x44, 'PAS7 boolean wrapper'),
    (0x1c04dae8, 0x4c, 'lookup verdict'),
    (0x1c04db34, 0x84, 'lazy lookup'),
    (0x1c04d354, 0x64, 'descriptor search'),
    (0x1c04dbb8, 8, 'descriptor ID'),
    (0x1c04d6ec, 0x20, 'descriptor prefix clear'),
    (0x1c04eea0, 0x30, 'default ops'),
    (0x1c040030, 0x2c, 'memset wrapper'),
    (0x1c040470, 0xd0, 'memset'),
    (0x14682be4, 0x50, 'exclusive lock'),
    (0x14682c34, 0x28, 'unlock'),
    (0x14683ea8, 0x28, 'MPIDR CPU index'),
)
DATA = ((COUNT, 4), (DESCRIPTORS, 15 * 0x238), (CACHE, 30 * 8), (LOCKS, 30 * 0x80))
INSTRUCTION_LIMIT = 100000
TIMEOUT_US = 1000000
ACCESS_LOG_LIMIT = 2048


def contains(ranges, address, size):
    return size > 0 and address >= 0 and address + size <= 1 << 64 and any(
        start <= address and address + size <= start + length for start, length in ranges)


def pages(ranges):
    return sorted({page for start, size in ranges for page in
                   range(start & ~4095, (start + size + 4095) & ~4095, 4096)})


def prepared(blob):
    if len(blob) != 4194304 or hashlib.sha256(blob).hexdigest() != VERIFY.TZ_SHA256:
        raise ValueError('fixed private TZ identity mismatch')
    segments = VERIFY.load_segments(blob)
    loaded = []
    for address, size, _ in CODE:
        offset, flags = VERIFY.map_range(segments, address, size)
        if not flags & 1:
            raise ValueError('code is not executable file-backed data')
        loaded.append((address, blob[offset:offset + size]))
    for address, size in DATA:
        offset, _ = VERIFY.map_range(segments, address, size)
        loaded.append((address, blob[offset:offset + size]))
    if struct.unpack('<I', loaded[len(CODE)][1])[0] != 15:
        raise ValueError('unexpected static descriptor count')
    return loaded


def validate_loaded(loaded):
    import capstone
    expected = [(a, n) for a, n, _ in CODE] + list(DATA)
    if [(a, len(b)) for a, b in loaded] != expected:
        raise ValueError('loaded bytes must exactly match declared ranges')
    decoder = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
    for address, raw in loaded[:len(CODE)]:
        decoded = list(decoder.disasm(raw, address))
        if len(decoded) * 4 != len(raw) or any(i.size != 4 for i in decoded):
            raise ValueError('incomplete AArch64 decode')
        if any(i.mnemonic in ('smc', 'hvc', 'svc', 'brk', 'hlt', 'eret', 'drps') for i in decoded):
            raise ValueError('exception/secure transition instruction prohibited')


def correlate(blob, loaded, evidence_paths):
    functions, sources = {}, []
    for path in evidence_paths:
        raw = path.read_bytes()
        evidence = json.loads(raw)
        verified = VERIFY.verify(blob, evidence)
        sources.append({'path': str(path), 'sha256': hashlib.sha256(raw).hexdigest(),
                        'verified_instructions': verified['instruction_count']})
        for function in evidence['functions']:
            entry = int(function['entry'], 16)
            if entry in functions and functions[entry]['instructions'] != function['instructions']:
                raise ValueError('conflicting native evidence')
            functions[entry] = function
    selected = []
    for address, size, _ in CODE:
        function = functions.get(address)
        if function is None or int(function['analyst_body_end_inclusive'], 16) + 1 != address + size:
            raise ValueError('missing exact native code-range evidence')
        if b''.join(bytes.fromhex(i['bytes']) for i in function['instructions']) != dict(loaded)[address]:
            raise ValueError('native code byte mismatch')
        selected.append(function)
    llvm = VERIFY.llvm_check(blob, {'functions': selected})
    return {'sources': sources, 'exact_ranges': len(selected), 'llvm': llvm}


def replay(loaded, name, mpidr, mutations=(), repeat=False):
    from unicorn import (Uc, UcError, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE,
                         UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_HOOK_MEM_INVALID,
                         UC_MEM_WRITE, UC_QUERY_TIMEOUT, UC_PROT_READ,
                         UC_PROT_WRITE, UC_PROT_EXEC)
    from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X30, UC_ARM64_REG_SP, UC_ARM64_REG_PC
    validate_loaded(loaded)
    if not 0 <= mpidr < 1 << 64:
        raise ValueError('invalid synthetic MPIDR')
    uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    regions = [(a, len(b)) for a, b in loaded] + [(STACK, 65536), (RESULT, 4096)]
    for page in pages(regions):
        uc.mem_map(page, 4096)
    for address, data in loaded:
        uc.mem_write(address, data)
    for address, data in mutations:
        if not contains(DATA, address, len(data)):
            raise ValueError('mutation outside declared private RAM data')
        uc.mem_write(address, data)
    code_pages = pages([(a, n) for a, n, _ in CODE])
    for page in pages(regions):
        uc.mem_protect(page, 4096, UC_PROT_READ | (UC_PROT_EXEC if page in code_pages else UC_PROT_WRITE))
    uc.cpr_write(3, 0, 0, 0, 5, mpidr)
    accesses, executed = [], []
    access_counts = {'read': 0, 'write': 0, 'omitted': 0}
    allowed_code = [(a, n) for a, n, _ in CODE]
    allowed_data = list(DATA) + [(STACK, 65536), (RESULT, 24)]

    def instruction(engine, address, size, _):
        if size != 4 or not contains(allowed_code, address, size):
            raise ValueError('execution outside bounded lookup: ' + hex(address))
        executed.append(address)

    def memory(engine, access, address, size, value, _):
        if not contains(allowed_data, address, size):
            raise ValueError('memory access outside declared RAM: ' + hex(address))
        if not STACK <= address < STACK + 65536:
            kind = 'write' if access == UC_MEM_WRITE else 'read'
            access_counts[kind] += 1
            if len(accesses) < ACCESS_LOG_LIMIT:
                accesses.append({'kind': kind, 'address': hex(address), 'bytes': size,
                                 'pc': hex(engine.reg_read(UC_ARM64_REG_PC))})
            else:
                access_counts['omitted'] += 1

    def invalid_memory(engine, access, address, size, value, _):
        raise ValueError('unmapped/protected memory access: ' + hex(address))

    uc.hook_add(UC_HOOK_CODE, instruction)
    uc.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, memory)
    uc.hook_add(UC_HOOK_MEM_INVALID, invalid_memory)
    calls = []
    before = bytes(uc.mem_read(DESCRIPTORS, 15 * 0x238))
    for _ in range(2 if repeat else 1):
        uc.reg_write(UC_ARM64_REG_SP, STACK + 65536)
        uc.reg_write(UC_ARM64_REG_X30, STOP)
        uc.reg_write(UC_ARM64_REG_X0, 13)
        uc.reg_write(UC_ARM64_REG_X1, RESULT)
        uc.mem_write(RESULT, bytes.fromhex('a5' * 24))
        first = len(executed)
        try:
            uc.emu_start(CODE[0][0], STOP, timeout=TIMEOUT_US, count=INSTRUCTION_LIMIT)
        except UcError as error:
            raise ValueError('emulator rejected unmapped/protected/invalid execution: ' + str(error)) from error
        returned = uc.reg_read(UC_ARM64_REG_PC) == STOP
        instructions = len(executed) - first
        termination = ('returned' if returned else 'instruction-limit' if instructions == INSTRUCTION_LIMIT
                       else 'emulator-timeout' if uc.query(UC_QUERY_TIMEOUT) else 'stopped-without-return')
        calls.append({'returned': returned, 'wrapper_return': uc.reg_read(UC_ARM64_REG_X0) if returned else None,
                      'results': list(struct.unpack('<3Q', bytes(uc.mem_read(RESULT, 24)))) if returned else None,
                      'instructions': instructions, 'termination': termination})
        if not returned:
            break
    after = bytes(uc.mem_read(DESCRIPTORS, 15 * 0x238))
    return {'case': name, 'input_mpidr': hex(mpidr), 'calls': calls,
            'cache13': hex(struct.unpack('<Q', bytes(uc.mem_read(CACHE + 13 * 8, 8)))[0]),
            'descriptor_id13': struct.unpack('<I', after[5 * 0x238 + 0x1f8:5 * 0x238 + 0x1fc])[0],
            'descriptor_changed_byte_offsets': [hex(i) for i, (a, b) in enumerate(zip(before, after)) if a != b],
            'code_entries_seen': [hex(a) for a, _, _ in CODE if a in executed],
            'memory_accesses': accesses, 'memory_access_counts': access_counts,
            'memory_access_log_limit': ACCESS_LOG_LIMIT,
            'hypothetical_mutations': [{'address': hex(a), 'bytes_hex': b.hex()} for a, b in mutations]}


def check_case(case, verdict):
    if not case['calls']:
        raise ValueError('no calls to classify')
    if verdict is None:
        if len(case['calls']) != 1 or case['calls'][0]['returned']:
            raise ValueError('expected unknown/nonreturn: ' + case['case'])
    elif not all(c['returned'] and c['wrapper_return'] == 0 and c['results'] == [verdict, 0, 0]
                 for c in case['calls']):
        raise ValueError('unexpected lookup verdict: ' + case['case'])
    if case['case'].startswith('static-') and (case['descriptor_id13'] != 13 or case['cache13'] != hex(PAS13)):
        raise ValueError('static cache/ID mismatch')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tz', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--evidence', required=True, action='append', type=Path,
                        help='native Ghidra bundle; repeat for dispatch and CPU-index coverage')
    args = parser.parse_args()
    if args.output.exists():
        parser.error('preserve previous output')
    blob = args.tz.read_bytes()
    loaded = prepared(blob)
    validate_loaded(loaded)
    native = correlate(blob, loaded, args.evidence)
    cases = []
    for name, cpu, mutations, again, verdict in (
        ('static-uefi-cpu-empty-cache', 0x81000000, (), True, 1),
        ('static-xnu-cpu-empty-cache', 0x81000200, (), True, 1),
        ('zero-descriptor-count', 0x81000000, ((COUNT, struct.pack('<I', 0)),), False, 0),
        ('count-stops-before-pas13', 0x81000000, ((COUNT, struct.pack('<I', 5)),), False, 0),
        ('pas13-id-replaced', 0x81000000, ((PAS13 + 0x1f8, struct.pack('<I', 0)),), False, 0),
        ('nonzero-cache-with-zero-count', 0x81000000,
         ((COUNT, struct.pack('<I', 0)), (CACHE + 13 * 8, struct.pack('<Q', PAS13))), False, 1),
        ('nonzero-cache-with-changed-id', 0x81000000,
         ((PAS13 + 0x1f8, struct.pack('<I', 0)), (CACHE + 13 * 8, struct.pack('<Q', PAS13))), False, 1),
        ('held-lock-does-not-return-false', 0x81000000,
         ((LOCKS + 13 * 0x80, struct.pack('<Q', 1)),), False, None),
    ):
        case = replay(loaded, name, cpu, mutations, again)
        check_case(case, verdict)
        cases.append(case)
    import unicorn
    report = {'status': 'PASS', 'scope': 'Bounded host instruction replay with hypothetical RAM state only',
              'tz_sha256': VERIFY.TZ_SHA256, 'unicorn_version': unicorn.__version__,
              'script_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              'native_code_correlation': native,
              'instruction_limit': INSTRUCTION_LIMIT, 'timeout_us': TIMEOUT_US,
              'code_ranges': [{'address': hex(a), 'bytes': n, 'purpose': p,
                               'sha256': hashlib.sha256(dict(loaded)[a]).hexdigest()} for a, n, p in CODE],
              'cases': cases, 'phone_operations': False, 'smc_executed': False,
              'limits': ['No firmware startup, MMU/EL2/EL3 dispatch, transport or authentication replay',
                         'CPU MPIDR and lock state are synthetic emulator inputs',
                         'Input private partition identity is not running secure-image identity',
                         'Mutation cases do not establish what changed on the phone',
                         'Instruction budget exhaustion is unknown/nonreturn, never a false verdict',
                         'Neither replay nor static descriptors authorize bypassing supported=false']}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('x') as output:
        json.dump(report, output, indent=2)
        output.write('\n')
    print('PASS: eight bounded actual-instruction RAM scenarios; no SMC/device execution')


if __name__ == '__main__':
    main()
