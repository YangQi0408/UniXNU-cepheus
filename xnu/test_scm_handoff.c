#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cepheus_scm_handoff.h"

static struct ceph_scm_handoff h;
static unsigned supported, interruptions, fallback, busy;
static int invoke(void *ctx,struct ceph_scm_regs *r)
{
    (void)ctx;
    struct ceph_scm_handoff_call *call=&h.calls[h.count++];
    call->in=*r;call->current_el=4;call->mpidr=0x80000000;call->ticks=h.count;
    if(interruptions) {--interruptions;r->x[0]=1;r->x[6]=0x1234;}
    else if(busy){r->x[0]=(uint64_t)-12;r->x[1]=0;}
    else if(fallback && r->x[0]==0xc2000601) {r->x[0]=(uint64_t)-1;r->x[1]=0;}
    else {unsigned pas=(r->x[0]&0x3fffffff)==0x02000207;r->x[0]=0;r->x[1]=pas?supported:1;}
    call->out=*r;call->complete=1;return 0;
}
static void capture(void)
{
    memset(&h,0,sizeof(h));
    h.magic=CE_SCM_HANDOFF_MAGIC;h.version=1;h.bytes=sizeof(h);h.current_el=4;
    h.ebs_attempts=1;h.ebs_done=h.complete=1;
    struct ceph_scm_discovery result={0};
    struct ceph_scm_discovery_ops ops={0,ceph_scm_handoff_lease,invoke};
    h.error=ceph_scm_discover_pas13(&result,&ops);
    h.convention=result.convention;h.available_mask=result.available_mask;
    h.supported=result.supported;h.terminal=result.terminal;
    h.checksum=ceph_scm_handoff_checksum(&h);
    assert(!ceph_scm_handoff_valid(&h));
}
int main(void)
{
    const char *bad[]={"cephscmpair=0","cephscmpair=2","cephscmpair=01","cephscmpair=1x",
        "cephscmpair","cephscmpair=1 cephscmpair=1","cephscmpair=1 cephsoc=0",
        "cephzapmem=1 cephscmpair=1","cephscmpair=1 cephunknown=1","cephscmpair-extra=1"};
    assert(ceph_scm_pair_profile(NULL)==0);
    assert(ceph_scm_pair_profile("sweetusb=2 cephsoc=1")==0);
    assert(ceph_scm_pair_profile("cephscmpair=1")==1);
    assert(ceph_scm_pair_profile("  sweetusb=2\tcephscmpair=1 \t")==1);
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i)assert(ceph_scm_pair_profile(bad[i])==-1);
    for(supported=0;supported<=1;++supported) {
        capture();assert(h.count==7);
        struct ceph_scm_handoff good=h;
        for(unsigned i=0;i<sizeof(h);++i) {
            ((unsigned char *)&h)[i]^=1;
            assert(ceph_scm_handoff_valid(&h));h=good;
        }
        h.calls[2].in.x[2]=0x02000205;h.checksum=ceph_scm_handoff_checksum(&h);
        assert(ceph_scm_handoff_valid(&h));h=good;
        h.supported^=1;h.checksum=ceph_scm_handoff_checksum(&h);
        assert(ceph_scm_handoff_valid(&h));h=good;
        h.calls[15].out.x[0]=1;h.checksum=ceph_scm_handoff_checksum(&h);
        assert(ceph_scm_handoff_valid(&h));
    }
    supported=0;fallback=1;capture();assert(h.count==8 && h.convention==CE_SCM_ARMV8_32);
    fallback=0;interruptions=1;capture();assert(h.count==8);
    interruptions=16;capture();assert(h.count==16 && h.error==-1);
    busy=1;capture();assert(h.count==1 && h.error==-1);
    puts("SCM pair profile, transcript corruption, policy replay, fallback/interruption/busy: PASS (RAM only)");
    return 0;
}
