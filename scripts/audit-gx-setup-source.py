#!/usr/bin/env python3
"""Compare fixed GX plans with pinned vendor tables; no hardware or FDT proof."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PIN = '36f369fdb77319a2984a48b89f025ac3e5f97386'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kernel', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('preserve existing evidence')
    texts, hashes = {}, {}
    for name in ('adreno_a6xx.c', 'a6xx_reg.h', 'adreno_a6xx_gmu.c'):
        path = 'drivers/gpu/msm/' + name
        recorded = subprocess.check_output(['git', '-C', str(args.kernel), 'show', PIN + ':' + path])
        if recorded != (args.kernel / path).read_bytes():
            raise ValueError('modified vendor source: ' + path)
        texts[name] = recorded.decode()
        hashes[path] = hashlib.sha256(recorded).hexdigest()
    source, defs = texts['adreno_a6xx.c'], texts['a6xx_reg.h']
    registers = {k: int(v, 0) for k, v in re.findall(
        r'^#define\s+(A6XX_\w+)\s+(0[xX][0-9a-fA-F]+|[0-9]+)\s*$', defs, re.M)}

    def table(name):
        match = re.search(r'\b' + name + r'\[\]\s*=\s*\{(.*?)\n\};', source, re.S)
        if not match:
            raise ValueError('table missing: ' + name)
        return [(registers[k], int(v, 0)) for k, v in re.findall(
            r'\{\s*(A6XX_\w+)\s*,\s*(0[xX][0-9a-fA-F]+|[0-9]+)\s*\}', match[1])]

    hwcg, gbif = table('a640_hwcg_regs'), table('a640_gbif')
    if len(hwcg) != 49 or len(gbif) != 5:
        raise ValueError('fixed table size changed')
    start = source.split('static void a6xx_start(', 1)[1].split('static int a6xx_microcode_load(', 1)[0]
    if not start.index('a6xx_hwcg_set(adreno_dev, true)') < start.index('adreno_vbif_start(') < start.index('A6XX_UCHE_WRITE_RANGE_MAX_LO'):
        raise ValueError('vendor stage ordering changed')
    hw = source.split('static void a6xx_hwcg_set(', 1)[1].split('static void', 1)[0]
    off = re.search(r'A6XX_GPU_GMU_GX_SPTPRAC_CLOCK_CONTROL,\s*1,\s*0\)', hw)
    on = re.search(r'A6XX_GPU_GMU_GX_SPTPRAC_CLOCK_CONTROL,\s*0,\s*1\)', hw)
    if not off or not on or not off.start() < hw.index('regs[j].off') < on.start():
        raise ValueError('SP clock/table ordering changed')
    gmu = texts['adreno_a6xx_gmu.c']
    for name, bit in (('SPTPRAC_POWER_OFF', 2), ('SP_CLK_OFF', 4), ('GX_GDSC_POWER_OFF', 6), ('GX_CLK_OFF', 7)):
        if not re.search(r'#define\s+' + name + r'\s+BIT\(' + str(bit) + r'\)', gmu):
            raise ValueError('access status semantics changed: ' + name)

    def rows(values):
        return ','.join('{%s,%s}' % (hex(r), hex(v)) for r, v in values)

    code = '''#include <assert.h>
#include "cepheus_gx_hwcg.h"
#include "cepheus_gx_gbif.h"
static const struct ceph_gx_write hw[]={''' + rows(hwcg) + '''};
static const struct ceph_gx_write gb[]={''' + rows(gbif) + '''};
int main(void){
struct ceph_gx_core_input i={0x06040001,0x100000,0x100000,15,32,3};
struct ceph_gx_hwcg_plan h;struct ceph_gx_gbif_plan g;
assert(!ceph_gx_hwcg_plan(&i,&h) && !ceph_gx_gbif_plan(&i,&g));
for(unsigned n=0;n<49;++n)assert(h.regs[n].word==hw[n].word && h.regs[n].value==hw[n].value);
for(unsigned n=0;n<5;++n)assert(g.writes[n].word==gb[n].word && g.writes[n].value==gb[n].value);
assert(h.gmu_cgc_mode==0x20202 && h.gmu_cgc_delay==0x10111 && h.gmu_cgc_hyst==0x5555);
assert(h.sptprac_clock==0x1a880 && h.top_clock_cntl==0x8aa8aa82);
}
'''
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / 'check.c'
        path.write_text(code)
        exe = Path(directory) / 'check'
        subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'xnu'),
                        str(path), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
    local = ('scripts/audit-gx-setup-source.py', 'xnu/cepheus_gx_hwcg.h', 'xnu/cepheus_gx_gbif.h',
             'xnu/cepheus_gx_core_native.h', 'xnu/cepheus_gx_core_native_private.h')
    result = {'status': 'PASS', 'vendor_commit': PIN, 'vendor_sha256': hashes,
              'local_sha256': {n: hashlib.sha256((ROOT / n).read_bytes()).hexdigest() for n in local},
              'hwcg_entries': hwcg, 'gbif_entries': gbif, 'hwcg_before_gbif_before_core': True,
              'sp_clock_off_table_on_order': True, 'phone_operations': False, 'gpu_execution': False,
              'limitations': ['source comparison only; no live FDT, owner, MMIO or firmware ACK']}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print('Pinned A640 HWCG49/GBIF5 and SP-clock/order/status semantics PASS; not hardware')


if __name__ == '__main__':
    main()
