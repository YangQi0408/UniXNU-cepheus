#define _DEFAULT_SOURCE 1
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "cepheus_gpu_compile.h"
#include "cepheus_gmu_hfi_link.h"
#include "../tests/gpll0-scope-fixture.h"
static volatile uint8_t *windows[4];
static const size_t sizes[]={0x90000,0x9000,0x30000,0x10000};
static unsigned ceph_gpll0_lock,maps,mode,checks,begins,ends,excluded,delays,query_delays,revoked;
static uint64_t ticks;
static uint32_t *table;
static struct ceph_gpu_publication publication;
static int ceph_scm_info_context(void *unused){(void)unused;return 0;}
static volatile uint32_t *word(unsigned n,uint32_t off)
{assert(n<4 && off<sizes[n]);return (volatile uint32_t *)(windows[n]+off);}
static volatile uint32_t *reg(enum ceph_gmu_reg r)
{uint32_t off;assert(!ceph_gmu_register_offset(r,&off));return word(2,off);}
static uintptr_t ml_io_map(uint64_t pa,size_t bytes)
{
    static const uint64_t physical[]={0x100000,0x2c90000,0x2c6a000,0x2ca0000};
    for(unsigned n=0;n<4;++n)if(pa==physical[n]){assert(bytes==sizes[n]);++maps;return (uintptr_t)windows[n];}
    assert(0);return 0;
}
static uint64_t mach_absolute_time(void){ticks+=1000;return ticks;}
static void absolutetime_to_nanoseconds(uint64_t value,uint64_t *out){*out=value;}
static void delay(unsigned us){assert(us==1);ticks+=1000;++delays;}
static int stage_validate(void *ctx)
{
    assert(ctx==windows && ceph_gpll0_lock);
    if(!*reg(CE_GMU_SYSRESET))*reg(CE_GMU_INIT_RESULT)=0xbabeface;
    if(*reg(CE_GMU_HFI_INIT))*reg(CE_GMU_HFI_STATUS)=1;
    return 0;
}
static int smmu_begin(void *ctx,const struct ceph_smmu_plan *p,const struct ceph_gpu_publication *pub)
{assert(ctx==windows && ceph_gpll0_lock && p->ttbr0==pub->ttbr0);return 0;}
static int upload_begin(void *ctx,const struct ceph_gpu_publication *pub,uint64_t snapshot)
{assert(ctx==windows && ceph_gpll0_lock && pub->group==publication.group && snapshot);return 0;}
static int cm3_begin(void *ctx,const struct ceph_gmu_start_config *config)
{assert(ctx==windows && ceph_gpll0_lock && config->chipid==0x06040001);return 0;}
static void stage_finish(void *ctx,int keep){assert(ctx==windows && ceph_gpll0_lock && keep);}
extern int cepheus_gpu_upload_snapshot(struct ceph_gmu_upload *,uint64_t,struct ceph_gmu_block *,unsigned);
#include "cepheus_smmu_native_private.h"
#include "cepheus_gmu_upload_native_private.h"
#include "cepheus_gmu_start_native_private.h"
#include "cepheus_gmu_hfi_native_private.h"
static int query_begin(void *ctx,const struct ceph_gpu_publication *pub)
{assert(ctx==windows && ceph_gpll0_lock && pub->group==publication.group);++begins;return 0;}
static int query_validate(void *ctx)
{
    assert(ctx==windows && ceph_gpll0_lock);++checks;
    if(revoked)return -1;
    if(ceph_hfi_native.sealed && table[17] && !excluded) {
        struct ceph_gmu_start_config config;struct ceph_gmu_mapping mappings[2];
        assert(ceph_memory_retire_begin(&publication)<0 && cepheus_gpu_memory_abandon_workspace(&publication)<0);
        assert(ceph_memory_smmu_acquire(&publication,&config)<0);
        assert(ceph_memory_upload_acquire(&publication,&config,mappings)<0);excluded=1;
        if(mode==7){revoked=1;return -1;}
    }
    if(*word(2,0x1f994*4-0x6a000)==1 && table[17] && delays-query_delays>=(mode==8?3u:0u)) {
        if(!table[29]) {
            assert(table[17]==4 && table[56]==ceph_hfi_header(1,2,0,1) && table[57]==0x20000000);
            table[16]=table[17];table[1080]=ceph_hfi_header(1,4,1,0);
            table[1081]=table[56]^(mode==2?1u:0u);table[1082]=0;
            table[1083]=0x20000042;table[29]=4;
        }
        *word(2,0x1f992*4-0x6a000)=1;
    }
    if(mode==3 && table[28]){ceph_cm3_native.failed=1;return -1;}
    return 0;
}
static void query_finish(void *ctx,unsigned words,unsigned doorbells,int keep)
{
    assert(ctx==windows && ceph_gpll0_lock && words<=4 && doorbells<=1);++ends;
    if(mode!=5 && mode!=6)assert(keep);
    if(mode==4)revoked=1;
}
int test_hfi_native_pool(uint64_t snapshot,const struct ceph_gmu_workspace *workspace,
    const struct ceph_smmu_plan *plan,unsigned selected)
{
    assert(snapshot && workspace && plan && selected<=8);mode=selected;publication=workspace->publication;
    table=workspace->views[2].cpu;ticks=1000000;gpll0_scope_reset();
    for(unsigned n=0;n<4;++n) {
        windows[n]=mmap(NULL,sizes[n],PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        assert(windows[n]!=MAP_FAILED);
    }
    *word(0,0x71004)=1;*word(0,CE_GPU_GCC_MEMNOC)=*word(0,CE_GPU_GCC_DVM)=1;
    *word(1,0x106c)=0x222000;*word(1,0x1540)=1u<<31;*word(1,0x100c)=0x222001;
    *word(1,0x1098)=*word(1,0x109c)=*word(1,0x1078)=1;
    *reg(CE_GMU_SYSRESET)=*reg(CE_GMU_TCM_RETENTION)=1;*reg(CE_GMU_HFI_VERSION)=0x20000001;
    *word(3,0x20)=(1u<<30)|(1u<<27)|(11u<<9)|16;*word(3,0x24)=(2u<<28)|8;*word(3,0x28)=0x1022;
    for(unsigned i=0;i<16;++i)*word(3,0xc00+4*i)=2u<<16;
    struct ceph_smmu_native_owner smmu={windows,smmu_begin,stage_validate,stage_finish};
    struct ceph_smmu_install installed;
    assert(!cepheus_gpu_smmu_install_native(&smmu,plan,&publication,&installed) && installed.state==CE_SM_ENABLED);
    struct ceph_gmu_upload_native_owner upload={windows,upload_begin,stage_validate,stage_finish};
    struct ceph_gmu_upload_native_result uploaded;
    assert(!cepheus_gmu_upload_native(&upload,snapshot,&publication,&uploaded) && uploaded.writes && !uploaded.quarantined);
    struct ceph_gmu_start_native_owner cm3={windows,cm3_begin,stage_validate,stage_finish};
    struct ceph_gmu_start started;
    assert(!cepheus_gmu_start_cold_native(&cm3,&workspace->layout.start,&started) && started.state==CE_GS_HFI_READY);
    assert(maps==10);query_delays=delays;
    if(mode==1)publication.asid++;
    if(mode==5)*word(2,0x1f996*4-0x6a000)=1;
    if(mode==6)table[17]=4;
    struct ceph_hfi_native_owner owner={windows,query_begin,query_validate,query_finish,1};
    struct ceph_hfi_native_result result;
    int e=cepheus_gmu_hfi_version_native(&owner,&publication,&result);
    assert((e==0)==(mode==0 || mode==8));
    if(!e)assert(result.firmware_version==0x20000042 && result.hfi_version==0x20000001 && result.retained);
    else assert(!result.firmware_version);
    assert(maps==10 && !ceph_gpll0_lock && begins==(mode!=1) && ends==begins);
    if(mode==1 || mode==5 || mode==6)assert(!result.retained && !result.doorbells);
    else assert(result.retained && excluded);
    if(mode==8)assert(delays-query_delays==3);
    result.error=123;
    assert(cepheus_gmu_hfi_version_native(&owner,&publication,&result)<0 && result.error==123 && ends==begins);
    for(unsigned n=0;n<4;++n)assert(!munmap((void *)windows[n],sizes[n]));
    printf("Actual SMMU/upload/CM3/HFI + actual pool mode=%u checks=%u PASS (emulated MMIO/ACK/IOKit, no GPU)\n",mode,checks);
    return e;
}
