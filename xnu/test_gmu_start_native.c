#define _DEFAULT_SOURCE 1
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "cepheus_gmu_mmio.h"
#include "cepheus_gpu_power_mmio.h"
#include "cepheus_gmu_start_native.h"
static volatile uint8_t *windows[3];
static const size_t sizes[]={0x90000,0x9000,0x30000};
static unsigned ceph_gpll0_lock,checks,expire,maps,fail_map,begins,finished,kept,context;
static unsigned begin_error,mutate,reenter,gmu_off,timeout,fail_fault,delays;
static unsigned frozen,reverse,late,cm3_delay,hfi_delay,cm3_ticks,hfi_ticks;
static uint64_t ticks;
static struct ceph_gmu_start_native_owner owner;
static struct ceph_gmu_start_config config;
static int ceph_scm_info_context(void *unused){(void)unused;return context?-1:0;}
static volatile uint32_t *word(unsigned w,unsigned offset)
{assert(w<3 && windows[w] && offset<sizes[w]);return (volatile uint32_t *)(windows[w]+offset);}
static volatile uint32_t *reg(enum ceph_gmu_reg r)
{uint32_t offset;assert(!ceph_gmu_register_offset(r,&offset));return word(2,offset);}
static void model(void)
{
    if(gmu_off)return;
    if(!*reg(CE_GMU_SYSRESET) && !timeout && cm3_ticks>=cm3_delay)
        *reg(CE_GMU_INIT_RESULT)=0xbabeface;
    if(*reg(CE_GMU_HFI_INIT) && !timeout && hfi_ticks>=hfi_delay)*reg(CE_GMU_HFI_STATUS)=1;
    if(!*reg(CE_GMU_SYSRESET) && fail_fault)*reg(CE_GMU_AO_STATUS)=1;
}
static int validate(void *ctx)
{
    assert(ctx==windows);++checks;
    if(checks==expire) {
        for(unsigned i=0;i<3;++i)assert(!mprotect((void *)windows[i],sizes[i],PROT_NONE));
        return -1;
    }
    if(mutate){mutate=0;owner.begin=NULL;owner.validate=NULL;owner.finish=NULL;config.chipid=0;}
    if(reenter) {
        reenter=0;struct ceph_gmu_start out={CE_GS_NEW,123,456,789};
        assert(cepheus_gmu_start_cold_native(&owner,&config,&out)<0);
        assert(out.effects==123 && out.error==456 && ceph_gpll0_lock);
    }
    model();return 0;
}
static uintptr_t ml_io_map(uint64_t pa,size_t bytes)
{
    static const uint64_t expected[]={0x100000,0x2c90000,0x2c6a000};
    assert(maps<3 && pa==expected[maps] && bytes==sizes[maps]);++maps;
    return maps==fail_map?0:(uintptr_t)windows[maps-1];
}
static uint64_t mach_absolute_time(void)
{
    if(reverse && ticks>=2000)ticks=0;
    else if(!frozen)ticks+=1000;
    return ticks;
}
static void absolutetime_to_nanoseconds(uint64_t value,uint64_t *out){*out=value;}
static void delay(unsigned us)
{
    assert(us==1);++delays;if(!frozen)ticks+=late?100001:1000;
    if(!*reg(CE_GMU_SYSRESET) && *reg(CE_GMU_INIT_RESULT)!=0xbabeface)++cm3_ticks;
    if(*reg(CE_GMU_HFI_INIT) && !*reg(CE_GMU_HFI_STATUS))++hfi_ticks;
    model();
}
#include "cepheus_gmu_start_native_private.h"
static int begin(void *ctx,const struct ceph_gmu_start_config *c)
{
    assert(ctx==windows && ceph_gpll0_lock && c->chipid==0x06040001);++begins;
    return begin_error?-7:0;
}
static void finish(void *ctx,int keep)
{
    assert(ctx==windows && ceph_gpll0_lock && ceph_cm3_native.finished);
    ++finished;kept=(unsigned)keep;
}
static void reset(void)
{
    for(unsigned i=0;i<3;++i) {
        assert(!mprotect((void *)windows[i],sizes[i],PROT_READ|PROT_WRITE));
        memset((void *)windows[i],0,sizes[i]);
    }
    memset(&ceph_cm3_native,0,sizeof(ceph_cm3_native));
    ceph_gpll0_lock=checks=expire=maps=fail_map=begins=finished=kept=context=0;
    begin_error=mutate=reenter=gmu_off=timeout=fail_fault=delays=0;
    frozen=reverse=late=cm3_delay=hfi_delay=cm3_ticks=hfi_ticks=0;ticks=0;
    owner=(struct ceph_gmu_start_native_owner){windows,begin,validate,finish};
    config=(struct ceph_gmu_start_config){0x06040001,0x60100000,20480,0x60200000,4096};
    *word(0,0x71004)=1;
    *word(1,0x106c)=0x222000;*word(1,0x1540)=1u<<31;*word(1,0x100c)=0x222001;
    *word(1,0x1098)=*word(1,0x109c)=*word(1,0x1078)=1;
    *reg(CE_GMU_SYSRESET)=1;*reg(CE_GMU_TCM_RETENTION)=1;
    *reg(CE_GMU_HFI_VERSION)=0x20000001;
}
static int run(struct ceph_gmu_start *out)
{return cepheus_gmu_start_cold_native(&owner,&config,out);}
int main(void)
{
    for(unsigned i=0;i<3;++i) {
        windows[i]=mmap(NULL,sizes[i],PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        assert(windows[i]!=MAP_FAILED);
    }
    struct ceph_gmu_start out;
    reset();assert(!run(&out) && out.state==CE_GS_HFI_READY && out.hfi_version==0x20000001);
    assert(maps==3 && finished==1 && kept && !ceph_gpll0_lock && out.effects==18);
    assert(*reg(CE_GMU_SYSRESET)==0 && *reg(CE_GMU_QTBL_ADDR)==0x60100000);
    unsigned boundaries=checks;
    out=(struct ceph_gmu_start){CE_GS_NEW,123,456,789};unsigned before=checks;
    assert(run(&out)<0 && checks==before && out.effects==123 && out.error==456 && finished==1);
    for(unsigned n=1;n<=boundaries;++n) {
        reset();expire=n;assert(run(&out)<0 && checks==n && finished==1 && !ceph_gpll0_lock);
        assert(kept==(out.effects!=0) && (out.state==CE_GS_QUARANTINED)==(out.effects!=0));
        before=checks;assert(run(&out)<0 && checks==before && finished==1);
    }
    reset();cm3_delay=3;hfi_delay=5;
    assert(!run(&out) && out.state==CE_GS_HFI_READY && out.effects==18);
    assert(delays==8 && cm3_ticks==3 && hfi_ticks==5 && finished==1 && kept);
    unsigned delayed_boundaries=checks;
    for(unsigned n=1;n<=delayed_boundaries;++n) {
        reset();cm3_delay=3;hfi_delay=5;expire=n;
        assert(run(&out)<0 && checks==n && finished==1 && !ceph_gpll0_lock);
        assert(kept==(out.effects!=0) && (out.state==CE_GS_QUARANTINED)==(out.effects!=0));
        before=checks;assert(run(&out)<0 && checks==before && finished==1);
    }
    reset();cm3_delay=9999;hfi_delay=9999;
    assert(!run(&out) && out.state==CE_GS_HFI_READY && delays==19998 && kept);
    reset();cm3_delay=10000;
    assert(run(&out)<0 && out.state==CE_GS_QUARANTINED && delays==10000 && kept);
    reset();hfi_delay=10000;
    assert(run(&out)<0 && out.state==CE_GS_QUARANTINED && delays==10000 && kept);
    for(unsigned n=1;n<=3;++n) {
        reset();fail_map=n;assert(run(&out)<0 && maps==n && checks==2*n && finished==1 && !kept);
        reset();fail_map=n;expire=2*n;assert(run(&out)<0 && maps==n && checks==expire && !kept);
    }
    reset();begin_error=1;assert(run(&out)==-7 && begins==1 && !finished && !maps);
    reset();context=1;assert(run(&out)<0 && !begins && !ceph_cm3_native.attempted);
    reset();ceph_gpll0_lock=1;assert(run(&out)<0 && !begins && ceph_gpll0_lock);
    reset();owner.finish=NULL;assert(run(&out)<0 && !begins);
    reset();assert(cepheus_gmu_start_cold_native(NULL,&config,&out)<0);
    assert(cepheus_gmu_start_cold_native(&owner,NULL,&out)<0);
    assert(cepheus_gmu_start_cold_native(&owner,&config,NULL)<0);
    reset();config.chipid++;assert(run(&out)<0 && !begins);
    reset();mutate=1;assert(!run(&out) && finished==1 && !owner.finish && !config.chipid);
    reset();reenter=1;assert(!run(&out) && finished==1);
    reset();*reg(CE_GMU_TCM_RETENTION)=0;assert(run(&out)<0 && finished==1 && !kept && !out.effects);
    reset();gmu_off=1;*word(0,0x71004)=0x80000000;
    assert(!mprotect((void *)windows[1],sizes[1],PROT_NONE));
    assert(!mprotect((void *)windows[2],sizes[2],PROT_NONE));
    assert(run(&out)<0 && !kept && !out.effects);
    reset();gmu_off=1;*word(1,0x1540)=0;
    assert(!mprotect((void *)windows[2],sizes[2],PROT_NONE));assert(run(&out)<0 && !kept);
    reset();*reg(CE_GMU_SYSRESET)=0;assert(run(&out)<0 && !kept && !out.effects);
    reset();*reg(CE_GMU_HFI_STATUS)=1;assert(run(&out)<0 && !kept && !out.effects);
    reset();*reg(CE_GMU_HFI_VERSION)=0x10000001;assert(run(&out)<0 && kept && out.state==CE_GS_QUARANTINED);
    reset();fail_fault=1;assert(run(&out)<0 && kept && out.state==CE_GS_QUARANTINED);
    reset();timeout=1;assert(run(&out)<0 && kept && delays<=10000);
    reset();timeout=1;frozen=1;assert(run(&out)<0 && kept && delays==1);
    reset();timeout=1;reverse=1;assert(run(&out)<0 && kept);
    reset();timeout=1;late=1;assert(run(&out)<0 && kept && delays==1);
    reset();timeout=1;*reg(CE_GMU_AO_STATUS)=UINT32_MAX;assert(run(&out)<0 && !kept && !out.effects);
    for(unsigned i=0;i<3;++i)assert(!munmap((void *)windows[i],sizes[i]));
    printf("Native CM3: %u immediate/%u delayed PROT_NONE owner boundaries, fixed maps/time/delay, delayed ACK bounds, copied inputs, cold retention/access gates and uncertain release retention PASS (host model)\n",boundaries,delayed_boundaries);
}
