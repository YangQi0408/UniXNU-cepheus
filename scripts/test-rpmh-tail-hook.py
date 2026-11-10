#!/usr/bin/env python3
"""Execute the actual XNU tail helper and independently verify its raw log in RAM."""
import ctypes
import importlib.util
import os
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('tail_log',ROOT/'scripts/analyze-rpmh-tail-log.py')
log=importlib.util.module_from_spec(spec);spec.loader.exec_module(log)
source=(ROOT/'xnu/cepheus_soc.c').read_text()
assert '__attribute__((noinline))\nstatic bool soc_rpmh_tail(void)' in source
helper='static bool soc_rpmh_tail(void)'+source.split('static bool soc_rpmh_tail(void)',1)[1].split('static bool soc_scm_pair',1)[0]
for forbidden in ('gBS','cepheus_gpu_scm_probe_once','cepheus_gpu_arc_raise','cepheus_gpu_bcm_raise',
                  'cepheus_gpu_submit','ceph_power_start'):
    assert forbidden not in helper,forbidden
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "cepheus_rpmh_tail_handoff.h"
typedef void *DTEntry;
#define kSuccess 0
static const char *line;
static unsigned waits,lookups,probes,bytes,missing;
static unsigned char raw[CE_RPMH_TAIL_HANDOFF_BYTES+1];
static char *PE_boot_args(void) {return (char *)line;}
static void swait(unsigned ms) {assert(ms==90000);++waits;}
static int SecureDTLookupEntry(DTEntry parent,const char *path,DTEntry *entry)
{assert(!parent && !strcmp(path,"/chosen"));++lookups;*entry=(void *)1;return missing==1?1:kSuccess;}
static int SecureDTGetProperty(DTEntry entry,const char *name,const void **out,unsigned *size)
{assert(entry==(void *)1 && !strcmp(name,CE_RPMH_TAIL_HANDOFF_PROPERTY));*out=missing==3?NULL:raw+1;*size=bytes;return missing==2?1:kSuccess;}
static void gpu_probe(void) {++probes;}
'''
suffix=r'''
static struct ceph_rpmh_tail_handoff fixture(void) {
    struct ceph_rpmh_tail_handoff h={.magic=CE_RPMH_TAIL_HANDOFF_MAGIC,.version=1,.bytes=sizeof(h),.flags=15,.ebs_attempts=1};
    struct ceph_rpmh_tail *t=&h.record;
    *t=(struct ceph_rpmh_tail){.magic=CE_RPMH_TAIL_MAGIC,.version=1,.bytes=sizeof(*t),.complete=1,
        .boundaries=15,.reads=9,.allocation_serial=3,.begin_ticks=100,.end_ticks=110};
    struct ceph_rpmh_snapshot *s=&t->requests;
    *s=(struct ceph_rpmh_snapshot){.magic=CE_RPMH_SNAPSHOT_MAGIC,.version=1,.bytes=sizeof(*s),.complete=1,
        .identity_mask=7,.resource_count=2,.ebs_attempts=1,.ebs_done=1,.phase=2,.current_el=4,.begin_ticks=100,.end_ticks=110};
    for(unsigned i=0;i<2;++i) {
        s->resource[i].address=0x30000+4*i;s->resource[i].count=1;
        s->resource[i].drv[0].id=2;s->resource[i].drv[0].shadow_data=i+1;
        s->resource[i].drv[0].local[0]=(struct ceph_rpmh_vote){i+2,1,1};
    }
    return h;
}
static void seal(struct ceph_rpmh_tail_handoff *h) {
    if(h->flags&CE_TAIL_COPIED) {
        h->record.requests.checksum=ceph_rpmh_snapshot_checksum(&h->record.requests);
        h->record.checksum=ceph_rpmh_tail_checksum(&h->record);
    }
    h->checksum=ceph_rpmh_tail_handoff_checksum(h);
}
int main(int argc,char **argv) {
    assert(argc==2);struct ceph_rpmh_tail_handoff h=fixture();
    if(!strcmp(argv[1],"coreerror")) {
        h.record.error=h.record.requests.error=CE_RPMH_SNAP_CHANGED;
        memset(h.record.requests.resource,0,sizeof(h.record.requests.resource));
    } else if(!strcmp(argv[1],"binderror") || !strcmp(argv[1],"copyerror")) {
        h.flags=!strcmp(argv[1],"binderror")?9:11;
        h.bind_status=h.flags==9?0x800000000000000eULL:0;
        h.copy_status=0x8000000000000006ULL;memset(&h.record,0,sizeof(h.record));
    }
    seal(&h);assert(!ceph_rpmh_tail_handoff_valid(&h));memcpy(raw+1,&h,sizeof(h));bytes=sizeof(h);
    line="sweetusb=2 cephrpmhtail=1";
    if(!strcmp(argv[1],"guards")) {
        const char *bad[]={"cephrpmhtail=0","cephrpmhtail=1 cephsoc=0","cephrpmhtail=1 cephscmpair=1",
            "cephrpmhtail=1 cephrpmhtail=1","cephrpmhtail=1 cephfoo=0"};
        line="sweetusb=2";assert(!soc_rpmh_tail() && !waits && !lookups && !probes);
        for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {line=bad[i];assert(soc_rpmh_tail() && !waits && !lookups && !probes);}
        line="cephrpmhtail=1";
        for(missing=1;missing<=3;++missing)assert(soc_rpmh_tail() && !probes);
        missing=0;bytes--;assert(soc_rpmh_tail() && !probes);bytes++;
        for(unsigned i=0;i<sizeof(h);++i) {raw[i+1]^=1;assert(soc_rpmh_tail() && !probes);raw[i+1]^=1;}
        assert(waits==sizeof(h)+4);return 0;
    }
    assert(soc_rpmh_tail() && waits==1 && lookups==1 && probes==1);return 0;
}
'''


def main():
    with tempfile.TemporaryDirectory() as directory:
        temp=Path(directory);c=temp/'hook.c';c.write_text(prefix+helper+suffix)
        validator=temp/'validator.c'
        validator.write_text('#include <string.h>\n#include "cepheus_rpmh_tail_handoff.h"\n'
                             'int validate(const void *p) {struct ceph_rpmh_tail_handoff h;memcpy(&h,p,sizeof(h));return ceph_rpmh_tail_handoff_valid(&h);}\n')
        lib=temp/'validator.so'
        subprocess.run(['cc','-shared','-fPIC','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined',
                        '-I',str(ROOT/'firmware'),str(validator),'-o',str(lib)],check=True)
        validate=ctypes.CDLL(str(lib)).validate;validate.argtypes=[ctypes.c_void_p]
        for optimization in ('-O0','-O2'):
            exe=temp/'hook';subprocess.run(['cc',optimization,'-std=c11','-Wall','-Wextra','-Werror',
                '-fsanitize=address,undefined','-I',str(ROOT/'firmware'),str(c),'-o',str(exe)],check=True)
            env={**os.environ,'ASAN_OPTIONS':'detect_leaks='+('0' if os.uname().sysname=='Darwin' else '1')}
            subprocess.run([str(exe),'guards'],check=True,stdout=subprocess.DEVNULL,env=env)
            for mode in ('success','coreerror','binderror','copyerror'):
                text=subprocess.check_output([str(exe),mode],text=True,env=env)
                result=log.analyze(text);assert result['status']=='TAIL_COMPLETE',result
                assert not result['gpu_execution'] and not result['admission_changed']
                lines=text.splitlines();raw=b''.join(struct.pack('<8Q',*(int(w,16) for w in log.RAW.fullmatch(l)[2].split(':')))
                                                   for l in lines if log.RAW.fullmatch(l))[:1128]
                assert not validate(ctypes.create_string_buffer(raw))
                assert log.decode(raw)['flags']==result['flags']
                for i in range(len(raw)):
                    bad=bytearray(raw);bad[i]^=1
                    assert validate(ctypes.create_string_buffer(bytes(bad)))
                    try:log.decode(bytes(bad))
                    except ValueError:pass
                    else:raise AssertionError(('corrupt byte accepted',i))
                assert log.analyze('\n'.join(lines[1:]))['status']=='TAIL_COMPLETE'
                for i in range(1,len(lines)):
                    altered=lines[:i]+lines[i+1:]
                    assert log.analyze('\n'.join(altered))['status']!='TAIL_COMPLETE',i
                    altered=lines[:i]+[lines[i]]+lines[i:]
                    assert log.analyze('\n'.join(altered))['status']!='TAIL_COMPLETE',i
                altered=lines[:];altered[2],altered[3]=altered[3],altered[2]
                assert log.analyze('\n'.join(altered))['status']=='REJECTED'
                altered=lines[:];altered[1]=altered[1].replace('ebs=1','ebs=2')
                assert log.analyze('\n'.join(altered))['status']=='REJECTED'
                altered=lines[:];last=19;assert 'raw=1088' in altered[last]
                altered[last]=altered[last][:-1]+'1'
                assert log.analyze('\n'.join(altered))['status']=='REJECTED'
    print('Actual XNU tail hook / independent raw-envelope analyzer: O0/O2 ASan/UBSan, unaligned DT, 1128 byte guards, failure records and lost/duplicate/reordered/padded logs PASS (RAM only)')


if __name__=='__main__':main()
