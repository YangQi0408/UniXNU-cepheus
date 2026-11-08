#!/usr/bin/env python3
"""Run the production delay guard/thread with host-only probe and clock stubs."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
soc = (ROOT / 'xnu/cepheus_soc.c').read_text()
helpers = 'static bool soc_zap_memory' + soc.split('static bool soc_zap_memory', 1)[1].split('static void soc_scm_probe', 1)[0]
thread = 'static void soc_thread' + soc.split('static void soc_thread', 1)[1].split('void cepheus_soc_init', 1)[0]
assert thread.index('soc_probe_delay(&delay)') < thread.index('swait(delay * 1000)') < thread.index('osm_dump(')
assert thread.index('soc_probe_mark("begin"') < thread.index('osm_dump(')
assert thread.index('osm_sample(25)') < thread.index('soc_probe_mark("baseline-end"') < thread.index('soc_scm_probe()')
assert thread.count('soc_scm_probe();') == 1
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
typedef int wait_result_t;
typedef uintptr_t vm_offset_t;
#define NSEC_PER_MSEC 1000000ULL
#define NSEC_PER_SEC 1000000000ULL
#define THREAD_CONTINUE_NULL 0
static const char *conflicts[] = {"cephfuse", "cepharc", "cephgx", "cephscm", "cephboost", "cephmem"};
static int delay_present, delay_value, soc_value=1, soc_present=1, conflict=-1, conflict_value;
static int zap_present,zap_value;
static unsigned zap_tests;
static unsigned waits, first_wait, dumps, gpu, samples, maps, reads, optional, scm, terminated, blocked, marks;
static unsigned expected_delay, last_sample;
static uint64_t now;
static int banks[4];
static vm_offset_t tsens;
static int PE_parse_boot_argn(const char *key, int *value, unsigned bytes)
{
    assert(bytes == sizeof(*value));
    if (!strcmp(key, "cephsocdelay")) {if (!delay_present) return 0; *value=delay_value; return 1;}
    if (!strcmp(key, "cephsoc")) {*value=soc_value; return soc_present;}
    if (!strcmp(key, "cephzapmem")) {*value=zap_value; return zap_present;}
    for (unsigned n=0; n<6; ++n) if (!strcmp(key, conflicts[n])) {
        if ((int)n != conflict) return 0;
        *value=conflict_value; return 1;
    }
    assert(0); return 0;
}
static uint64_t mach_absolute_time(void) {return now;}
static void absolutetime_to_nanoseconds(uint64_t ticks, uint64_t *ns) {*ns=ticks;}
static int test_printf(const char *fmt, ...)
{
    if (!strcmp(fmt, "CEPHEUS SOC: %s delay_s=%u uptime_ms=%llu\n")) {
        va_list ap; va_start(ap, fmt);
        const char *phase=va_arg(ap, const char *);
        assert(va_arg(ap, unsigned)==240);
        assert(va_arg(ap, unsigned long long)==now/NSEC_PER_MSEC);
        va_end(ap);
        assert(!strcmp(phase, marks==0 ? "scheduled" : marks==1 ? "begin" : "baseline-end"));
        if (marks==0) assert(!waits && !dumps);
        if (marks==1) assert(waits==1 && first_wait==240000 && !dumps);
        if (marks==2) assert(last_sample==25 && samples==26);
        ++marks;
    }
    return 0;
}
#define printf test_printf
static void swait(unsigned ms) {if (!waits) first_wait=ms; ++waits; now+=(uint64_t)ms*NSEC_PER_MSEC;}
static void require_begin(void) {assert(waits && first_wait==expected_delay*1000); assert(expected_delay!=240 || marks==2);}
static void osm_dump(int *bank) {require_begin(); assert(bank==&banks[dumps]); ++dumps;}
static void gpu_probe(void) {require_begin(); assert(dumps==4); ++gpu;}
static void gpu_fuse_probe(void) {++optional;}
static void gpu_gx_probe(void) {++optional;}
static void cepheus_gpu_arc_probe(void) {++optional;}
static void cepheus_gpu_memory_selftest(void) {++optional;}
static void cepheus_gmu_workspace_selftest(void) {++optional;}
static int cepheus_zap_memory_selftest(void) {assert(first_wait==90000 && waits==1);++zap_tests;return 0;}
static void osm_sample(unsigned n) {require_begin(); assert(n==samples); ++samples; last_sample=n;}
static int cpu_temperature(int *t) {require_begin(); *t=35000; return 1;}
static vm_offset_t ml_io_map(uintptr_t pa, unsigned size) {require_begin(); assert(pa==0x0c265000 && size==0x1000); ++maps; return 1;}
static unsigned sr(vm_offset_t base, unsigned offset) {(void)base; (void)offset; require_begin(); ++reads; return 0;}
static int ceph_tsens_mc(unsigned value, int *t) {(void)value; *t=35000; return 1;}
static void cpu_interactive(void) {++optional;}
static void osm_trial_start(void) {require_begin();}
static void osm_trial_restore(void) {require_begin();}
static void clock_interval_to_deadline(unsigned n, uint64_t unit, uint64_t *deadline) {*deadline=now+n*unit;}
static void soc_scm_probe(void) {int v=0; PE_parse_boot_argn("cephscm", &v, sizeof(v)); if (v==1) ++scm;}
static int current_thread(void) {return 7;}
static int thread_terminate(int t) {assert(t==7); ++terminated; return 0;}
static void thread_block(int c) {assert(c==0 && terminated==1); ++blocked;}
'''
suffix = r'''
static void reset(void)
{
    waits=first_wait=dumps=gpu=samples=maps=reads=optional=scm=terminated=blocked=marks=last_sample=0;
    zap_tests=0;
    now=17000*NSEC_PER_MSEC;
}
static void rejected(void)
{
    reset(); soc_thread(NULL, 0);
    assert(!waits && !dumps && !gpu && !samples && !maps && !reads && !optional && !scm && !marks && !zap_tests);
    assert(terminated==1 && blocked==1);
}
int main(void)
{
    unsigned seconds;
    delay_present=0; expected_delay=45;
    assert(soc_probe_delay(&seconds) && seconds==45);
    reset(); soc_thread(NULL, 0);
    assert(first_wait==45000 && dumps==4 && gpu==1 && samples==26 && maps==1 && !optional && !scm && !marks);
    assert(terminated==1 && blocked==1);
    delay_present=1; delay_value=240; expected_delay=240;
    assert(soc_probe_delay(&seconds) && seconds==240);
    reset(); soc_thread(NULL, 0);
    assert(first_wait==240000 && dumps==4 && gpu==1 && samples==26 && maps==1 && !optional && !scm && marks==3);
    assert(terminated==1 && blocked==1);
    const int invalid[] = {-1, 0, 1, 45, 60, 239, 241, 2147483647};
    for (unsigned n=0; n<sizeof(invalid)/sizeof(invalid[0]); ++n) {delay_value=invalid[n]; rejected();}
    delay_value=240;
    for (soc_value=0; soc_value<=2; soc_value+=2) rejected();
    soc_value=1;
    for (conflict=0; conflict<6; ++conflict)
        for (conflict_value=0; conflict_value<=2; ++conflict_value) rejected();
    conflict=-1;delay_present=0;soc_present=0;zap_present=1;zap_value=1;
    reset();soc_thread(NULL,0);
    assert(zap_tests==1 && waits==1 && terminated==1 && blocked==1);
    assert(!dumps && !gpu && !samples && !maps && !reads && !optional && !scm && !marks);
    for(int invalid=-1;invalid<=2;++invalid)if(invalid!=1){zap_value=invalid;rejected();}
    zap_value=1;
    for(soc_value=0;soc_value<=2;++soc_value){soc_present=1;rejected();}soc_present=0;
    delay_present=1;rejected();delay_present=0;
    for(conflict=0;conflict<6;++conflict)
        for(conflict_value=0;conflict_value<=2;++conflict_value)rejected();
    puts("Production SoC delay guard/thread PASS: default45, fixed240, phase order, rejected profiles do no probes (host stubs only)");
    puts("Production ZAP hook PASS: fixed90 single memory preflight, no SoC/SCM reads, all mixed/invalid profiles refused");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    source = path / 'delay.c'
    source.write_text(prefix + helpers + thread + suffix)
    exe = path / 'delay'
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=undefined', str(source), '-o', str(exe)], check=True)
    print(subprocess.check_output([str(exe)], text=True).strip())
