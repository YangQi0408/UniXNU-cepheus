#!/usr/bin/env python3
"""Accept scoped XNU idle coordination locally; no full kernel or device proof."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--xnu-reference', type=Path,
                        default=ROOT.parent/'kernel-test/xnu')
    args = parser.parse_args()
    temporary = tempfile.TemporaryDirectory() if not args.output else None
    out = args.output.resolve() if args.output else Path(temporary.name)
    if args.output:
        out.mkdir(parents=True, exist_ok=False)
    receipt = {'result': 'RUNNING', 'time_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
               'phone_operations': False, 'gpu_execution': False, 'full_xnu_build': False,
               'foreign_writer_ownership': False, 'steps': [], 'source_sha256': {}}
    for name in ('cepheus_gpu_idle.h', 'cepheus_gpu_idle_native_private.h',
                 '0089-cepheus-gpu-idle.patch', 'test_gpu_idle.c', 'test_gpu_idle_native.c'):
        receipt['source_sha256'][name] = hashlib.sha256((ROOT/'xnu'/name).read_bytes()).hexdigest()
    for name in ('scripts/test-gpu-idle.py', 'scripts/verify-gpu-idle-kernel.py',
                 'tests/gpu-idle-source-host.c'):
        receipt['source_sha256'][name] = hashlib.sha256((ROOT/name).read_bytes()).hexdigest()

    def run(label, command, cwd=ROOT):
        log = out/(label+'.log')
        with log.open('w') as stream:
            result = subprocess.run(command, cwd=cwd, stdout=stream, stderr=subprocess.STDOUT,
                                    timeout=120)
        body = log.read_text()
        code = result.returncode or int('warning:' in body or 'error:' in body)
        receipt['steps'].append({'label': label, 'command': [str(v) for v in command],
                                 'exit_code': code, 'log': log.name,
                                 'sha256': hashlib.sha256(log.read_bytes()).hexdigest()})
        receipt['result'] = 'FAIL' if code else 'RUNNING'
        (out/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
        print(label, 'FAIL' if code else 'PASS', flush=True)
        if code:
            raise SystemExit(body)

    for compiler, label, flags in (('cc', 'host', ['-O2']),
                                   ('clang', 'asan', ['-O1', '-g', '-fsanitize=address,undefined'])):
        for name in ('gpu_idle', 'gpu_idle_native'):
            exe = out/(label+'-'+name)
            run(label+'-'+name+'-compile', [compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-pthread', *flags, ROOT/'xnu'/('test_'+name+'.c'), '-o', exe])
            run(label+'-'+name+'-run', [exe])
    tree = out/'applied'
    receipt['reference_sha256'] = {}
    have_reference = all((args.xnu_reference/'osfmk/arm64'/name).is_file()
                         for name in ('cpu.c', 'machine_routines.c'))
    receipt['reference_applied'] = have_reference
    for name in ('cpu.c', 'machine_routines.c') if have_reference else ():
        source = args.xnu_reference/'osfmk/arm64'/name
        dest = tree/'osfmk/arm64'/name
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(source.read_bytes())
        receipt['reference_sha256'][name] = hashlib.sha256(source.read_bytes()).hexdigest()
    if have_reference:
        run('init', ['git', 'init', '-q', tree])
        run('apply', ['git', 'apply', ROOT/'xnu/0089-cepheus-gpu-idle.patch'], tree)
        cpu = (tree/'osfmk/arm64/cpu.c').read_text()
        machine = (tree/'osfmk/arm64/machine_routines.c').read_text()
        idle = cpu.split('cpu_idle(void)\n{', 1)[1].split('\n/*\n *\tRoutine:\tcpu_idle_exit', 1)[0]
        exit_body = cpu.split('cpu_idle_exit(boolean_t from_reset)\n{', 1)[1].split('\nvoid\ncpu_init', 1)[0]
        assert idle.count('ceph_idle_enter(') == 1
        assert idle.count('ceph_idle_leave(') == 2
        assert idle.index('ceph_idle_enter(') < idle.index('wfe_to_deadline_or_interrupt(') < idle.index('__builtin_arm_wfi(')
        assert 'if (!from_reset) {\n\t\tceph_idle_leave(' in exit_body
        assert machine.count('cepheus_gpu_idle_cpu_enter(') == 1
        assert machine.count('cepheus_gpu_idle_cpu_leave(') == 1
        assert 'cepheus_gpu_idle_cpu_leave(ceph_idle_cpu)' in machine
        receipt['applied_sha256'] = {name: hashlib.sha256((tree/'osfmk/arm64'/name).read_bytes()).hexdigest()
                                    for name in ('cpu.c', 'machine_routines.c')}
        assert idle.count('Idle_load_context();') == 4
        assert exit_body.count('Idle_load_context();') == 1
        actual_idle = 'void __attribute__((noreturn))\ncpu_idle(void)\n{' + idle
        actual_exit = 'void\ncpu_idle_exit(boolean_t from_reset)\n{' + exit_body
        actual_machine = 'void\nmachine_idle(void)\n{' + machine.split('machine_idle(void)\n{', 1)[1].split('\nvoid\nOSSynchronizeIO', 1)[0]
        harness = (ROOT/'tests/gpu-idle-source-host.c').read_text()
        for marker, body in (('APPLIED_CPU_IDLE', actual_idle),
                             ('APPLIED_CPU_IDLE_EXIT', actual_exit),
                             ('APPLIED_MACHINE_IDLE', actual_machine)):
            assert harness.count('/* '+marker+' */') == 1
            harness = harness.replace('/* '+marker+' */', body)
        file = out/'applied-idle.c'
        file.write_text(harness)
        for label, flags in (('host', ['-O2']), ('asan', ['-O1', '-g', '-fsanitize=address,undefined'])):
            exe = out/(label+'-applied-idle')
            run(label+'-applied-compile', ['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-I', ROOT/'xnu', *flags, file, '-o', exe])
            run(label+'-applied-run', [exe])
    else:
        print('Applied reference check SKIPPED: no local XNU reference; CI must verify real build tree')
    native = (ROOT/'xnu/cepheus_gpu_idle_native_private.h').read_text()
    for forbidden in ('ml_io_map', 'cpu_signal(', 'wfi=', 'idle_enable=', 'smc', 'cepheus_gpu_submit'):
        assert forbidden not in native, forbidden
    # Compile the production private body with only platform declarations.
    file = out/'native.c'
    file.write_text('''#include <stdint.h>
#include <stddef.h>
extern int ml_get_interrupts_enabled(void);
extern int ml_at_interrupt_context(void);
extern int get_preemption_level(void);
extern uint64_t mach_absolute_time(void);
extern void absolutetime_to_nanoseconds(uint64_t,uint64_t *);
extern void delay(int);
#include "cepheus_gpu_idle_native_private.h"
''')
    command = ['clang', '--target=aarch64-none-elf', '-std=c11', '-Wall', '-Wextra', '-Werror',
               '-O2', '-ffreestanding', '-I', ROOT/'xnu']
    run('native-compile', [*command, '-c', file, '-o', out/'native.o'])
    run('native-assembly', [*command, '-S', file, '-o', out/'native.s'])
    assembly = (out/'native.s').read_text()
    for name in ('hold', 'check', 'finish', 'cpu_enter', 'cpu_leave'):
        assert 'cepheus_gpu_idle_'+name+':' in assembly
    assert 'ldaxr' in assembly and 'stlxr' in assembly
    for name in ('ml_io_map', 'cpu_signal', 'smc\t', 'wfi\n', 'wfe\n'):
        assert name not in assembly, name
    run('native-analysis', ['clang', '--analyze', '-std=c11', '-I', ROOT/'xnu',
        '-Xanalyzer', '-analyzer-output=text', '-o', os.devnull, file])
    receipt['result'] = 'PASS'
    (out/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
    print('GPU scoped idle local PASS:', out/'receipt.json')


if __name__ == '__main__':
    main()
