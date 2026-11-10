#define _DEFAULT_SOURCE 1
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "cepheus_gmu_mmio.h"
#include "cepheus_gpu_power_mmio.h"
#include "cepheus_gmu_start_native.h"
#include "cepheus_gmu_oob_native.h"
#include "../tests/gpll0-scope-fixture.h"
static volatile uint8_t *windows[3];
static const size_t sizes[]={0x90000,0x9000,0x30000};
static unsigned ceph_gpll0_lock,maps,context,checks,expire,begins,ends,kept;
static unsigned begin_error,busy,mutate,reenter,stage,silent,delays,time_fault,ack_delay;
static unsigned late_record,live_version,powered_off;
static uint64_t ticks;
static struct ceph_oob_native_owner owner;
static int ceph_scm_info_context(void *unused){(void)unused;return context?-1:0;}
static volatile uint32_t *word(unsigned n,uint32_t off)
{assert(n<3 && off<sizes[n]);return (volatile uint32_t *)(windows[n]+off);}
static volatile uint32_t *reg(enum ceph_gmu_reg r)
{uint32_t off;assert(!ceph_gmu_register_offset(r,&off));return word(2,off);}
static uintptr_t ml_io_map(uint64_t pa,size_t bytes)
{
    static const uint64_t physical[]={0x100000,0x2c90000,0x2c6a000};
    assert(maps<3 && physical[maps]==pa && sizes[maps]==bytes);return (uintptr_t)windows[maps++];
}
static uint64_t mach_absolute_time(void)
{if(time_fault==2)ticks=0;else if(time_fault!=1)ticks+=1000;return ticks;}
static void absolutetime_to_nanoseconds(uint64_t value,uint64_t *out){*out=value;}
static void delay(unsigned us)
{assert(us==1);++delays;if(time_fault!=1)ticks+=time_fault==3?100001:1000;}
static int cm3_begin(void *ctx,const struct ceph_gmu_start_config *config)
{assert(ctx==windows && config->chipid==0x06040001);return 0;}
static int cm3_validate(void *ctx)
{
    assert(ctx==windows);
    if(!*reg(CE_GMU_SYSRESET))*reg(CE_GMU_INIT_RESULT)=0xbabeface;
    if(*reg(CE_GMU_HFI_INIT))*reg(CE_GMU_HFI_STATUS)=1;
    return 0;
}
static void cm3_finish(void *ctx,int keep){assert(ctx==windows && keep);}
#include "cepheus_gmu_start_native_private.h"
#include "cepheus_gmu_oob_native_private.h"
static void revoke(void)
{for(unsigned i=0;i<3;++i)assert(!mprotect((void *)windows[i],sizes[i],PROT_NONE));}
static int begin(void *ctx){assert(ctx==windows && ceph_gpll0_lock);++begins;return begin_error?-7:0;}
static int validate(void *ctx)
{
    assert(ctx==windows && ceph_gpll0_lock);++checks;
    if(expire && checks>=expire){revoke();return -1;}
    if(powered_off)return 0;
    if(mutate){mutate=0;owner.begin=NULL;owner.validate=NULL;owner.finish=NULL;owner.drained=NULL;owner.rpmh_token=0;}
    if(reenter) {
        reenter=0;struct ceph_gmu_oob out={.error=123};
        assert(cepheus_gpu_oob_acquire_native(&owner,&out)<0 && out.error==123);
        assert(cepheus_gpu_oob_release_native(&out)<0 && out.error==123);
    }
    if(late_record && ceph_oob_native.txn.state==CE_OOB_HELD)ceph_cm3_native.failed=1;
    struct ceph_oob_bits bits;assert(!ceph_gmu_oob_gpu_bits(ceph_cm3_native.txn.hfi_version,&bits));
    if(!stage && *word(2,0x1f994*4-0x6a000)==bits.request && !silent && delays>=ack_delay) {
        *word(2,0x1f992*4-0x6a000)|=bits.ack;stage=1;
    }
    if(stage==1 && *word(2,0x1f991*4-0x6a000)==bits.ack) {
        *word(2,0x1f992*4-0x6a000)&=~bits.ack;stage=2;
    }
    return 0;
}
static int drained(void *ctx){assert(ctx==windows);return busy?-1:0;}
static void finish(void *ctx,unsigned stores,int keep)
{assert(ctx==windows && ceph_gpll0_lock && stores==ceph_oob_native.txn.writes);++ends;kept=(unsigned)keep;}
static void reset(void)
{
    for(unsigned i=0;i<3;++i) {
        assert(!mprotect((void *)windows[i],sizes[i],PROT_READ|PROT_WRITE));
        memset((void *)windows[i],0,sizes[i]);
    }
    memset(&ceph_cm3_native,0,sizeof(ceph_cm3_native));
    memset(&ceph_oob_native,0,sizeof(ceph_oob_native));gpll0_scope_reset();
    ceph_gpll0_lock=maps=context=checks=expire=begins=ends=kept=0;
    begin_error=busy=mutate=reenter=stage=silent=delays=time_fault=ack_delay=powered_off=0;ticks=1000000;
    *word(0,0x71004)=1;*word(1,0x106c)=0x222000;*word(1,0x1540)=1u<<31;
    *word(1,0x100c)=0x222001;*word(1,0x1098)=*word(1,0x109c)=*word(1,0x1078)=1;
    *reg(CE_GMU_SYSRESET)=1;*reg(CE_GMU_TCM_RETENTION)=1;
    *reg(CE_GMU_HFI_VERSION)=live_version?live_version:0x20000001;
    struct ceph_gmu_start_native_owner cm3={windows,cm3_begin,cm3_validate,cm3_finish};
    struct ceph_gmu_start_config config={0x06040001,0x60100000,20480,0x60200000,4096};
    struct ceph_gmu_start out;assert(!cepheus_gmu_start_cold_native(&cm3,&config,&out));
    assert(out.state==CE_GS_HFI_READY && maps==3);scope_checks=0;delays=0;
    owner=(struct ceph_oob_native_owner){windows,begin,validate,drained,finish,1};
}
static int run(struct ceph_gmu_oob *out){return cepheus_gpu_oob_acquire_native(&owner,out);}
int main(void)
{
    for(unsigned i=0;i<3;++i) {
        windows[i]=mmap(NULL,sizes[i],PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        assert(windows[i]!=MAP_FAILED);
    }
    struct ceph_gmu_oob out;
    reset();assert(!run(&out) && out.state==CE_OOB_HELD && out.writes==2);
    assert(begins==1 && ends==1 && kept && scope_idle_retained && maps==3 && stage==2);
    unsigned boundaries=checks,scope_boundaries=scope_checks;
    out.error=123;assert(run(&out)<0 && out.error==123 && ends==1);
    checks=scope_checks=0;
    assert(!cepheus_gpu_oob_release_native(&out) && out.state==CE_OOB_RELEASED && out.writes==3 && ends==2 && kept);
    unsigned release_boundaries=checks,release_scope_boundaries=scope_checks;
    out.error=123;assert(cepheus_gpu_oob_release_native(&out)<0 && out.error==123 && ends==2);
    for(unsigned n=1;n<=boundaries;++n) {
        reset();expire=n;assert(run(&out)<0 && out.state==CE_OOB_FAILED && checks==n && ends==1);
        assert(!out.writes || kept);expire=0;
        out.error=123;assert(run(&out)<0 && out.error==123 && ends==1);
    }
    for(unsigned n=1;n<=scope_boundaries;++n) {
        reset();scope_expire=n;scope_revoke=revoke;
        assert(run(&out)<0 && out.state==CE_OOB_FAILED && scope_checks==n);
        assert(!begins || ends==1);if(out.writes)assert(kept && scope_idle_retained);
    }
    for(unsigned n=1;n<=release_boundaries;++n) {
        reset();assert(!run(&out));checks=0;expire=n;
        assert(cepheus_gpu_oob_release_native(&out)<0 && checks==n && kept);
        assert(out.state!=CE_OOB_RELEASED);
    }
    for(unsigned n=1;n<=release_scope_boundaries;++n) {
        reset();assert(!run(&out));scope_checks=0;scope_expire=n;scope_revoke=revoke;
        assert(cepheus_gpu_oob_release_native(&out)<0 && scope_checks==n && kept);
        assert(out.state!=CE_OOB_RELEASED);
    }
    reset();mutate=reenter=1;assert(!run(&out) && !owner.finish && ends==1);
    assert(!cepheus_gpu_oob_release_native(&out) && ends==2);
    reset();begin_error=1;assert(run(&out)==-7 && begins==1 && !ends && !out.writes);
    reset();owner.rpmh_token=0;assert(run(&out)<0 && !begins && !ends);
    reset();ceph_cm3_native.txn.state=CE_GS_CM3_READY;assert(run(&out)<0 && !begins);
    reset();ceph_cm3_native.failed=1;assert(run(&out)<0 && !begins);
    reset();*reg(CE_GMU_HFI_VERSION)=0x20000000;assert(run(&out)<0 && ends==1 && !out.writes);
    reset();*reg(CE_GMU_SYSRESET)=1;assert(run(&out)<0 && ends==1 && !out.writes);
    reset();*reg(CE_GMU_HFI_STATUS)=0;assert(run(&out)<0 && ends==1 && !out.writes);
    reset();*reg(CE_GMU_AO_STATUS)=UINT32_MAX;assert(run(&out)<0 && ends==1 && !out.writes);
    reset();*word(2,0x1f992*4-0x6a000)=1u<<31;assert(run(&out)<0 && !out.writes);
    reset();*word(2,0x1f996*4-0x6a000)=1u<<30;assert(run(&out)<0 && !out.writes && kept);
    reset();*word(2,0x1f996*4-0x6a000)=1u<<31;assert(run(&out)<0 && !out.writes && kept);
    reset();*word(2,0x1f8d0*4-0x6a000)=0xc0;assert(run(&out)<0 && out.writes==2 && kept);
    reset();assert(!run(&out));busy=1;assert(cepheus_gpu_oob_release_native(&out)<0 && out.state==CE_OOB_HELD && out.writes==2);
    busy=0;out.error=123;assert(cepheus_gpu_oob_release_native(&out)<0 && out.error==123 && ends==1);
    reset();ack_delay=3;assert(!run(&out) && delays==3 && kept);
    reset();late_record=1;assert(run(&out)<0 && out.state==CE_OOB_FAILED && out.writes==2 && kept);late_record=0;
    live_version=0x20000000;reset();assert(!run(&out) && out.bits.request==(1u<<16));
    assert(!cepheus_gpu_oob_release_native(&out) && out.bits.release==(1u<<24));live_version=0;
    reset();powered_off=1;*word(0,0x71004)=0x80000000;
    assert(!mprotect((void *)windows[1],sizes[1],PROT_NONE));
    assert(!mprotect((void *)windows[2],sizes[2],PROT_NONE));
    assert(run(&out)<0 && !out.writes && kept);
    reset();revoke();
    /* Validate can revoke before a read; a missing scope must reject without
     * touching even the clock window. */
    owner.rpmh_token=0;assert(run(&out)<0 && !begins);
    for(unsigned mode=1;mode<=3;++mode) {
        reset();silent=1;time_fault=mode;assert(run(&out)<0 && out.writes==1 && kept);
    }
    reset();silent=1;assert(run(&out)<0 && delays<=10000 && kept);
    reset();context=1;out.error=123;assert(run(&out)<0 && out.error==123 && !begins);
    reset();ceph_gpll0_lock=1;assert(run(&out)<0 && !begins && ceph_gpll0_lock);
    for(unsigned i=0;i<3;++i)assert(!munmap((void *)windows[i],sizes[i]));
    printf("Native OOB0: actual CM3 publication chain, %u/%u acquire and %u/%u release owner/scope PROT_NONE boundaries, fixed retained maps, fresh ACK and fail-closed lifetime PASS (host model)\n",boundaries,scope_boundaries,release_boundaries,release_scope_boundaries);
}
