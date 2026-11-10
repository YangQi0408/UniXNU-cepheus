#!/usr/bin/env python3
"""Compile the actual private SMC callback for AArch64; never execute it."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'xnu/cepheus_gpu_compile.c').read_text()
body = source.split('static int ceph_scm_info_invoke', 1)[1].split('int cepheus_gpu_scm_probe_once', 1)[0]
preamble = '''
typedef unsigned long long uint64_t;
#define NULL ((void *)0)
struct ceph_scm_regs { uint64_t x[8]; };
extern int ceph_scm_info_context(void *);
extern void disable_preemption(void);
extern void enable_preemption(void);
extern int printf(const char *, ...);
'''
with tempfile.TemporaryDirectory() as directory:
    work = Path(directory)
    file = work / 'scm.c'
    file.write_text(preamble + 'int ceph_scm_info_invoke' + body)
    assembly = work / 'scm.s'
    subprocess.run(['clang', '--target=aarch64-none-elf', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-ffreestanding', '-S', str(file), '-o', str(assembly)], check=True)
    text = assembly.read_text()
    assert text.count('smc\t#0') == 1, 'one physical SMC per callback'
    # Return the A6 quirk cookie and input/output values, not stale inputs.
    assert 'x6' in text and 'x7' in text and 'ceph_scm_info_context' in text
    subprocess.run(['clang', '--target=aarch64-none-elf', '-c', str(assembly), '-o', str(work / 'scm.o')], check=True)
    data = (work / 'scm.o').read_bytes()
    for kind in ('INFO', 'PAS'):
        assert ('CEPHEUS SCM ' + kind + ' REGS: in=').encode() in data
        assert ('CEPHEUS SCM ' + kind + ' REGS: out=').encode() in data
    # Compile the actual query policy too: the callback-only check cannot
    # establish that the new command7 enum/path compiles for the kernel target.
    query = work / 'query.c'
    query.write_text('#include "cepheus_scm_discovery.h"\n'
                     'int query_policy(struct ceph_scm_discovery *s, struct ceph_scm_discovery_ops *b) '
                     '{return ceph_scm_discover_pas13(s,b);}\n')
    subprocess.run(['clang', '--target=aarch64-none-elf', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-ffreestanding', '-I', str(root / 'xnu'), '-c', str(query), '-o', str(work / 'query.o')], check=True)
    # Compile the actual native PAS integration, including its cached query
    # gate and one-shot/serialization logic. Only platform declarations are
    # stand-ins; no device execution is implied by this object file.
    native = source.split('static LCK_GRP_DECLARE', 1)[1].split('\n#endif', 1)[0]
    declarations = '''
#define KERNEL 1
#include "cepheus_zap_pas.h"
#include "cepheus_gpll0.h"
#include "cepheus_clock_measure.h"
#include "cepheus_gpu_power_mmio.h"
#include "cepheus_gpu_power_native.h"
extern void delay(unsigned);
extern uintptr_t ml_io_map(uint64_t,size_t);
extern uint64_t mach_absolute_time(void);
extern void absolutetime_to_nanoseconds(uint64_t,uint64_t *);
#define LCK_GRP_DECLARE(n,s) int n __attribute__((unused))
#define LCK_MTX_DECLARE(n,g) int n __attribute__((unused))
extern void lck_mtx_lock(int *);
extern void lck_mtx_unlock(int *);
extern int ml_get_interrupts_enabled(void);
extern int ml_at_interrupt_context(void);
extern int get_preemption_level(void);
extern void disable_preemption(void);
extern void enable_preemption(void);
extern int printf(const char *, ...);
'''
    path = work / 'pas-native.c'
    path.write_text(declarations + 'static LCK_GRP_DECLARE' + native)
    subprocess.run(['clang', '--target=aarch64-none-elf', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-ffreestanding', '-I', str(root / 'xnu'), '-c', str(path), '-o', str(work / 'pas-native.o')], check=True)
print('Production SMC callback, query policy and native PAS lifecycle: AArch64 compile/assemble PASS; not executed')
