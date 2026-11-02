#!/usr/bin/env python3
"""Compare restore register offsets and CP ordinals against explicit KGSL source."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('kgsl_dir',type=Path);a=p.parse_args()
    source=(a.kgsl_dir/'adreno_a6xx.c').read_text();regs=(a.kgsl_dir/'a6xx_reg.h').read_text()
    defs={k:int(v,0) for k,v in re.findall(r'^#define\s+(A6XX_\w+)\s+(0[xX][0-9a-fA-F]+|[0-9]+)\s*$',regs,re.M)}
    arrays=[]
    for name in ('a6xx_ifpc_pwrup_reglist','a6xx_pwrup_reglist'):
        match=re.search(name+r'\[\]\s*=\s*\{(.*?)\n\};',source,re.S)
        if not match:raise ValueError('missing source register list')
        values=[defs[k]+int(extra or 0) for k,extra in re.findall(r'\{\s*(A6XX_\w+)(?:\s*\+\s*(\d+))?\s*,',match.group(1))]
        arrays.extend(values)
    if len(arrays)!=66:raise ValueError('source register count changed')
    lock=re.search(r'struct cpu_gpu_lock\s*\{[^{}]*\};',(a.kgsl_dir/'adreno_a6xx.h').read_text()).group(0)
    snippet=r'''
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include "cepheus_cp_init.h"
''' + lock + '\nstatic const uint32_t offsets[]={' + ','.join(hex(v) for v in arrays) + '};\n' + r'''
_Static_assert(sizeof(struct cpu_gpu_lock)==16,"lock size");
_Static_assert(offsetof(struct cpu_gpu_lock,list_length)==12,"length offset");
_Static_assert(offsetof(struct cpu_gpu_lock,list_offset)==14,"list offset");
int main(void) {
    for(unsigned i=0;i<66;++i)assert(offsets[i]==ceph_cp_restore_offsets[i]);
    uint32_t packet[12];assert(ceph_cp_me_init(packet,12,0x100004000ULL,4096)==12);
    const uint32_t expected[]={0x70c8000b,0x16f,3,0x20000000,0,0,0,2,0x4000,1,0,0};
    for(unsigned i=0;i<12;++i)assert(packet[i]==expected[i]);
}
'''
    # Fixed source mask is independently recomputed from declared bit positions.
    bits={name:int(n) for name,n in re.findall(r'^#define\s+(CP_INIT_\w+)\s+BIT\((\d+)\)',source,re.M)}
    enabled=('MAX_CONTEXT','ERROR_DETECTION_CONTROL','HEADER_DUMP','DEFAULT_RESET_STATE','UCODE_WORKAROUND_MASK','OPERATION_MODE_MASK','REGISTER_INIT_LIST_WITH_SPINLOCK')
    if sum(1<<bits['CP_INIT_'+name] for name in enabled)!=0x16f:raise ValueError('source ordinal mask changed')
    with tempfile.TemporaryDirectory(prefix='ceph-cp-source-') as tmp:
        c=Path(tmp)/'test.c';c.write_text(snippet);binary=Path(tmp)/'test'
        subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined',
                        '-I',str(ROOT/'xnu'),str(c),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
    print('CP restore offsets/lock ABI/ME_INIT mask match supplied KGSL source: PASS')


if __name__=='__main__':main()
