#define _DEFAULT_SOURCE 1
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "cepheus_gpll0.h"

static uint32_t *regs;
static unsigned checks,expire,maps,kept,kept_writes,delayed,delay_calls;
static int ready,context_bad,mutation,applied,delay_error;
static int ceph_scm_info_context(void *p){(void)p;return context_bad;}
static uintptr_t ml_io_map(uint64_t pa,size_t bytes)
{assert(pa==0x100000 && bytes==0x90000);++maps;return (uintptr_t)regs;}
static void delay(unsigned us);
#include "cepheus_gpll0_private.h"

static int owner(void *p)
{
    assert(p==regs);++checks;
    if(expire && checks>=expire) {
        assert(!mprotect(regs,CE_GPLL0_BYTES,PROT_NONE));return -1;
    }
    if(mutation && ceph_gpll0_native.attempted_stores && !applied) {
        applied=1;
        if(mutation==2)regs[0x52000/4]^=1u<<4;
        if(mutation==3)regs[0]^=1u<<31;
    }
    if(mutation==1 && applied)regs[0x52000/4]&=~1u;
    return 0;
}
static void retained(void *p,unsigned stores,int ok)
{assert(p==regs);++kept;kept_writes=stores;ready=ok;}
static void delay(unsigned us)
{
    assert(us==1 || us==500);delayed+=us;++delay_calls;
    if(mutation==4 && us==500)regs[0x52004/4]&=~(1u<<16);
    if(mutation==5 && us==500)regs[0xc/4]^=1u<<8;
    if(mutation==6 && us==500)regs[0x52000/4]^=1u<<5;
    if(mutation==7 && us==500)regs[0x52004/4]^=1u<<5;
}
static int failing_delay(void *p,unsigned us){(void)p;(void)us;return delay_error;}
static void reset(void)
{
    assert(!mprotect(regs,CE_GPLL0_BYTES,PROT_READ|PROT_WRITE));
    memset(regs,0,CE_GPLL0_BYTES);
    ceph_gpll0_native=(struct ceph_gpll0){0};ceph_gpll0_lock=0;
    checks=expire=maps=kept=kept_writes=delayed=delay_calls=0;
    ready=context_bad=mutation=applied=delay_error=0;
    regs[0]=(1u<<20)|(1u<<30)|(1u<<31)|7u;
    regs[4/4]=31;regs[0x40/4]=0x4000;regs[0xc/4]=0x107;
    regs[0x30/4]=1u<<26;regs[0x38/4]=1;
    regs[0x52000/4]=0x400;regs[0x52004/4]=0x8080;
}
static struct ceph_gpll0_owner ops(void)
{return (struct ceph_gpll0_owner){regs,owner,retained};}
static int run(struct ceph_gpll0_snapshot *out)
{struct ceph_gpll0_owner o=ops();return cepheus_gpu_gpll0_hold(&o,out);}
int main(void)
{
    regs=mmap(NULL,CE_GPLL0_BYTES,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(regs!=MAP_FAILED);reset();
    uint32_t original[CE_GPLL0_BYTES/4];memcpy(original,regs,sizeof(original));
    struct ceph_gpll0_snapshot out={0};
    assert(!run(&out));unsigned total=checks;
    assert(out.configured_div_hz==300000000 && kept==1 && kept_writes==2 && ready);
    assert(delayed==500 && delay_calls==1 && maps==1 && ceph_gpll0_native.phase==CE_GP_HELD);
    for(unsigned i=0;i<CE_GPLL0_BYTES/4;++i) {
        uint32_t want=original[i];
        if(i==0x52000/4)want|=1;
        if(i==0x52004/4)want|=1u<<16;
        assert(regs[i]==want);
    }
    assert(run(&out)<0 && kept==1 && maps==1);
    assert(!cepheus_gpu_gpll0_check(&out));
    regs[0x52004/4]&=~(1u<<16);
    assert(cepheus_gpu_gpll0_check(&out)<0 && ceph_gpll0_native.phase==CE_GP_POISONED);
    for(unsigned n=1;n<=total;++n) {
        reset();expire=n;memset(&out,0xa5,sizeof(out));
        assert(run(&out)<0);
        assert(out.mode==0xa5a5a5a5 && out.configured_div_hz==UINT64_C(0xa5a5a5a5a5a5a5a5));
        assert(kept==(ceph_gpll0_native.attempted_stores!=0) && !ready);
        if(kept)assert(ceph_gpll0_native.phase==CE_GP_POISONED);
        assert(run(&out)<0);
    }
    reset();assert(!run(&out));unsigned start=checks;
    assert(!cepheus_gpu_gpll0_check(&out));unsigned check_count=checks-start;
    for(unsigned n=1;n<=check_count;++n) {
        reset();assert(!run(&out));expire=checks+n;
        assert(cepheus_gpu_gpll0_check(&out)<0 && ceph_gpll0_native.phase==CE_GP_POISONED);
        assert(kept==1 && ready);
    }
    for(int n=1;n<=7;++n) {
        reset();mutation=n;
        assert(run(&out)<0 && kept==1 && !ready && ceph_gpll0_native.phase==CE_GP_POISONED);
        assert(kept_writes==ceph_gpll0_native.attempted_stores);
        if(n==1)assert(delayed==100 && delay_calls==100);
    }
    const unsigned offs[]={0,4,0x40,0xc,0x30,0x38,0x52000,0x52004};
    for(unsigned n=0;n<sizeof(offs)/sizeof(offs[0]);++n) {
        reset();regs[offs[n]/4]=UINT32_MAX;assert(run(&out)<0 && !kept);
    }
    const struct {unsigned off,value;} invalid[]={
        {0,7},{0,(1u<<20)|(1u<<30)|7},{0,(1u<<20)|(1u<<31)|7},
        {0,(1u<<20)|(1u<<30)|(1u<<31)|7|(1u<<22)},
        {4,0},{4,105},{0x40,0x10000},{0xc,0x100},{0xc,0x203},
        {0x30,0},{0x38,0},{4,32}};
    for(unsigned n=0;n<sizeof(invalid)/sizeof(invalid[0]);++n) {
        reset();regs[invalid[n].off/4]=invalid[n].value;assert(run(&out)<0 && !kept);
    }
    reset();regs[0x52000/4]|=1;regs[0x52004/4]|=1u<<16;
    assert(!run(&out) && kept==1 && !kept_writes && delayed==500);
    reset();regs[0x52000/4]|=1;assert(!run(&out) && kept_writes==1);
    reset();regs[0x52004/4]|=1u<<16;assert(!run(&out) && kept_writes==1);
    reset();regs[4/4]=15;regs[0x40/4]=0xa000;regs[0xc/4]=7;
    assert(!run(&out) && out.configured_div_hz==300000000);
    reset();regs[4/4]=62;regs[0x40/4]=0x8000;regs[0xc/4]=0x307;
    assert(!run(&out) && out.configured_div_hz==300000000);
    reset();struct ceph_gpll0 local={.gcc=(volatile uint8_t *)regs,.pa=CE_GPLL0_PA,
        .bytes=CE_GPLL0_BYTES,.owner=ops(),.delay_us=failing_delay};
    delay_error=1;
    assert(ceph_gpll0_hold(&local,&out)<0 && local.phase==CE_GP_POISONED && kept==1 && !ready);
    reset();local=(struct ceph_gpll0){.gcc=(volatile uint8_t *)regs,.pa=CE_GPLL0_PA,
        .bytes=CE_GPLL0_BYTES,.owner=ops(),.delay_us=failing_delay};
    regs[0x52000/4]|=1;regs[0x52004/4]|=1u<<16;delay_error=1;
    assert(ceph_gpll0_hold(&local,&out)<0 && local.phase==CE_GP_REJECTED && !kept);
    reset();context_bad=1;assert(run(&out)<0 && !maps && !checks);
    reset();ceph_gpll0_lock=1;assert(run(&out)<0 && !maps && !checks);
    reset();struct ceph_gpll0_owner o=ops();o.validate=NULL;
    assert(cepheus_gpu_gpll0_hold(&o,&out)<0 && !maps);
    o=ops();o.retained=NULL;assert(cepheus_gpu_gpll0_hold(&o,&out)<0 && !maps);
    assert(cepheus_gpu_gpll0_hold(NULL,&out)<0 && run(NULL)<0 && !maps);
    reset();assert(!run(&out));struct ceph_gpll0 bad=ceph_gpll0_native;
    uint32_t v=123;
    bad.pa+=4;assert(ceph_gpll0_read(&bad,0,&v)<0 && v==123);
    bad=ceph_gpll0_native;--bad.bytes;assert(ceph_gpll0_read(&bad,0,&v)<0);
    bad=ceph_gpll0_native;++bad.gcc;assert(ceph_gpll0_read(&bad,0,&v)<0);
    bad=ceph_gpll0_native;bad.gcc=(volatile uint8_t *)(UINTPTR_MAX-3);
    assert(ceph_gpll0_read(&bad,0,&v)<0);
    assert(ceph_gpll0_read(&ceph_gpll0_native,0x52008,&v)<0);
    assert(ceph_gpll0_vote_set(&ceph_gpll0_native,0xc)<0);
    assert(!munmap(regs,CE_GPLL0_BYTES));
    printf("GPLL0 native adapter: actual volatile stores, %u hold/%u check lease faults with protected aperture, retention/retry gates PASS (host model; not measured)\n",total,check_count);
}
