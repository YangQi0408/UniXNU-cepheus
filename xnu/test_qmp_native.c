#define _DEFAULT_SOURCE 1
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "cepheus_qmp_native.h"
#include "../tests/gpll0-scope-fixture.h"
static volatile uint32_t *ram,*irq;
static unsigned ceph_gpll0_lock,checks,expire,maps,fail_map,begins,finishes,kept;
static unsigned stores,notifies,context,begin_fail,mutate,reenter,mode;
static unsigned time_fault,injected,inject_after,observed_stores;
static uint64_t ticks;
static int ceph_scm_info_context(void *unused){(void)unused;return context?-1:0;}
static uintptr_t ml_io_map(uint64_t pa,size_t bytes)
{
    assert(maps<2 && pa==(maps?0x17c0000c:0xc300000) && bytes==(maps?4u:4096u));
    ++maps;return maps==fail_map?0:(uintptr_t)(maps==1?ram:irq);
}
static uint64_t mach_absolute_time(void){return ticks;}
static void absolutetime_to_nanoseconds(uint64_t value,uint64_t *out){*out=value;}
static void delay(unsigned us)
{
    assert(us==500);
    if(time_fault==1)return;
    if(time_fault==2)ticks=0;else ticks+=time_fault==3?10000001:500000;
    if(mode==1 || !*irq)return;
    *irq=0;
    if(ram[9]==CE_QMP_UP)ram[10]=CE_QMP_UP;
    if(ram[11]==CE_QMP_UP){ram[5]=CE_QMP_UP;if(ram[6]==CE_QMP_UP)ram[12]=CE_QMP_UP;}
    if(ram[32]==32 && mode!=2)ram[32]=0;
    if(mode==3)ram[14]+=4;
}
#include "cepheus_qmp_native_private.h"
static struct ceph_qmp_native_owner owner;
static int begin(void *ctx){assert(ctx==(void *)ram && ceph_gpll0_lock);++begins;return begin_fail?-1:0;}
static void revoke(void)
{assert(!mprotect((void *)ram,4096,PROT_NONE));assert(!mprotect((void *)irq,4096,PROT_NONE));}
static int validate(void *ctx)
{
    assert(ctx==(void *)ram && ceph_gpll0_lock);++checks;
    if(expire && checks>=expire){revoke();return -1;}
    if(mutate){mutate=0;owner.begin=NULL;owner.validate=NULL;owner.finish=NULL;owner.rpmh_token=0;}
    if(reenter) {
        reenter=0;struct ceph_qmp_native_result out={.error=123};
        assert(cepheus_gpu_qmp_acd_native(&owner,&out)<0 && out.error==123);
    }
    observed_stores=ceph_qmp_native.result.connection.writes+ceph_qmp_native.mmio.writes;
    if(inject_after && observed_stores>=inject_after && !injected) {
        injected=1;ram[0]^=1;
    }
    return 0;
}
static void finish(void *ctx,unsigned writes,unsigned notifications,int keep)
{
    assert(ctx==(void *)ram && ceph_gpll0_lock && ceph_qmp_native.finished && !finishes);
    ++finishes;stores=writes;notifies=notifications;kept=(unsigned)keep;
}
static void reset(void)
{
    assert(!mprotect((void *)ram,4096,PROT_READ|PROT_WRITE));
    assert(!mprotect((void *)irq,4096,PROT_READ|PROT_WRITE));
    memset((void *)ram,0,4096);*irq=0;
    memset(&ceph_qmp_native,0,sizeof(ceph_qmp_native));gpll0_scope_reset();
    ceph_gpll0_lock=checks=expire=maps=fail_map=begins=finishes=kept=0;
    stores=notifies=context=begin_fail=mutate=reenter=mode=0;ticks=1000000;
    time_fault=injected=inject_after=observed_stores=0;
    ram[0]=0x4d41494c;ram[1]=1;
    for(unsigned c=0;c<2;++c)for(unsigned i=0;i<4;++i)ram[3+6*c+i]=CE_QMP_DOWN;
    ram[3]=CE_QMP_UP;ram[7]=64;ram[8]=64;ram[13]=128;ram[14]=128;
    owner=(struct ceph_qmp_native_owner){(void *)ram,begin,validate,finish,1};
}
static int run(struct ceph_qmp_native_result *out){return cepheus_gpu_qmp_acd_native(&owner,out);}
int main(void)
{
    ram=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    irq=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(ram!=MAP_FAILED && irq!=MAP_FAILED);
    struct ceph_qmp_native_result out;
    reset();assert(!run(&out) && !out.error && out.connection.connected && out.acd.consumed);
    assert(finishes==1 && kept && stores==13 && notifies==4 && scope_idle_retained);
    assert(!memcmp((const void *)(ram+33),"{class: gpu, res: acd, value: 1}",32));
    unsigned boundaries=checks,scope_boundaries=scope_checks;
    out.error=123;assert(run(&out)<0 && out.error==123 && finishes==1);
    for(unsigned n=1;n<=boundaries;++n) {
        reset();expire=n;assert(run(&out)<0 && !out.acd.consumed && checks==n && finishes==1);
        assert(ceph_qmp_native.failed && (!stores || kept));
        expire=0;unsigned before=checks;assert(run(&out)<0 && before==checks && finishes==1);
    }
    for(unsigned n=1;n<=scope_boundaries;++n) {
        reset();scope_expire=n;scope_revoke=revoke;
        assert(run(&out)<0 && scope_checks==n && !out.acd.consumed);
        assert(!begins || finishes==1);
        if(stores)assert(kept && scope_idle_retained);
    }
    for(unsigned n=1;n<=2;++n) {
        reset();fail_map=n;assert(run(&out)<0 && maps==n && finishes==1 && kept && !stores);
    }
    reset();mutate=1;reenter=1;assert(!run(&out) && finishes==1 && kept && !owner.finish);
    reset();begin_fail=1;assert(run(&out)<0 && begins==1 && !maps && !finishes);
    reset();owner.rpmh_token=0;assert(run(&out)<0 && !begins && !maps);
    reset();context=1;assert(run(&out)<0 && !begins && !maps && !ceph_qmp_native.attempted);
    reset();ceph_gpll0_lock=1;out.error=123;assert(run(&out)<0 && out.error==123 && !begins);
    const unsigned inherited[]={4,5,6,9,10,11,12};
    for(unsigned n=0;n<7;++n){reset();ram[inherited[n]]=CE_QMP_UP;
        assert(run(&out)<0 && !stores && !notifies && finishes==1 && kept);}
    for(unsigned n=1;n<=3;++n){reset();mode=n;assert(run(&out)<0 && finishes==1 && kept && stores);}
    for(unsigned n=1;n<=3;++n){reset();time_fault=n;
        assert(run(&out)<0 && finishes==1 && kept && stores && !out.acd.consumed);}
    for(unsigned n=1;n<=13;++n){reset();inject_after=n;
        assert(run(&out)<0 && injected && finishes==1 && kept && stores && !out.acd.consumed);}
    reset();ram[32]=4;assert(run(&out)<0 && !stores && !notifies && ram[32]==4);
    assert(!munmap((void *)ram,4096));assert(!munmap((void *)irq,4096));
    printf("Native QMP: %u owner/%u RPMh PROT_NONE boundaries, cold handshake/ACD, fixed maps/time, copied callbacks, inherited refusal and retention PASS (host peer model)\n",boundaries,scope_boundaries);
}
