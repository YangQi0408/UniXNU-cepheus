#!/usr/bin/env python3
"""Verify applied scoped idle source and linked native entries, not runtime policy."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('xnu', type=Path)
    parser.add_argument('kernel', type=Path)
    args = parser.parse_args()
    for name in ('cepheus_gpu_idle.h', 'cepheus_gpu_idle_native_private.h'):
        assert (args.xnu/'osfmk/arm64'/name).read_bytes() == (ROOT/'xnu'/name).read_bytes(), name
    cpu = (args.xnu/'osfmk/arm64/cpu.c').read_text()
    machine = (args.xnu/'osfmk/arm64/machine_routines.c').read_text()
    assert cpu.count('#include "cepheus_gpu_idle_native_private.h"') == 1
    idle = cpu.split('cpu_idle(void)\n{', 1)[1].split('\n/*\n *\tRoutine:\tcpu_idle_exit', 1)[0]
    exit_body = cpu.split('cpu_idle_exit(boolean_t from_reset)\n{', 1)[1].split('\nvoid\ncpu_init', 1)[0]
    assert idle.count('ceph_idle_enter(') == 1 and idle.count('ceph_idle_leave(') == 2
    assert idle.index('ceph_idle_enter(') < idle.index('wfe_to_deadline_or_interrupt(') < idle.index('__builtin_arm_wfi(')
    assert 'if (!from_reset) {\n\t\tceph_idle_leave(' in exit_body
    assert machine.count('cepheus_gpu_idle_cpu_enter(') == 1
    assert machine.count('cepheus_gpu_idle_cpu_leave(') == 1
    assert 'cepheus_gpu_idle_cpu_leave(ceph_idle_cpu)' in machine
    nm = subprocess.check_output(['xcrun', '--find', 'nm'], text=True).strip() if sys.platform == 'darwin' else shutil.which('llvm-nm')
    assert nm
    output = subprocess.check_output([nm, str(args.kernel)], text=True)
    symbols = {line.split()[-1] for line in output.splitlines()
               if len(line.split()) == 3 and line.split()[-2].upper() != 'U'}
    assert {'_cepheus_gpu_idle_'+name for name in ('hold', 'check', 'finish', 'cpu_enter', 'cpu_leave')} <= symbols
    print('GPU idle applied sources and 5 linked native entries PASS (not runtime/foreign-writer proof)')


if __name__ == '__main__':
    main()
