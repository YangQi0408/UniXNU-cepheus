#!/usr/bin/env python3
"""Compile the exact production GX read-only probe against RAM callbacks.

Optional explicit KGSL source validates register names independently. This
checks address units and gate ordering, not real clock ownership or hardware.
"""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'xnu/cepheus_soc.c').read_text()
body = source.split('static void gpu_gx_probe(void)', 1)[1]
body = re.split(r'\nstatic (?:bool|void) \w+\(', body, maxsplit=1)[0]
assert 'soc_scm_pair' not in body and 'soc_zap_memory' not in body
assert not re.search(r'\*\s*\(volatile[^\n]+\)\s*=', body)
assert not any(name+'(' in body for name in ('osm_request', 'rh_send', 'rh_write', 'ceph_gmu_start_cold'))
assert 'if(gxprobe==1) {gpu_gx_probe();}' in source
registers = {
    'A6XX_CP_SQE_CNTL': 0x808, 'A6XX_CP_HW_FAULT': 0x821,
    'A6XX_CP_PROTECT_CNTL': 0x84f, 'A6XX_CP_RB_BASE': 0x800, 'A6XX_CP_RB_BASE_HI': 0x801,
    'A6XX_CP_SQE_INSTR_BASE_LO': 0x830, 'A6XX_CP_SQE_INSTR_BASE_HI': 0x831,
    'A6XX_CP_ADDR_MODE_CNTL': 0x842, 'A6XX_VSC_ADDR_MODE_CNTL': 0xc01,
    'A6XX_GRAS_ADDR_MODE_CNTL': 0x8601, 'A6XX_RB_ADDR_MODE_CNTL': 0x8e05,
    'A6XX_PC_ADDR_MODE_CNTL': 0x9e01, 'A6XX_HLSQ_ADDR_MODE_CNTL': 0xbe05,
    'A6XX_VFD_ADDR_MODE_CNTL': 0xa601, 'A6XX_VPC_ADDR_MODE_CNTL': 0x9601,
    'A6XX_UCHE_ADDR_MODE_CNTL': 0xe00, 'A6XX_SP_ADDR_MODE_CNTL': 0xae01,
    'A6XX_TPL1_ADDR_MODE_CNTL': 0xb601, 'A6XX_RBBM_SECVID_TSB_ADDR_MODE_CNTL': 0xf810,
    'A6XX_UCHE_TRAP_BASE_LO': 0xe09, 'A6XX_UCHE_TRAP_BASE_HI': 0xe0a,
    'A6XX_UCHE_GMEM_RANGE_MIN_LO': 0xe0b, 'A6XX_UCHE_GMEM_RANGE_MIN_HI': 0xe0c,
    'A6XX_UCHE_GMEM_RANGE_MAX_LO': 0xe0d, 'A6XX_UCHE_GMEM_RANGE_MAX_HI': 0xe0e,
}
if len(sys.argv) == 2:
    kgsl = Path(sys.argv[1]) / 'drivers/gpu/msm'
    definitions = (kgsl / 'a6xx_reg.h').read_text()
    for name, value in registers.items():
        match = re.search(r'^#define\s+'+name+r'\s+(0x[0-9a-fA-F]+)\b', definitions, re.M)
        assert match and int(match[1], 16) == value, name
    original = (kgsl / 'adreno.c').read_text()
    assert 'reg = (device->reg_virt + (offsetwords << 2))' in original
    print('KGSL register constants and DWORD-to-byte convention PASS (source only)')

prefix = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "cepheus_gpu_power.h"
typedef uintptr_t vm_offset_t;
enum {GCC=1, CC, GMU, GX};
static unsigned scenario, gmu_maps, gmu_reads, gx_maps, gx_reads;
static unsigned seen[25];
static const unsigned expected[25] = {
    0x808,0x821,0x84f,0x800,0x801,0x830,0x831,
    0x842,0xc01,0x8601,0x8e05,0x9e01,0xbe05,0xa601,0x9601,0xe00,0xae01,0xb601,0xf810,
    0xe09,0xe0a,0xe0b,0xe0c,0xe0d,0xe0e
};
static vm_offset_t ml_io_map(vm_offset_t pa, unsigned bytes)
{
    if(pa==0x00100000) {assert(bytes==0x90000);return scenario==1?0:GCC;}
    if(pa==0x02c90000) {assert(bytes==0x9000);return scenario==3?0:CC;}
    if(pa==0x02c6a000) {assert(bytes==0x30000);++gmu_maps;return scenario==5?0:GMU;}
    assert(pa==0x02c00000 && bytes==0x40000);++gx_maps;
    return scenario==10?0:GX;
}
static uint32_t sr(vm_offset_t base, unsigned off)
{
    assert(!(off&3u));
    if(base==GCC) {
        assert(off==CE_GPU_GCC_CFG || off==CE_GPU_GCC_MEMNOC || off==CE_GPU_GCC_DVM || off==CE_GPU_GCC_DDR);
        return off==CE_GPU_GCC_CFG && scenario==2?UINT32_MAX:0;
    }
    if(base==CC) {
        assert(off==CE_GPU_CX_GDSCR || off==CE_GPU_CX_HW_CTRL || off==CE_GPU_GX_GDSCR ||
               off==CE_GPU_AHB_CBCR || off==CE_GPU_GMU_CBCR || off==CE_GPU_CXO_CBCR || off==CE_GPU_GMU_CMD);
        if(off==CE_GPU_CX_HW_CTRL) return scenario==4?0:0x80000000u;
        return 0;
    }
    if(base==GMU) {
        ++gmu_reads;
        assert(off==0x1f8d0u*4-0x6a000 || off==0x23b10u*4-0x6a000);
        if(off==0x1f8d0u*4-0x6a000) return scenario==6?UINT32_MAX:scenario==8?0xd4:0;
        return scenario==7?UINT32_MAX:scenario==9?1:0;
    }
    assert(base==GX && gx_maps==1 && gmu_reads==2);
    unsigned i;for(i=0;i<25;++i) if(off==expected[i]*4)break;
    assert(i<25 && !seen[i]);seen[i]=1;++gx_reads;
    return i>=7 && i<19?1:0;
}
'''
suffix = r'''
int main(void)
{
    for(scenario=0;scenario<=10;++scenario) {
        gmu_maps=gmu_reads=gx_maps=gx_reads=0;memset(seen,0,sizeof(seen));
        gpu_gx_probe();
        if(!scenario) {assert(gx_reads==25);for(unsigned i=0;i<25;++i)assert(seen[i]);}
        else assert(!gx_reads);
        if(scenario>=1 && scenario<=4)assert(!gmu_maps && !gmu_reads && !gx_maps);
        if(scenario==5)assert(gmu_maps==1 && !gmu_reads && !gx_maps);
        if(scenario>=6 && scenario<=9)assert(gmu_reads==2 && !gx_maps);
        if(scenario==10)assert(gmu_reads==2 && gx_maps==1);
    }
    puts("Production GX probe: exact byte addresses, CP_HW_FAULT821, full GMEM pairs and refusal gates PASS (RAM only)");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    c = path / 'gx.c'
    c.write_text(prefix+'static void gpu_gx_probe(void)'+body+suffix)
    exe = path / 'gx'
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=undefined', '-I', str(ROOT/'xnu'), str(c), '-o', str(exe)], check=True)
    output = subprocess.check_output([str(exe)], text=True)
    assert 'addr_mode_mask=0xfff' in output
    print(output.splitlines()[-1])
