#define _DEFAULT_SOURCE 1
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "cepheus_gx_smmu_native.h"
#include "cepheus_gx_core_native.h"
#include "cepheus_smmu_mmio.h"
/* Fast access-boundary fixture. Upstream HFI/OOB/GX and pool are synthetic;
 * a separate mixed C/C++ chain exercises their actual implementations. */
static struct {
    unsigned attempted,acquired,failed,sp_clock_off;
    struct ceph_gx_native_result result;
    struct {uint64_t rpmh_token;} owner;
    volatile uint32_t *gpu;
} ceph_gx_native;
static struct {
    struct {volatile uint8_t *base;} gmu,smmu;
    struct {struct {volatile uint8_t *base;} gcc,cc;} power;
} ceph_hfi_native;
static unsigned ceph_gpll0_lock,checks,expire,maps,begins,ends,kept,context;
static unsigned pool_state,pins,publishes,finishes,finish_result,finish_error,corrupt;
static unsigned map_fail,map_bad,begin_fail,final_loss,mutation,reentry,upstream_loss;
static unsigned frozen,reverse,late,timeout,delays,ready_after;
static uint64_t ticks;
static volatile uint8_t *smmu;
static uint8_t *backing[CE_CP_BUFFERS];
static struct ceph_gx_smmu_native_owner owner;
static struct ceph_gx_smmu_plan plan;
static struct ceph_cp_publication publication;
static int ceph_scm_info_context(void *unused){(void)unused;return context?-1:0;}
static int ceph_gx_native_record(void){return upstream_loss?-1:0;}
static int ceph_gx_native_window(void){return upstream_loss?-1:0;}
static int cepheus_gpu_rpmh_check(uint64_t token){return token==1?0:-1;}
static uintptr_t ml_io_map(uint64_t pa,size_t bytes)
{
    assert(pa==0x2ca0000 && bytes==0x10000 && ceph_gpll0_lock);++maps;
    if(map_fail)return 0;
    if(map_bad==1)return UINTPTR_MAX-7;
    if(map_bad==2)return (uintptr_t)smmu+1;
    return (uintptr_t)smmu;
}
static uint64_t mach_absolute_time(void)
{if(reverse)ticks=0;else if(!frozen)ticks+=1000;return ticks;}
static void absolutetime_to_nanoseconds(uint64_t value,uint64_t *out){*out=value;}
static void delay(unsigned us)
{
    assert(us==1);++delays;if(!frozen)ticks+=late?100001:1000;
    if(!timeout && delays>=ready_after)*(volatile uint32_t *)(smmu+0x97f4)=0;
}
int ceph_memory_cp_validate(const struct ceph_cp_publication *p)
{return p->memory.group==257 && pool_state && pool_state!=5 && !corrupt?0:-1;}
int ceph_memory_cp_acquire(const struct ceph_cp_publication *p,struct ceph_cp_memory_view *out)
{
    if(ceph_memory_cp_validate(p) || pool_state!=1)return -1;
    out->publication=*p;
    for(unsigned i=0;i<CE_CP_BUFFERS;++i)out->views[i]=(struct ceph_gpu_memory_view){backing[i],0x110000000ULL+0x20000*i,4096};
    pool_state=2;++pins;return 0;
}
int ceph_memory_cp_publish(const struct ceph_cp_publication *p)
{if(ceph_memory_cp_validate(p) || pool_state!=2)return -1;pool_state=3;++publishes;return 0;}
int ceph_memory_cp_finish(const struct ceph_cp_publication *p,int result)
{
    assert(p->memory.group==257);++finishes;finish_result=(unsigned)result;
    if(finish_error)return -1;
    if(result==CE_MEMORY_CP_UNCERTAIN){pool_state=5;return 0;}
    if(ceph_memory_cp_validate(p))return -1;
    if(result==CE_MEMORY_CP_REFUSED && pool_state==2){pool_state=1;return 0;}
    if(result==CE_MEMORY_CP_INSTALLED && pool_state==3){pool_state=4;return 0;}
    return -1;
}
#include "cepheus_gx_smmu_native_private.h"
static int begin(void *ctx,const struct ceph_gx_smmu_plan *p,const struct ceph_cp_publication *pub)
{assert(ctx==(void *)smmu && p->context.ttbr0==pub->memory.ttbr0 && ceph_gpll0_lock);++begins;return begin_fail?-1:0;}
static int validate(void *ctx)
{
    assert(ctx==(void *)smmu && ceph_gpll0_lock);++checks;
    if(expire && checks>=expire){assert(!mprotect((void *)smmu,0x10000,PROT_NONE));return -1;}
    if(mutation){mutation=0;owner.validate=NULL;plan.context.smr=0;publication.memory.group=0;}
    if(reentry) {
        reentry=0;struct ceph_smmu_install out={CE_SM_NEW,123,456};
        assert(cepheus_gpu_gx_smmu_install_native(&owner,&plan,&publication,&out)<0 && out.effects==123);
    }
    return 0;
}
static void finish(void *ctx,unsigned effects,int keep)
{
    assert(ctx==(void *)smmu && ceph_gpll0_lock && effects==ceph_gx_smmu_native.txn.effects);
    ++ends;kept=(unsigned)keep;if(final_loss)expire=checks+1;
}
static void reset(void)
{
    assert(!mprotect((void *)smmu,0x10000,PROT_READ|PROT_WRITE));memset((void *)smmu,0,0x10000);
    memset(&ceph_gx_smmu_native,0,sizeof(ceph_gx_smmu_native));
    ceph_gpll0_lock=checks=expire=maps=begins=ends=kept=context=0;
    pins=publishes=finishes=finish_result=finish_error=corrupt=0;
    map_fail=map_bad=begin_fail=final_loss=mutation=reentry=upstream_loss=0;
    frozen=reverse=late=timeout=delays=ready_after=0;ticks=10000;pool_state=1;
    ceph_gx_native.attempted=ceph_gx_native.acquired=1;ceph_gx_native.failed=ceph_gx_native.sp_clock_off=0;
    ceph_gx_native.result=(struct ceph_gx_native_result){.configured=1,.setup=1};ceph_gx_native.owner.rpmh_token=1;
    owner=(struct ceph_gx_smmu_native_owner){(void *)smmu,begin,validate,finish,1};
    struct ceph_smmu_geometry g;assert(!ceph_smmu_geometry((1u<<30)|(1u<<27)|(11u<<9)|16,(2u<<28)|8,0x1222,0x10000,&g));
    struct ceph_smmu_stream streams[16]={0};for(unsigned i=0;i<16;++i)streams[i].s2cr=2u<<16;
    assert(!ceph_gx_smmu_plan(streams,16,&g,0,1,2,0x110000000ULL,0,&plan));
    publication=(struct ceph_cp_publication){.memory={.group=257,.ttbr0=plan.context.ttbr0,
        .ttbr1=plan.context.ttbr1,.smr=plan.context.smr,.cb=1,.slot=0,.asid=2}};
    *(volatile uint32_t *)(smmu+0x20)=(1u<<30)|(1u<<27)|(11u<<9)|16;
    *(volatile uint32_t *)(smmu+0x24)=(2u<<28)|8;*(volatile uint32_t *)(smmu+0x28)=0x1222;
    for(unsigned i=0;i<16;++i)*(volatile uint32_t *)(smmu+0xc00+4*i)=2u<<16;
}
static int run(struct ceph_smmu_install *out)
{return cepheus_gpu_gx_smmu_install_native(&owner,&plan,&publication,out);}
int main(void)
{
    smmu=mmap(NULL,0x10000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(smmu!=MAP_FAILED);
    for(unsigned i=0;i<CE_CP_BUFFERS;++i) {
        backing[i]=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(backing[i]!=MAP_FAILED);
    }
    struct ceph_smmu_install out;
    reset();assert(!run(&out) && out.state==CE_SM_ENABLED && out.effects==15 && pool_state==4);
    assert(maps==1 && begins==1 && ends==1 && kept && pins==1 && publishes==1 && finishes==1);
    assert(*(volatile uint32_t *)(smmu+0x800)==0x84010000 &&
        *(volatile uint64_t *)(smmu+0x9020)==plan.context.ttbr0 && *(volatile uint32_t *)(smmu+0x9030)==0x802019);
    unsigned boundaries=checks;
    for(unsigned n=1;n<=boundaries;++n) {
        reset();expire=n;out=(struct ceph_smmu_install){CE_SM_NEW,123,456};
        assert(run(&out)<0 && checks==n && ends==1 && kept && !ceph_gpll0_lock);
        if(ceph_gx_smmu_native.txn.effects)assert(pool_state==5 && out.state==CE_SM_QUARANTINED);
        unsigned before=checks;assert(run(&out)<0 && checks==before && ends==1);
    }
    reset();mutation=reentry=1;assert(!run(&out) && !owner.validate && pool_state==4);
    reset();final_loss=1;assert(run(&out)<0 && out.state==CE_SM_QUARANTINED && pool_state==5);
    reset();map_fail=1;assert(run(&out)<0 && !out.effects && pool_state==1 && kept);
    for(unsigned n=1;n<=2;++n){reset();map_bad=n;assert(run(&out)<0 && !out.effects && pool_state==1);}
    reset();begin_fail=1;assert(run(&out)<0 && begins==1 && !ends && !maps && !pins);
    reset();upstream_loss=1;out.error=123;assert(run(&out)<0 && out.error==123 && !begins);
    reset();owner.rpmh_token=0;assert(run(&out)<0 && !begins);
    reset();context=1;assert(run(&out)<0 && !begins);
    reset();assert(run((struct ceph_smmu_install *)backing[0])<0 && pool_state==1 && !maps);
    reset();assert(run((struct ceph_smmu_install *)(smmu+0x800))<0 && pool_state==1 && !out.effects);
    reset();*(volatile uint32_t *)(smmu+0x800)=0x84000000;assert(run(&out)<0 && !out.effects && pool_state==1);
    reset();*(volatile uint32_t *)(smmu+0x9058)=1;assert(run(&out)<0 && !out.effects && pool_state==1);
    reset();*(volatile uint32_t *)(smmu+0x97f4)=1;ready_after=3;
    assert(!run(&out) && delays==3 && out.effects==15);
    for(unsigned n=0;n<4;++n) {
        reset();*(volatile uint32_t *)(smmu+0x97f4)=1;
        if(n==0)frozen=1;
        if(n==1)reverse=1;
        if(n==2)late=1;
        if(n==3)timeout=1;
        assert(run(&out)<0 && out.state==CE_SM_QUARANTINED && pool_state==5);
    }
    reset();finish_error=1;assert(run(&out)<0 && out.state==CE_SM_QUARANTINED && ceph_gx_smmu_native.pinned);
    reset();struct ceph_smmu_mmio m={.base=smmu,.mapped_bytes=0x10000,.plan=plan.context,.actlr=0x303};
    uint64_t value=123;assert(ceph_smmu_mmio_read(&m,CE_SM_TTBR0,&value)<0 && value==123);
    struct ceph_smmu_install legacy={0};struct ceph_smmu_ops ops={0};
    assert(ceph_smmu_install_context(&legacy,&ops,&plan.context,0x303)<0 && !legacy.effects);
    for(unsigned i=0;i<CE_CP_BUFFERS;++i)assert(!munmap(backing[i],4096));
    assert(!munmap((void *)smmu,0x10000));
    printf("Native GX SMMU: %u PROT_NONE owner boundaries, SID0/39VA,15 effects, CP pin/retention and legacy refusal PASS (synthetic upstream/pool)\n",boundaries);
    return 0;
}
