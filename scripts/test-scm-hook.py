#!/usr/bin/env python3
"""Run production opt-in helper/one-shot wrapper with RAM SMC substitute.

No real SMC is compiled into this host test. The separate codegen test builds
the real callback for AArch64 without executing it. No PAS mutation calls.
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
soc = (ROOT/'xnu/cepheus_soc.c').read_text()
helper = soc.split('static void soc_scm_probe(void)',1)[1].split('static void soc_thread',1)[0]
thread = soc.split('static void soc_thread',1)[1].split('void cepheus_soc_init',1)[0]
assert thread.count('soc_scm_probe();') == 1
assert thread.index('osm_sample(25);') < thread.index('soc_scm_probe();')
assert 'cephscm' not in (ROOT/'xnu/cepheus_touch.c').read_text()
source = (ROOT/'xnu/cepheus_gpu_compile.c').read_text()
wrapper = source.split('int cepheus_gpu_scm_probe_once',1)[1].split('#endif',1)[0]
assert wrapper.count('ceph_scm_discover_pas13(')==1
assert wrapper.index('ceph_scm_info_attempted=1') < wrapper.index('ceph_scm_discover_pas13(')
assert 'lck_mtx_lock(&ceph_scm_info_lock)' in wrapper
for name in ('rh_send', 'rh_write', 'cepheus_gpu_submit', 'ceph_power_start'):
    assert name+'(' not in wrapper+helper
prefix = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define KERNEL 1
#include "cepheus_scm_discovery.h"
static unsigned ceph_scm_info_attempted, calls, locked;
static int ceph_scm_info_lock, enabled, bad_context, fail_transport, soc_enabled=1, conflict;
static void lck_mtx_lock(int *p) {assert(p==&ceph_scm_info_lock && !locked);locked=1;}
static void lck_mtx_unlock(int *p) {assert(p==&ceph_scm_info_lock && locked);locked=0;}
static int ceph_scm_info_context(void *unused) {(void)unused;return bad_context?-1:0;}
static int ceph_scm_info_invoke(void *unused, struct ceph_scm_regs *r)
{
    (void)unused;assert(locked);++calls;
    uint32_t fn=(uint32_t)r->x[0]&~0xc0000000u;
    assert(fn==0x02000601 || fn==0x02000207); /* never PAS1/2/5/6 */
    assert(r->x[1]==1 && !r->x[3] && !r->x[4] && !r->x[5] && !r->x[6] && !r->x[7]);
    if(fn==0x02000207)assert(r->x[2]==13);
    if(fail_transport)return -1;
    r->x[0]=0;r->x[1]=1;return 0;
}
static int PE_parse_boot_argn(const char *key, int *value, unsigned bytes)
{
    assert(bytes==sizeof(*value));
    if(!strcmp(key,"cephscm")) {
        if(enabled==-1)return 0;
        *value=enabled;return 1;
    }
    if(!strcmp(key,"cephsoc")) {*value=soc_enabled;return 1;}
    assert(!strcmp(key,"cephboost") || !strcmp(key,"cephmem"));
    *value=0; /* mere presence, including =0, must refuse mixed profiles */
    return conflict==(!strcmp(key,"cephboost")?1:2);
}
'''
suffix = r'''
int main(void)
{
    struct ceph_scm_discovery result;
    for(enabled=-1;enabled<=2;++enabled) {
        calls=ceph_scm_info_attempted=0;
        soc_scm_probe();
        assert(calls==(enabled==1?7u:0u) && !locked);
        if(enabled==1) {soc_scm_probe();assert(calls==7 && !locked);}
    }
    calls=ceph_scm_info_attempted=0;enabled=1;
    for(soc_enabled=0;soc_enabled<=2;soc_enabled+=2) {soc_scm_probe();assert(!calls && !ceph_scm_info_attempted);}
    soc_enabled=1;
    for(conflict=1;conflict<=2;++conflict) {soc_scm_probe();assert(!calls && !ceph_scm_info_attempted);}
    conflict=0;bad_context=1;
    soc_scm_probe();assert(!calls && !ceph_scm_info_attempted && !locked);
    bad_context=0;fail_transport=1;soc_scm_probe();
    assert(calls==1 && ceph_scm_info_attempted && !locked);
    soc_scm_probe();assert(calls==1 && !locked);
    assert(cepheus_gpu_scm_probe_once(NULL)<0 && calls==1);
    ceph_scm_info_attempted=0;fail_transport=0;
    assert(!cepheus_gpu_scm_probe_once(&result) && result.supported && result.calls==7 && !locked);
    assert(cepheus_gpu_scm_probe_once(&result)<0 && calls==8 && !locked);
    puts("Production SCM hook/once wrapper: opt-in1 only, serialization, errors latched and fixed queries PASS (RAM only)");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    c = path/'hook.c'
    c.write_text(prefix+'int cepheus_gpu_scm_probe_once'+wrapper+'static void soc_scm_probe(void)'+helper+suffix)
    exe = path/'hook'
    subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror',
                    '-fsanitize=undefined','-I',str(ROOT/'xnu'),str(c),'-o',str(exe)],check=True)
    print(subprocess.check_output([str(exe)],text=True).splitlines()[-1])
