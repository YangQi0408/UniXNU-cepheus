#!/usr/bin/env python3
"""Check actual linked RPMh scope and participating-writer branches, not runtime."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def require(condition, detail='linked scope check failed'):
    if not condition:
        raise AssertionError(detail)


def parse(text):
    blocks, current = {}, None
    for line in text.splitlines():
        label = re.fullmatch(r'([0-9a-f]+) <([^>]+)>:', line)
        if label:
            current = label[2]
            blocks[current] = []
        instruction = re.match(r'([0-9a-f]+):\s+(\S+)(?:\s+(.*))?$', line)
        if instruction and current:
            operands = (instruction[3] or '').split(';', 1)[0].strip()
            blocks[current].append((int(instruction[1], 16), instruction[2], operands))
    return blocks


def target(row):
    match = re.search(r'0x([0-9a-f]+) <', row[2])
    return int(match[1], 16) if match else None


def call(row, name):
    return row[1] in ('bl', 'b') and row[2].endswith('<'+name+'>')


def reachable(rows, start, avoided=()):
    addresses = {row[0]: index for index, row in enumerate(rows)}
    pending, seen = [start], set()
    while pending:
        index = pending.pop()
        if index in seen or index in avoided:
            continue
        require(0 <= index < len(rows))
        seen.add(index)
        row = rows[index]
        if row[1] == 'ret':
            continue
        if row[1] == 'b' or row[1].startswith('b.') or row[1] in ('cbz', 'cbnz', 'tbz', 'tbnz'):
            address = target(row)
            if address not in addresses:
                require(row[1] == 'b', row)
                continue
            pending.append(addresses[address])
            if row[1] == 'b':
                continue
        require(index+1 < len(rows), row)
        pending.append(index+1)
    return seen


def memory(rows):
    """Resolve straight-line PC-relative global references in current codegen."""
    constants, accesses = {}, {}
    for index, row in enumerate(rows):
        operands = row[2].split(', ', 2)
        if row[1] == 'adrp':
            match = re.match(r'(x\d+), 0x([0-9a-f]+)', row[2])
            require(match, row)
            constants[match[1]] = int(match[2], 16)
            continue
        if row[1] == 'add' and len(operands) == 3 and operands[1] in constants and operands[2].startswith('#'):
            constants[operands[0]] = constants[operands[1]]+int(operands[2][1:], 0)
            continue
        if row[1] == 'mov' and len(operands) >= 2 and operands[1].startswith('#'):
            constants[operands[0]] = int(operands[1][1:], 0)
            continue
        match = re.search(r'\[(x\d+)(?:, #(-?(?:0x)?[0-9a-f]+))?\]', row[2])
        if match and match[1] in constants:
            accesses[index] = constants[match[1]]+int(match[2] or '0', 0)
        # Calls preserve callee-saved registers, not x0-x18.
        if row[1] in ('bl', 'blr'):
            constants = {key: value for key, value in constants.items()
                         if key.startswith('x') and int(key[1:]) >= 19}
        elif row[1].startswith('ld') and operands:
            constants.pop(operands[0], None)
            if operands[0].startswith('w'):
                constants.pop('x'+operands[0][1:], None)
    return accesses


def legacy(rows, symbols):
    refs = memory(rows)
    phase = next(index for index, row in enumerate(rows)
                 if row[1] in ('ldar', 'ldapr') and refs.get(index) == symbols['_ceph_rpmh_scope_phase'])
    lock = next(index for index, row in enumerate(rows)
                if row[1] == 'swpa' and refs.get(index) == symbols['_rh_lock'])
    require(lock < phase)
    require(phase not in reachable(rows, 0, (lock,)))
    lock_register = re.search(r'\[(x\d+)\]', rows[lock][2])[1]
    require(any(row[1] == 'stlr' and row[2] == 'wzr, ['+lock_register+']' for row in rows))
    maps = [index for index, row in enumerate(rows) if call(row, '_rh_map')]
    require(len(maps) == 1 and phase < maps[0])
    require(maps[0] not in reachable(rows, 0, (phase,)))
    # The SWEET compiler currently implements OPEN || RELEASED exactly this way.
    register = rows[phase][2].split(',', 1)[0]
    require(rows[phase+1][1:] == ('cmp', register+', #0x3'))
    addresses = {row[0]: index for index, row in enumerate(rows)}
    if rows[phase+2][1] == 'b.eq':
        require(rows[phase+3][1] == 'cbnz' and rows[phase+3][2].startswith(register+', '))
        released = addresses[target(rows[phase+2])]
        denied = addresses[target(rows[phase+3])]
    else:
        # CCMP selects Z=1 for RELEASED; otherwise Z tests OPEN (zero).
        require(rows[phase+2][1:] == ('ccmp', register+', #0x0, #0x4, ne'))
        require(rows[phase+3][1] == 'b.ne')
        released = phase+4
        denied = addresses[target(rows[phase+3])]
    forbidden = {'_rh_map', '_rh_lookup', '_rh_send', '_ml_io_map'}
    denial = reachable(rows, denied)
    require(not any(any(call(rows[index], name) for name in forbidden) for index in denial))
    require(any(rows[index][1:] == ('mov', 'w19, #0x10') or rows[index][1:] == ('mov', 'w21, #0x10')
                for index in denial))
    require(maps[0] in reachable(rows, released))
    return {'lock': hex(rows[lock][0]), 'phase_check': hex(rows[phase][0]),
            'denial': hex(rows[denied][0]), 'map': hex(rows[maps[0]][0]),
            'open_released_only': True, 'denial_has_no_hardware_calls': True}


def active_write(rows, symbols):
    refs = memory(rows)
    stores = [index for index, row in enumerate(rows)
              if row[1] == 'str' and re.search(r'\[x\d+, (?:x|w)\d+(?:, uxtw)?\]', row[2])]
    require(len(stores) == 1)
    store = stores[0]
    idle_check = next(index for index, row in enumerate(rows) if call(row, '_cepheus_gpu_idle_check'))
    keep = next(index for index, row in enumerate(rows) if call(row, '_cepheus_gpu_idle_finish'))
    retained = next(index for index, row in enumerate(rows)
                    if row[1] == 'stlr' and refs.get(index) == symbols['_ceph_rpmh_scope_phase']
                    and index > keep)
    require(idle_check < keep < retained < store)
    require(rows[keep-1][1:] == ('mov', 'w1, #0x1'))
    phase_register = rows[retained][2].split(',', 1)[0]
    require(rows[retained-1][1:] == ('mov', phase_register+', #0x2'))
    for required in (idle_check, keep, retained):
        require(store not in reachable(rows, 0, (required,)), rows[required])
    addresses = {row[0]: index for index, row in enumerate(rows)}
    branch = rows[keep+1]
    require(branch[1] == 'cbz' and branch[2].startswith('w0, '))
    require(store not in reachable(rows, keep+2))
    require(store in reachable(rows, addresses[target(branch)]))
    barrier = store-1
    if rows[barrier][1] == 'mov':
        # Current full XNU widens the unsigned MMIO offset after the barrier.
        register = rows[barrier][2].split(', ')
        require(len(register) == 2 and register[0] == register[1] and register[0].startswith('w'))
        require(re.search(r'\[x\d+, x'+register[0][1:]+r'\]', rows[store][2]))
        barrier -= 1
    require(rows[barrier][1:] == ('dmb', 'oshst') and rows[store+1][1:] == ('dsb', 'osh'))
    for required in (keep-1, retained-1, barrier):
        require(store not in reachable(rows, 0, (required,)))
    return {'idle_check': hex(rows[idle_check][0]), 'idle_keep': hex(rows[keep][0]),
            'retained_publication': hex(rows[retained][0]), 'mmio_store': hex(rows[store][0]),
            'all_store_paths_pass_keep': True, 'keep_failure_has_no_store': True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kernel', type=Path, required=True)
    parser.add_argument('--kernel-receipt', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    require(not args.output.exists())
    data = args.kernel.read_bytes()
    require(data[:8] == bytes.fromhex('cffaedfe0c000001'))
    sha = hashlib.sha256(data).hexdigest()
    receipt = json.loads(args.kernel_receipt.read_text())
    require(receipt['status'] == 'PASS' and receipt['kernel_sha256'] == sha)
    names = ['_cepheus_gpu_rpmh_'+name for name in ('hold', 'check', 'finish')]
    names += ['_sweet_rpmh_vreg_enable', '_cepheus_touch_power_enable', '_ceph_rpmh_active_write']
    output = subprocess.check_output(['llvm-nm', '-n', str(args.kernel)], text=True)
    symbols = {line.split()[2]: int(line.split()[0], 16) for line in output.splitlines()
               if len(line.split()) == 3 and line.split()[1].upper() != 'U'}
    required = names+['_rh_lock', '_ceph_rpmh_scope_phase', '_ceph_rpmh_scope_failed']
    require(all(name in symbols for name in required), 'missing new scope symbols')
    assembly = subprocess.check_output(['llvm-objdump', '-d', '--no-show-raw-insn',
                                       '--disassemble-symbols='+','.join(names), str(args.kernel)], text=True)
    blocks = parse(assembly)
    require(all(name in blocks for name in names))
    for name in names[:3]:
        rows = blocks[name]
        require(not any(row[1] in ('smc', 'wfi', 'wfe', 'blr') or any(call(row, forbidden) for forbidden in
                        ('_ml_io_map', '_rh_map', '_rh_read', '_rh_write', '_rh_send')) for row in rows))
        for context in ('_ml_get_interrupts_enabled', '_ml_at_interrupt_context', '_get_preemption_level'):
            require(any(call(row, context) for row in rows))
    result = {'status': 'PASS', 'kernel_sha256': sha, 'run': receipt['run'], 'commit': receipt['commit'],
              'kernel_receipt_sha256': hashlib.sha256(args.kernel_receipt.read_bytes()).hexdigest(),
              'disassembly_sha256': hashlib.sha256(assembly.encode()).hexdigest(),
              'legacy': {name: legacy(blocks[name], symbols) for name in names[3:5]},
              'active_write': active_write(blocks[names[5]], symbols),
              'management_context_and_no_hardware_calls': True,
              'phone_operations': False, 'gpu_execution': False,
              'limitations': ['linked participating XNU control flow, not runtime or resource ownership',
                              'does not exclude secure/EL2/RSC/hardware/other DRV writers',
                              'source guard/ACK and concurrency acceptance remain separate']}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2)+'\n')
    print('Linked RPMh scope/legacy denial/retain-before-store PASS; not runtime: '+str(args.output))


if __name__ == '__main__':
    main()
