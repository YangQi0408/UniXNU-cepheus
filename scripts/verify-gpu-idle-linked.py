#!/usr/bin/env python3
"""Independently check actual SWEET idle linked control flow, not runtime."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kernel', type=Path, required=True)
    parser.add_argument('--kernel-receipt', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    assert not args.output.exists()
    data = args.kernel.read_bytes()
    sha = hashlib.sha256(data).hexdigest()
    base = json.loads(args.kernel_receipt.read_text())
    assert base['status'] == 'PASS' and base['kernel_sha256'] == sha
    names = ['_cpu_idle', '_cpu_idle_exit', '_machine_idle', '_ceph_idle_leave',
             '_ceph_gpu_idle_now'] + ['_cepheus_gpu_idle_'+name for name in
                                      ('hold', 'check', 'finish', 'cpu_enter', 'cpu_leave')]
    assembly = subprocess.check_output(['llvm-objdump', '-d', '--no-show-raw-insn',
                                       '--disassemble-symbols='+','.join(names), str(args.kernel)], text=True)
    blocks = {}
    current = None
    for line in assembly.splitlines():
        label = re.fullmatch(r'([0-9a-f]+) <([^>]+)>:', line)
        if label:
            current = label[2]
            blocks[current] = []
        instruction = re.match(r'([0-9a-f]+):\s+(\S+)(?:\s+(.*))?$', line)
        if instruction and current:
            operands = (instruction[3] or '').split(';', 1)[0].strip()
            blocks[current].append((int(instruction[1], 16), instruction[2], operands))
    assert set(names) <= blocks.keys()
    addresses = {name: rows[0][0] for name, rows in blocks.items()}

    def call(row, name):
        return row[1] in ('bl', 'b') and row[2].endswith('<'+name+'>')

    def targets(row):
        match = re.search(r'0x([0-9a-f]+) <', row[2])
        return int(match[1], 16) if match else None

    def flow(rows, start, stop, forbidden):
        by_address = {row[0]: index for index, row in enumerate(rows)}
        pending = [start]
        seen = set()
        reached = False
        while pending:
            index = pending.pop()
            if index in seen:
                continue
            seen.add(index)
            row = rows[index]
            if stop(row):
                reached = True
                continue
            assert not forbidden(row), row
            op = row[1]
            if op == 'ret':
                continue
            if op == 'b' or op.startswith('b.') or op in ('cbz', 'cbnz', 'tbz', 'tbnz'):
                target = targets(row)
                assert target in by_address, row
                pending.append(by_address[target])
                if op == 'b':
                    continue
            if index + 1 < len(rows):
                pending.append(index + 1)
        assert reached

    machine = blocks['_machine_idle']
    enter = next(index for index, row in enumerate(machine) if call(row, '_cepheus_gpu_idle_cpu_enter'))
    assert machine[enter+1][1] == 'cbz' and machine[enter+1][2].startswith('w0, ')
    denied = next(index for index, row in enumerate(machine) if row[0] == targets(machine[enter+1]))
    assert call(machine[denied], '_Idle_context')
    assert not any('wfe' in row[1] for row in machine[:enter])
    assert any(call(row, '_wfe_to_deadline_or_interrupt') for row in machine[enter:])
    flow(machine, enter+2,
         lambda row: call(row, '_cepheus_gpu_idle_cpu_leave'),
         lambda row: call(row, '_Idle_context') or row[1] == 'ret')
    leaves = [row for index, row in enumerate(machine) if call(row, '_cepheus_gpu_idle_cpu_leave')
              and machine[index-1][1:] == ('mov', 'x0, x19')]
    assert len(leaves) == 2

    idle = blocks['_cpu_idle']
    acquire = next(index for index, row in enumerate(idle) if row[1] == 'casal')
    early = next(row for row in idle if call(row, '_Idle_load_context'))
    assert idle[acquire-2][1] == 'orr' and idle[acquire+3][1] == 'b.ne'
    assert any(row[1] == 'cbnz' and targets(row) == early[0] for row in idle[:acquire])
    assert acquire < next(index for index, row in enumerate(idle) if row[1] in ('wfe', 'wfi'))
    flow(idle, acquire+4, lambda row: call(row, '_cpu_idle_exit') or call(row, '_ceph_idle_leave'),
         lambda row: call(row, '_Idle_load_context'))
    assert any(row[1] == 'wfi' for row in idle)

    exit_rows = blocks['_cpu_idle_exit']
    bypass = next(index for index, row in enumerate(exit_rows)
                  if row[1] == 'cbnz' and row[2].startswith('w19, ') and index > len(exit_rows)//2)
    assert call(next(row for row in exit_rows if row[0] == targets(exit_rows[bypass])), '_Idle_load_context')
    assert any(row[1] == 'casal' for row in exit_rows[bypass:])
    assert any(row[1:] == ('mov', 'x19, x0') for row in exit_rows[:bypass])
    for name in ('_cepheus_gpu_idle_cpu_enter', '_cepheus_gpu_idle_cpu_leave', '_ceph_idle_leave'):
        assert all(any(row[1] == instruction for row in blocks[name]) for instruction in ('ldar', 'casal', 'ldsetal'))
    for name in names[3:]:
        assert not any(row[1] in ('wfi', 'wfe', 'smc') or '_ml_io_map' in row[2] for row in blocks[name])
    for name in ('_cepheus_gpu_idle_hold', '_cepheus_gpu_idle_check', '_cepheus_gpu_idle_finish'):
        for context in ('_ml_get_interrupts_enabled', '_ml_at_interrupt_context', '_get_preemption_level'):
            assert any(call(row, context) for row in blocks[name])
    result = {'status': 'PASS', 'kernel_sha256': sha, 'kernel_bytes': len(data),
              'run': base['run'], 'commit': base['commit'], 'addresses': addresses,
              'kernel_receipt_sha256': hashlib.sha256(args.kernel_receipt.read_bytes()).hexdigest(),
              'disassembly_sha256': hashlib.sha256(assembly.encode()).hexdigest(),
              'linked_entries': 5, 'machine_denial_bypasses_wfe': True,
              'machine_accepted_paths_leave_before_context': True,
              'cpu_accepted_paths_leave_or_exit_before_context': True,
              'normal_exit_release_reset_bypass': True,
              'phone_operations': False, 'gpu_execution': False, 'runtime_idle_policy': False}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2)+'\n')
    print('Actual linked idle entries/control-flow PASS; not runtime: '+str(args.output))


if __name__ == '__main__':
    main()
