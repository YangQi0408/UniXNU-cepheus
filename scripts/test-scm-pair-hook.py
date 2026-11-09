#!/usr/bin/env python3
"""Run the actual XNU pair helper against DT/query substitutes, no real SMC."""
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'xnu/cepheus_soc.c').read_text()
helper='static bool soc_scm_pair(void)'+source.split('static bool soc_scm_pair(void)',1)[1].split('static bool soc_zap_memory',1)[0]
prefix=r'''
#include <stdbool.h>
#define main fixture_main
#include "test_scm_handoff.c"
#undef main
typedef void *DTEntry;
#define kSuccess 0
static const char *line;
static unsigned queries,waits,lookups,bytes,missing;
static unsigned char raw[CE_SCM_HANDOFF_BYTES+1];
static char *PE_boot_args(void) {return (char *)line;}
static void swait(unsigned ms) {assert(ms==90000);++waits;}
static int SecureDTLookupEntry(DTEntry parent,const char *path,DTEntry *entry)
{assert(parent==NULL && !strcmp(path,"/chosen"));++lookups;*entry=(void *)1;return missing?1:kSuccess;}
static int SecureDTGetProperty(DTEntry entry,const char *name,const void **out,unsigned *size)
{assert(entry==(void *)1 && !strcmp(name,CE_SCM_HANDOFF_PROPERTY));*out=raw+1;*size=bytes;return kSuccess;}
static int cepheus_gpu_scm_probe_once(struct ceph_scm_discovery *out)
{++queries;*out=(struct ceph_scm_discovery){.calls=7,.terminal=1,.supported=0,.error=-1};return -1;}
'''
suffix=r'''
int main(void) {
    capture();memcpy(raw+1,&h,sizeof(h));bytes=sizeof(h);
    line="sweetusb=2";assert(!soc_scm_pair() && !queries && !waits && !lookups);
    const char *bad[]={"cephscmpair=0","cephscmpair=1 cephsoc=0","cephscmpair=1 cephzapmem=1",
        "cephscmpair=1 cephscmpair=1","cephscmpair=1 cephfoo=0"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        line=bad[i];assert(soc_scm_pair() && !queries && !waits && !lookups);
    }
    line="sweetusb=2 cephscmpair=1";
    missing=1;assert(soc_scm_pair() && !queries);missing=0;
    bytes--;assert(soc_scm_pair() && !queries);bytes++;
    raw[1]^=1;assert(soc_scm_pair() && !queries);raw[1]^=1;
    assert(soc_scm_pair() && queries==1 && waits==4);
    supported=1;capture();memcpy(raw+1,&h,sizeof(h));
    assert(soc_scm_pair() && queries==2); /* UEFI true never changes XNU false. */
    puts("XNU pair hook: opt-in, missing/unaligned/corrupt DT, fixed wait and independent XNU verdict PASS (RAM only)");
}
'''
with tempfile.TemporaryDirectory() as temp:
    root=Path(temp);c=root/'hook.c';c.write_text(prefix+helper+suffix)
    exe=root/'hook'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-I',str(ROOT/'xnu'),str(c),'-o',str(exe)],check=True)
    print(subprocess.check_output([str(exe)],text=True).splitlines()[-1])
