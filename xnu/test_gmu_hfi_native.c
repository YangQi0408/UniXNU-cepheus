#define _DEFAULT_SOURCE 1
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "cepheus_gpu_compile.h"
#include "cepheus_gmu_hfi_link.h"
#include "../tests/gpll0-scope-fixture.h"
/* Synthetic bin0 identity for focused query/CM3. The separate mixed native/
 * pool chain executes real config parsing and QMP; this is not DB evidence. */
int cepheus_gpu_config_check_native(const struct ceph_gpu_config *c)
{return !c || c->policy.bin || !ceph_gpu_bin_policy_valid(&c->policy) ||
    c->pdc.chipid!=0x06040001 || c->initial_freq!=1 || c->initial_bw!=3?-1:0;}
static volatile uint8_t *windows[4];
static const size_t sizes[]={0x90000,0x9000,0x30000,0x10000};
static uint32_t *table;
static unsigned ceph_gpll0_lock,context,maps,checks,expire,begins,ends,kept,pin,sealed;
static unsigned silent,delays,delay_ack,time_fault,mutate,reenter,powered_off,fail_finish;
static unsigned wrong_ack,wrong_version,late_record,begin_error;
static uint64_t ticks;
static struct ceph_gpu_publication publication;
static struct ceph_hfi_native_owner owner;
static int ceph_scm_info_context(void *unused){(void)unused;return context?-1:0;}
static volatile uint32_t *word(unsigned n,uint32_t off)
{assert(n<4 && off<sizes[n]);return (volatile uint32_t *)(windows[n]+off);}
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
/* Synthetic prerequisite records. Production compiles against the real
 * private SMMU/upload declarations; this test does not run those stages. */
static struct {
    struct ceph_gpu_publication publication;
    struct ceph_smmu_mmio mmio;
    struct ceph_gmu_mmio gmu;
    struct ceph_smmu_install txn;
    unsigned attempted,acquired,finished,failed,pinned;
} ceph_smmu_native;
static struct {
    struct ceph_gpu_publication publication;
    struct ceph_gmu_mmio gmu;
    struct ceph_gmu_upload upload;
    unsigned attempted,acquired,finished,failed,pinned;
} ceph_upload_native;
int cepheus_gpu_memory_group_status(uint64_t group,struct ceph_gpu_group_status *out)
{
    if(group!=publication.group)return -1;
    *out=(struct ceph_gpu_group_status){.identity=publication,.state=CE_GPU_GROUP_SHARED};return 0;
}
int ceph_memory_hfi_acquire(const struct ceph_gpu_publication *p,struct ceph_gpu_memory_view *out,
    struct ceph_gmu_start_config *config)
{
    if(pin || p->group!=publication.group)return -1;
    pin=1;*out=(struct ceph_gpu_memory_view){table,0x120084000,32768};
    *config=ceph_cm3_native.mmio.config;return 0;
}
int ceph_memory_hfi_validate(const struct ceph_gpu_publication *p)
{return pin && p->group==publication.group?0:-1;}
int ceph_memory_hfi_publish(const struct ceph_gpu_publication *p){return ceph_memory_hfi_validate(p);}
int ceph_memory_hfi_seal(const struct ceph_gpu_publication *p)
{if(ceph_memory_hfi_validate(p) || sealed)return -1;sealed=1;return 0;}
int ceph_memory_hfi_finish(const struct ceph_gpu_publication *p,int result)
{
    assert(p->group==publication.group);
    if(fail_finish)return -1;
    if(result==CE_MEMORY_HFI_REFUSED){assert(!sealed);pin=0;}
    else assert(sealed);
    return 0;
}
#include "cepheus_qmp_native_private.h"
#include "cepheus_gmu_oob_native_private.h"
#include "cepheus_gmu_hfi_native_private.h"
static void revoke(void)
{
    for(unsigned n=0;n<4;++n)assert(!mprotect((void *)windows[n],sizes[n],PROT_NONE));
    assert(!mprotect(table,32768,PROT_NONE));
}
static int begin(void *ctx,const struct ceph_gpu_publication *p)
{assert(ctx==windows && ceph_gpll0_lock && p->group==publication.group);++begins;return begin_error?-7:0;}
static int validate(void *ctx)
{
    assert(ctx==windows && ceph_gpll0_lock);++checks;
    if(expire && checks>=expire){revoke();return -1;}
    if(powered_off)return 0;
    if(mutate){mutate=0;owner.validate=NULL;owner.begin=NULL;owner.finish=NULL;owner.rpmh_token=0;}
    if(reenter) {
        reenter=0;struct ceph_hfi_native_result out={.error=123};
        assert(cepheus_gmu_hfi_version_native(&owner,&publication,&out)<0 && out.error==123);
    }
    if(!silent && delays>=delay_ack && *word(2,0x1f994*4-0x6a000)==1 && table[17]) {
        if(!table[29]) {
            table[16]=table[17];table[1080]=ceph_hfi_header(1,4,1,0);
            table[1081]=table[56]^(wrong_ack?1u:0u);table[1082]=0;
            table[1083]=wrong_version?0x10000000:0x20000042;table[29]=4;
        }
        *word(2,0x1f992*4-0x6a000)=1;
    }
    if(late_record && table[28])ceph_cm3_native.failed=1;
    return 0;
}
static void finish(void *ctx,unsigned words,unsigned doorbells,int keep)
{assert(ctx==windows && ceph_gpll0_lock && words<=4 && doorbells<=1);++ends;kept=(unsigned)keep;}
static void reset(void)
{
    for(unsigned n=0;n<4;++n) {
        assert(!mprotect((void *)windows[n],sizes[n],PROT_READ|PROT_WRITE));memset((void *)windows[n],0,sizes[n]);
    }
    assert(!mprotect(table,32768,PROT_READ|PROT_WRITE));memset(table,0,32768);
    memset(&ceph_hfi_native,0,sizeof(ceph_hfi_native));memset(&ceph_cm3_native,0,sizeof(ceph_cm3_native));
    memset(&ceph_smmu_native,0,sizeof(ceph_smmu_native));memset(&ceph_upload_native,0,sizeof(ceph_upload_native));
    gpll0_scope_reset();ceph_gpll0_lock=context=maps=checks=expire=begins=ends=kept=pin=sealed=0;
    silent=delays=delay_ack=time_fault=mutate=reenter=powered_off=fail_finish=wrong_ack=wrong_version=late_record=begin_error=0;ticks=1000000;
    *word(0,0x71004)=1;*word(0,CE_GPU_GCC_MEMNOC)=*word(0,CE_GPU_GCC_DVM)=1;
    *word(1,0x106c)=0x222000;*word(1,0x1540)=1u<<31;*word(1,0x100c)=0x222001;
    *word(1,0x1098)=*word(1,0x109c)=*word(1,0x1078)=1;
    *reg(CE_GMU_SYSRESET)=1;*reg(CE_GMU_TCM_RETENTION)=1;*reg(CE_GMU_HFI_VERSION)=0x20000001;
    struct ceph_gmu_start_native_owner cm3={windows,cm3_begin,cm3_validate,cm3_finish};
    struct ceph_gmu_start_config config={0x06040001,0x60100000,20480,0x60200000,4096};
    struct ceph_gpu_config gpu={.policy={0,8,0,0,0x80},.pdc={.chipid=0x06040001},.initial_freq=1,.initial_bw=3};
    struct ceph_gmu_start started;assert(!cepheus_gmu_start_cold_native(&cm3,&config,&gpu,&started));
    struct ceph_smmu_stream streams[8]={0};for(unsigned i=0;i<8;++i)streams[i].s2cr=2u<<16;
    struct ceph_smmu_plan plan;assert(!ceph_smmu_gmu_plan(streams,8,2,3,8,5,4,0x120094000,0,&plan));
    publication=(struct ceph_gpu_publication){.group=1,.ttbr0=plan.ttbr0,.ttbr1=plan.ttbr1,
        .smr=plan.smr,.cb=plan.cb,.slot=plan.slot,.asid=plan.asid};
    for(unsigned i=0;i<6;++i)publication.handles[i]=i+1;
    ceph_smmu_native.publication=ceph_upload_native.publication=publication;
    ceph_smmu_native.gmu=ceph_upload_native.gmu=ceph_cm3_native.mmio;
    ceph_smmu_native.attempted=ceph_smmu_native.acquired=ceph_smmu_native.finished=1;
    ceph_smmu_native.txn.state=CE_SM_ENABLED;
    ceph_upload_native.attempted=ceph_upload_native.acquired=ceph_upload_native.finished=1;ceph_upload_native.upload.writes=1;
    struct ceph_smmu_geometry geometry;
    assert(!ceph_smmu_geometry((1u<<30)|(1u<<27)|(11u<<9)|8,(2u<<28)|8,(1u<<12)|(2u<<4)|2,0x10000,&geometry));
    ceph_smmu_native.mmio=(struct ceph_smmu_mmio){windows[3],0x10000,geometry,plan,0x303,NULL,NULL};
    const enum ceph_smmu_reg regs[]={CE_SM_SMR,CE_SM_S2CR,CE_SM_TTBR0,CE_SM_TTBR1,CE_SM_SCTLR,CE_SM_FSR};
    const uint64_t values[]={plan.smr,plan.s2cr_translate,plan.ttbr0,plan.ttbr1,plan.sctlr,0};
    ceph_smmu_native.mmio.lease=cm3_validate;
    for(unsigned i=0;i<6;++i) {
        uint32_t off;unsigned width;assert(!ceph_smmu_mmio_offset(&ceph_smmu_native.mmio,regs[i],&off,&width));
        if(width==8)*(volatile uint64_t *)(windows[3]+off)=values[i];else *word(3,off)=(uint32_t)values[i];
    }
    assert(!ceph_hfi_table(table,54,0x60100000,20480));delays=0;
    owner=(struct ceph_hfi_native_owner){windows,begin,validate,finish,1};
}
static int run(struct ceph_hfi_native_result *out)
{return cepheus_gmu_hfi_version_native(&owner,&publication,out);}
int main(void)
{
    for(unsigned n=0;n<4;++n) {
        windows[n]=mmap(NULL,sizes[n],PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(windows[n]!=MAP_FAILED);
    }
    table=mmap(NULL,32768,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(table!=MAP_FAILED);
    struct ceph_hfi_native_result out;
    reset();assert(!run(&out) && out.firmware_version==0x20000042 && out.hfi_version==0x20000001);
    assert(out.doorbells==1 && out.packet_words==4 && sealed && pin && kept && ends==1 && maps==3);
    unsigned boundaries=checks,scope_boundaries=scope_checks;
    out.error=123;assert(run(&out)<0 && out.error==123 && ends==1);
    /* Exhaustive queue guards are separate tests. Sample 128 distributed
     * native compound guards plus the first/last, all with inaccessible RAM. */
    for(unsigned i=0;i<=128;++i) {
        reset();expire=1+(uint64_t)(boundaries-1)*i/128;
        assert(run(&out)<0 && !out.firmware_version && checks==expire);
        assert(!out.doorbells || (sealed && out.retained));
        expire=0;out.error=123;assert(run(&out)<0 && out.error==123);
    }
    for(unsigned i=0;i<=128;++i) {
        reset();scope_expire=1+(uint64_t)(scope_boundaries-1)*i/128;scope_revoke=revoke;
        assert(run(&out)<0 && !out.firmware_version && scope_checks==scope_expire);
        assert(!out.doorbells || out.retained);
    }
    reset();mutate=reenter=1;assert(!run(&out) && kept && !owner.finish);
    reset();begin_error=1;assert(run(&out)==-7 && begins==1 && !ends && !pin);
    reset();owner.rpmh_token=0;assert(run(&out)<0 && !begins && !pin);
    reset();ceph_smmu_native.finished=0;assert(run(&out)<0 && !begins);
    reset();ceph_upload_native.failed=1;assert(run(&out)<0 && !begins);
    reset();publication.asid++;assert(run(&out)<0 && !begins);
    reset();*reg(CE_GMU_HFI_VERSION)=0x20000000;assert(run(&out)<0 && !out.doorbells);
    reset();*word(3,0xb058)=1;assert(run(&out)<0 && !out.doorbells);
    reset();*word(2,0x1f996*4-0x6a000)=1;assert(run(&out)<0 && !out.doorbells && !sealed);
    reset();*word(2,0x1f992*4-0x6a000)=1;assert(run(&out)<0 && !out.doorbells && !sealed);
    reset();table[17]=4;assert(run(&out)<0 && !out.doorbells && sealed && out.retained);
    reset();wrong_ack=1;assert(run(&out)<0 && !out.firmware_version && sealed && kept);
    reset();wrong_version=1;assert(run(&out)<0 && !out.firmware_version && sealed && kept);
    reset();late_record=1;assert(run(&out)<0 && !out.firmware_version && sealed && kept);
    reset();delay_ack=3;assert(!run(&out) && delays==3);
    reset();silent=1;assert(run(&out)<0 && delays<=10000 && out.retained);
    for(unsigned mode=1;mode<=3;++mode) {
        reset();silent=1;time_fault=mode;assert(run(&out)<0 && out.retained);
    }
    reset();fail_finish=1;assert(run(&out)<0 && !out.firmware_version && pin && kept);
    reset();powered_off=1;*word(0,0x71004)=0x80000000;
    for(unsigned n=1;n<4;++n)assert(!mprotect((void *)windows[n],sizes[n],PROT_NONE));
    assert(run(&out)<0 && !out.doorbells);
    reset();context=1;out.error=123;assert(run(&out)<0 && out.error==123 && !begins);
    reset();for(unsigned n=0;n<4;++n)assert(!munmap((void *)windows[n],sizes[n]));assert(!munmap(table,32768));
    printf("Native fixed HFI version: actual CM3 startup + synthetic SMMU/upload/pool, 129/129 distributed owner/scope PROT_NONE samples from %u/%u boundaries, fixed doorbell/ACK, retained queues, failure atomicity PASS (no GPU)\n",boundaries,scope_boundaries);
}
