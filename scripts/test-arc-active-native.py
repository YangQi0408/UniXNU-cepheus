#!/usr/bin/env python3
"""Compile the actual ARC adapter within the pinned RPMh translation unit.

Platform declarations stand in for XNU headers. No firmware, MMIO or SMC runs.
"""
import argparse
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('patches', type=Path, nargs='?', default=ROOT.parent/'inputs/ios-sweet-ordered-touch/xnu/patches')
parser.add_argument('--headers', type=Path, default=ROOT/'xnu')
args = parser.parse_args()
PINNED = args.patches.resolve()
if not PINNED.is_dir():
    raise SystemExit('Supply the pinned ios-sweet patch directory')
spec = importlib.util.spec_from_file_location('power', ROOT / 'scripts/prepare-touch-power-patch.py')
power = importlib.util.module_from_spec(spec)
spec.loader.exec_module(power)
declarations = '''
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef uintptr_t vm_offset_t;
typedef uintptr_t vm_size_t;
#define __unused __attribute__((unused))
#define EIO 5
#define ENOMEM 12
#define EBUSY 16
#define ENODEV 19
#define EINVAL 22
#define ENOSPC 28
#define ETIMEDOUT 60
extern int printf(const char *, ...);
extern int strcmp(const char *,const char *);
extern void delay(unsigned);
extern int ml_get_interrupts_enabled(void);
extern int ml_at_interrupt_context(void);
'''

with tempfile.TemporaryDirectory() as directory:
    tree = Path(directory)
    subprocess.run(['git', 'init', '-q', str(tree)], check=True)
    for patch in sorted(PINNED.glob('*.patch')):
        if patch.name == '0086-cepheus-gpu-arc.patch':
            continue
        subprocess.run(['git', '-C', str(tree), 'apply', '--whitespace=nowarn',
                        '--include=bsd/dev/sweet_rpmh.c', str(patch)], check=True)
    source = tree / power.TARGET
    source.write_text(power.transform(source.read_text()))
    subprocess.run(['git', '-C', str(tree), 'apply', str(ROOT/'xnu/0086-cepheus-gpu-arc.patch')], check=True)
    # Preserve every function and real inline barrier; substitute declarations
    # for headers unavailable on Linux, not for the ARC/RPMh implementation.
    body = '\n'.join(line for line in source.read_text().splitlines()
                     if not line.startswith('#include <'))
    source.write_text(declarations + body + '\n')
    command = ['clang', '--target=aarch64-none-elf', '-std=c11', '-O2', '-ffreestanding',
               '-Wall', '-Wextra', '-Werror', '-DARM64_BOARD_CONFIG_SWEET=1',
               '-I', str(args.headers.resolve())]
    assembly = tree/'rpmh.s'
    subprocess.run(command + ['-S', str(source), '-o', str(assembly)], check=True)
    text = assembly.read_text()
    if any(symbol not in text for symbol in ('cepheus_gpu_arc_raise:', 'cepheus_gpu_bcm_raise:', 'dsb\tosh', 'dmb\tosh')):
        raise SystemExit('Missing native ARC/BCM symbol or actual MMIO barriers')
    subprocess.run(command + ['-c', str(source), '-o', str(tree/'rpmh.o')], check=True)
    analysis = subprocess.run(command + ['--analyze', '-Xanalyzer', '-analyzer-output=text',
                                         str(source), '-o', '/dev/null'], capture_output=True, text=True)
    if analysis.returncode or 'warning:' in analysis.stderr or 'error:' in analysis.stderr:
        raise SystemExit(analysis.stdout + analysis.stderr)
print('Pinned RPMh + actual ARC/BCM ACTIVE adapters: AArch64 compile/assembly and static analysis PASS; not executed')
