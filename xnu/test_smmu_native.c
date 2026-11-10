#define _DEFAULT_SOURCE 1
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "cepheus_gpu_power_mmio.h"
#include "cepheus_gmu_mmio.h"
#include "cepheus_smmu_mmio.h"
#include "cepheus_gpu_memory_private.h"
#include "cepheus_smmu_native.h"
static volatile uint8_t *windows[4];
static const size_t sizes[]={0x90000,0x9000,0x30000,0x10000};
static unsigned ceph_gpll0_lock,checks,expire,maps,fail_map,begins,finished,kept,context;
static unsigned begin_error,mutate,reenter,pin_error,pins,unpins,published,publish_error,finish_error,pin_result,pin_invalid;
static unsigned delays,ready_after,timeout,frozen,reverse,late;
static unsigned revoke_effect;
static int effect_seen(void);
static uint64_t ticks;
static struct ceph_smmu_native_owner owner;
static struct ceph_smmu_plan plan;
static struct ceph_gpu_publication publication;
static int ceph_scm_info_context(void *unused){(void)unused;return context?-1:0;}
static volatile uint32_t *word(unsigned w,unsigned off)
{assert(w<4 && windows[w] && off<sizes[w]);return (volatile uint32_t *)(windows[w]+off);}
static volatile uint32_t *reg(enum ceph_gmu_reg r)
{uint32_t off;assert(!ceph_gmu_register_offset(r,&off));return word(2,off);}
static int validate(void *ctx)
{
    assert(ctx==windows);++checks;
    if(checks==expire || (revoke_effect && effect_seen())) {
        for(unsigned i=0;i<4;++i)assert(!mprotect((void *)windows[i],sizes[i],PROT_NONE));
        return -1;
    }
    if(mutate) {
        mutate=0;owner.begin=NULL;owner.validate=NULL;owner.finish=NULL;
        plan.ttbr0=0;publication.handles[0]=0;
    }
    if(reenter) {
        reenter=0;struct ceph_smmu_install out={CE_SM_NEW,123,456};
        assert(cepheus_gpu_smmu_install_native(&owner,&plan,&publication,&out)<0);
        assert(out.effects==123 && out.error==456 && ceph_gpll0_lock);
    }
    return 0;
}
static uintptr_t ml_io_map(uint64_t pa,size_t bytes)
{
    static const uint64_t expected[]={0x100000,0x2c90000,0x2c6a000,0x2ca0000};
    assert(maps<4 && pa==expected[maps] && bytes==sizes[maps]);++maps;
    return maps==fail_map?0:(uintptr_t)windows[maps-1];
}
static uint64_t mach_absolute_time(void)
{if(reverse)ticks=0;else if(!frozen)ticks+=1000;return ticks;}
static void absolutetime_to_nanoseconds(uint64_t value,uint64_t *out){*out=value;}
static void delay(unsigned us)
{
    assert(us==1);++delays;if(!frozen)ticks+=late?100001:1000;
    if(!timeout && delays>=ready_after)*word(3,0xb7f4)=0;
}
#if !defined(CEPHEUS_SMMU_NATIVE_POOL_TEST)
int ceph_memory_smmu_acquire(const struct ceph_gpu_publication *p,struct ceph_gmu_start_config *c)
{
    assert(p->ttbr0==0x4000110000000ULL && p->handles[0]==1);++pins;
    if(pin_error)return -1;
    *c=(struct ceph_gmu_start_config){0x06040001,0x60100000,20480,0x60200000,4096};return 0;
}
int ceph_memory_smmu_publish(const struct ceph_gpu_publication *p)
{assert(p->handles[0]==1);++published;return publish_error?-1:0;}
int ceph_memory_smmu_validate(const struct ceph_gpu_publication *p)
{assert(p->handles[0]==1);return pin_invalid?-1:0;}
int ceph_memory_smmu_finish(const struct ceph_gpu_publication *p,enum ceph_memory_smmu_result result)
{assert(p->handles[0]==1);++unpins;pin_result=(unsigned)result;return finish_error?-1:0;}
#endif
#include "cepheus_smmu_native_private.h"
static int effect_seen(void){return ceph_smmu_native.txn.effects!=0;}
static int begin(void *ctx,const struct ceph_smmu_plan *p,const struct ceph_gpu_publication *pub)
{
    assert(ctx==windows && ceph_gpll0_lock && p->ttbr0==pub->ttbr0 && pub->handles[0]);
    ++begins;return begin_error?-7:0;
}
static void finish(void *ctx,int keep)
{assert(ctx==windows && ceph_gpll0_lock && ceph_smmu_native.finished);++finished;kept=(unsigned)keep;}
static void reset(void)
{
    for(unsigned i=0;i<4;++i) {
        assert(!mprotect((void *)windows[i],sizes[i],PROT_READ|PROT_WRITE));
        memset((void *)windows[i],0,sizes[i]);
    }
    memset(&ceph_smmu_native,0,sizeof(ceph_smmu_native));
    ceph_gpll0_lock=checks=expire=maps=fail_map=begins=finished=kept=context=0;
    begin_error=mutate=reenter=pin_error=pins=unpins=published=publish_error=finish_error=pin_result=pin_invalid=0;
    delays=ready_after=timeout=frozen=reverse=late=revoke_effect=0;ticks=10000;
    owner=(struct ceph_smmu_native_owner){windows,begin,validate,finish};
    struct ceph_smmu_stream streams[16]={0};for(unsigned i=0;i<16;++i)streams[i].s2cr=2u<<16;
    assert(!ceph_smmu_gmu_plan(streams,16,2,3,8,5,4,0x110000000ULL,0,&plan));
    publication=(struct ceph_gpu_publication){.group=257,.handles={1,2,3,4,5,6},
        .ttbr0=plan.ttbr0,.ttbr1=plan.ttbr1,.smr=plan.smr,.cb=plan.cb,.slot=plan.slot,.asid=plan.asid};
    *word(0,0x71004)=1;*word(1,0x106c)=0x222000;*word(1,0x1540)=1u<<31;*word(1,0x100c)=0x222001;
    *word(0,CE_GPU_GCC_MEMNOC)=*word(0,CE_GPU_GCC_DVM)=1;
    *word(1,0x1098)=*word(1,0x109c)=*word(1,0x1078)=1;*reg(CE_GMU_SYSRESET)=1;
    *word(3,0x20)=(1u<<30)|(1u<<27)|(11u<<9)|16;*word(3,0x24)=(2u<<28)|8;*word(3,0x28)=0x1022;
    for(unsigned i=0;i<16;++i)*word(3,0xc00+4*i)=2u<<16;
}
static int run(struct ceph_smmu_install *out)
{return cepheus_gpu_smmu_install_native(&owner,&plan,&publication,out);}
#if defined(CEPHEUS_SMMU_NATIVE_POOL_TEST)
int test_smmu_native_pool(const struct ceph_smmu_plan *p,const struct ceph_gpu_publication *pub,unsigned mode)
{
    assert(mode<=5 && p && pub);
    for(unsigned i=0;i<4;++i) {
        windows[i]=mmap(NULL,sizes[i],PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        assert(windows[i]!=MAP_FAILED);
    }
    reset();plan=*p;publication=*pub;
    if(mode==1){*word(3,0xb7f4)=1;ready_after=3;}
    if(mode==2)expire=5;
    if(mode==3)revoke_effect=1;
    if(mode==4){*word(3,0xb7f4)=1;timeout=1;}
    struct ceph_smmu_install out;int e=run(&out);
    assert((e==0)==(mode<2));assert(finished==1 && !ceph_gpll0_lock);
    if(mode<2)assert(out.state==CE_SM_ENABLED && kept && !ceph_smmu_native.pinned);
    if(mode==2 || mode==5)assert(out.state==CE_SM_REFUSED && !kept && !out.effects);
    if(mode==3 || mode==4)assert(out.state==CE_SM_QUARANTINED && kept && out.effects);
    for(unsigned i=0;i<4;++i)assert(!munmap((void *)windows[i],sizes[i]));
    return e;
}
#else
int main(void)
{
    for(unsigned i=0;i<4;++i) {
        windows[i]=mmap(NULL,sizes[i],PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        assert(windows[i]!=MAP_FAILED);
    }
    struct ceph_smmu_install out;
    reset();assert(!run(&out) && out.state==CE_SM_ENABLED && out.effects==15);
    assert(maps==4 && pins==1 && unpins==1 && published==1 && pin_result==CE_MEMORY_SMMU_INSTALLED);
    assert(finished==1 && kept && !ceph_gpll0_lock && *reg(CE_GMU_SYSRESET)==1);
    assert(*word(3,0x808)==plan.smr && *word(3,0xc08)==plan.cb &&
        *(volatile uint64_t *)(windows[3]+0xb020)==plan.ttbr0);
    unsigned boundaries=checks,before=checks;
    out=(struct ceph_smmu_install){CE_SM_NEW,123,456};assert(run(&out)<0 && checks==before && out.effects==123);
    for(unsigned n=1;n<=boundaries;++n) {
        reset();expire=n;assert(run(&out)<0 && checks==n && finished==1 && !ceph_gpll0_lock);
        assert(kept==(out.effects!=0) && (out.state==CE_SM_QUARANTINED)==(out.effects!=0));
        assert(unpins==(ceph_smmu_native.pinned?0u:pins));
        if(unpins)assert(pin_result==(out.effects?CE_MEMORY_SMMU_UNCERTAIN:CE_MEMORY_SMMU_REFUSED));
        before=checks;assert(run(&out)<0 && checks==before && finished==1);
    }
    for(unsigned n=1;n<=4;++n) {
        reset();fail_map=n;assert(run(&out)<0 && maps==n && finished==1 && !kept && unpins==1);
        reset();fail_map=n;expire=2+2*n;
        assert(run(&out)<0 && maps==n && checks==expire && finished==1 && !kept && unpins==1);
    }
    reset();begin_error=1;assert(run(&out)==-7 && begins==1 && !finished && !maps && !pins);
    reset();pin_error=1;assert(run(&out)<0 && pins==1 && !unpins && finished==1 && !kept);
    reset();pin_invalid=1;assert(run(&out)<0 && !maps && unpins==1 && !kept);
    reset();publish_error=1;assert(run(&out)<0 && published==1 && pin_result==CE_MEMORY_SMMU_UNCERTAIN && kept);
    reset();finish_error=1;assert(run(&out)<0 && out.state==CE_SM_QUARANTINED && kept && ceph_smmu_native.pinned);
    reset();fail_map=finish_error=1;
    assert(run(&out)<0 && out.state==CE_SM_QUARANTINED && !out.effects && kept && ceph_smmu_native.pinned);
    reset();context=1;assert(run(&out)<0 && !begins && !ceph_smmu_native.attempted);
    reset();ceph_gpll0_lock=1;assert(run(&out)<0 && !begins && ceph_gpll0_lock);
    reset();owner.finish=NULL;assert(run(&out)<0 && !begins);
    reset();assert(cepheus_gpu_smmu_install_native(NULL,&plan,&publication,&out)<0);
    assert(cepheus_gpu_smmu_install_native(&owner,NULL,&publication,&out)<0);
    assert(cepheus_gpu_smmu_install_native(&owner,&plan,NULL,&out)<0);
    assert(cepheus_gpu_smmu_install_native(&owner,&plan,&publication,NULL)<0);
    reset();publication.cb++;assert(run(&out)<0 && !begins);
    reset();plan.smr=0x84000004;publication.smr=plan.smr;assert(run(&out)<0 && !begins);
    reset();mutate=1;assert(!run(&out) && finished==1 && !owner.finish && !plan.ttbr0);
    reset();reenter=1;assert(!run(&out) && finished==1);
    reset();*word(3,0x20)=UINT32_MAX;assert(run(&out)<0 && !kept && !out.effects);
    reset();*word(3,0x24)|=1u<<31;assert(run(&out)<0 && !kept);
    reset();*word(3,0x800)=0x84000005;assert(run(&out)<0 && !kept);
    reset();*word(3,0x800)=0x80000007;*word(3,0xc00)=3;assert(run(&out)<0 && !kept);
    reset();*word(3,0xb000)=1;assert(run(&out)<0 && !kept);
    reset();*word(3,0xb058)=1;assert(run(&out)<0 && !kept);
    reset();*reg(CE_GMU_SYSRESET)=0;assert(run(&out)<0 && !kept);
    reset();*word(0,0x71004)=0x80000000;
    assert(!mprotect((void *)windows[1],sizes[1],PROT_NONE));
    assert(!mprotect((void *)windows[2],sizes[2],PROT_NONE));assert(run(&out)<0 && !kept);
    reset();*word(1,0x1540)=0;
    assert(!mprotect((void *)windows[2],sizes[2],PROT_NONE));assert(run(&out)<0 && !kept);
    for(unsigned i=0;i<2;++i) {
        reset();*word(0,i?CE_GPU_GCC_DVM:CE_GPU_GCC_MEMNOC)=0x80000000;
        assert(!mprotect((void *)windows[3],sizes[3],PROT_NONE));assert(run(&out)<0 && !kept && !out.effects);
    }
    reset();*word(3,0xb7f4)=1;ready_after=3;assert(!run(&out) && delays==3 && kept);
    reset();*word(3,0xb7f4)=1;timeout=1;assert(run(&out)<0 && delays==1000 && kept);
    reset();*word(3,0xb7f4)=1;timeout=frozen=1;assert(run(&out)<0 && delays==1 && kept);
    reset();*word(3,0xb7f4)=1;timeout=reverse=1;assert(run(&out)<0 && delays==1 && kept);
    reset();*word(3,0xb7f4)=1;timeout=late=1;assert(run(&out)<0 && delays==1 && kept);
    for(unsigned i=0;i<4;++i)assert(!munmap((void *)windows[i],sizes[i]));
    printf("Native SMMU: %u PROT_NONE boundaries, fixed maps/live geometry/stream scan, 15 effects, pool pin/publication, scoped sync and uncertain retention PASS (host model)\n",boundaries);
}
#endif
