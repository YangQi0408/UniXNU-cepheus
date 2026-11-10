#define _DEFAULT_SOURCE 1
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "cepheus_gpu_compile.h"
#include "cepheus_gmu_hfi_link.h"
#include "../tests/gpll0-scope-fixture.h"
#include "../tests/gpu_config_fixture.h"
static volatile uint8_t *windows[6];
static const size_t sizes[]={0x90000,0x9000,0x30000,0x10000,4096,4096};
static unsigned ceph_gpll0_lock,maps,mode,checks,begins,ends,excluded,delays,query_delays,revoked;
static unsigned bin,booting,fw_received,boot_fail,late_boot,config_ends;
static unsigned starting,late_start;
static unsigned qmp_checks,qmp_expire,qmp_final_loss;
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
    static const uint64_t physical[]={0x100000,0x2c90000,0x2c6a000,0x2ca0000,0xc300000,0x17c0000c};
    for(unsigned n=0;n<6;++n)if(pa==physical[n]){assert(bytes==(n==5?4:sizes[n]));++maps;return (uintptr_t)windows[n];}
    assert(0);return 0;
}
static uint64_t mach_absolute_time(void){ticks+=1000;return ticks;}
static void absolutetime_to_nanoseconds(uint64_t value,uint64_t *out){*out=value;}
static void delay(unsigned us)
{
    assert(us==1 || us==500);ticks+=(uint64_t)us*1000;++delays;
    if(us==500) {
        volatile uint32_t *ram=(volatile uint32_t *)windows[4],*irq=(volatile uint32_t *)windows[5];
        *irq=0;
        if(ram[9]==CE_QMP_UP)ram[10]=CE_QMP_UP;
        if(ram[11]==CE_QMP_UP){ram[5]=CE_QMP_UP;if(ram[6]==CE_QMP_UP)ram[12]=CE_QMP_UP;}
        if(ram[32]==32)ram[32]=0;
    }
}
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
static void stage_finish(void *ctx,int keep)
{
    assert(ctx==windows && ceph_gpll0_lock && keep);
    if(mode==29 && maps==12)revoked=1;
}
extern int cepheus_gpu_upload_snapshot(struct ceph_gmu_upload *,uint64_t,struct ceph_gmu_block *,unsigned);
#include "cepheus_smmu_native_private.h"
#include "cepheus_gmu_upload_native_private.h"
#include "cepheus_gmu_start_native_private.h"
#include "cepheus_gmu_oob_native_private.h"
#include "cepheus_qmp_native_private.h"
static uint32_t rh_read(uintptr_t base,uint32_t off)
{assert(base==(uintptr_t)config_db && rh_lock);return config_read(config_db,off);}
int cepheus_gpu_fuse_sample(uint32_t *out){*out=bin<<29;return 0;}
#include "cepheus_gpu_config_native_private.h"
#include "cepheus_gmu_hfi_native_private.h"
static int query_begin(void *ctx,const struct ceph_gpu_publication *pub)
{assert(ctx==windows && ceph_gpll0_lock && pub->group==publication.group);++begins;return 0;}
static int query_validate(void *ctx)
{
    assert(ctx==windows && ceph_gpll0_lock);++checks;
    if(revoked)return -1;
    if(booting && table[16]!=table[17] && *word(2,0x1f994*4-0x6a000)==1) {
        static const unsigned ids0[]={4,3,11,14},ids1[]={4,3,11,7,11,11,14};
        unsigned r=table[16],w=table[17],header=table[56+r],words=(header>>8)&255u;
        const unsigned *ids=bin?ids1:ids0;
        if(starting)assert((header&255u)==(starting==1?30u:10u));
        else assert(fw_received<(bin?7u:4u) && (header&255u)==ids[fw_received]);
        uint32_t expected[160]={0};
        struct ceph_hfi_boot_config c;assert(!ceph_gpu_config_hfi(&ceph_config_native.config,0x20000001,&c));
        if(starting==1)assert(ceph_hfi_dcvs(expected,160,1,CE_HFI_DCVS_INVALID)==4);
        else if(starting==2)assert(ceph_hfi_start(expected,160)==1);
        else if(fw_received==0)assert(ceph_hfi_perf(expected,160,c.gx,c.ng,c.cx,c.nc,c.gfx,c.cxl,c.mx)==59);
        else if(fw_received==1)assert(ceph_hfi_bandwidth(expected,160,c.bw)==160);
        else if(fw_received==2)ceph_hfi_feature(expected,160,1,0,0);
        else if(bin && fw_received==3)assert(ceph_hfi_acd_table(expected,160,c.acd)==17);
        else if(bin && fw_received==4)ceph_hfi_feature(expected,160,12,1,0);
        else if(bin && fw_received==5)ceph_hfi_feature(expected,160,7,1,c.throttle_mask);
        else ceph_hfi_core_start(expected,160);
        assert(words<=160 && words==((expected[0]>>8)&255u));
        assert((header&0xfffffu)==expected[0]);
        for(unsigned i=1;i<words;++i)assert(table[56+(r+i)%1024]==expected[i]);
        unsigned padded=(words+3u)&~3u;assert(w==(r+padded)%1024);++fw_received;
        unsigned reply=table[29];assert(reply==table[28]);
        table[1080+reply]=ceph_hfi_header(header&255u,3,1,0);table[1080+(reply+1)%1024]=header;
        table[1080+(reply+2)%1024]=boot_fail==fw_received?9:0;
        table[29]=(reply+4)%1024;table[16]=w;
        *word(2,0x1f992*4-0x6a000)=1;
    }
    if(ceph_hfi_native.sealed && table[17] && !excluded) {
        struct ceph_gmu_start_config config;struct ceph_gmu_mapping mappings[2];
        assert(ceph_memory_retire_begin(&publication)<0 && cepheus_gpu_memory_abandon_workspace(&publication)<0);
        assert(ceph_memory_smmu_acquire(&publication,&config)<0);
        assert(ceph_memory_upload_acquire(&publication,&config,mappings)<0);excluded=1;
        if(mode==7){revoked=1;return -1;}
    }
    if(!booting && *word(2,0x1f994*4-0x6a000)==1 && table[17] && delays-query_delays>=(mode==8?3u:0u)) {
        if(!table[29]) {
            assert(table[17]==4 && table[56]==ceph_hfi_header(1,2,0,1) && table[57]==0x20000000);
            table[16]=table[17];table[1080]=ceph_hfi_header(1,4,1,0);
            table[1081]=table[56]^(mode==2?1u:0u);table[1082]=0;
            table[1083]=0x20000042;table[29]=4;
        }
        *word(2,0x1f992*4-0x6a000)=1;
    }
    if(mode==3 && table[28]){ceph_cm3_native.failed=1;return -1;}
    if(starting && ceph_oob_native.txn.state==CE_OOB_REQUESTED)
        *word(2,0x1f992*4-0x6a000)|=ceph_oob_native.txn.bits.ack;
    return 0;
}
static void query_finish(void *ctx,unsigned words,unsigned doorbells,int keep)
{
    assert(ctx==windows && ceph_gpll0_lock && words<=(booting?160u:4u) && doorbells<=(booting?7u:1u));++ends;
    if(mode!=5 && mode!=6)assert(keep);
    if(mode==4)revoked=1;
    if(booting && late_boot)revoked=1;
    if(starting==2 && late_start)revoked=1;
}
static int config_begin(void *ctx,const char *sha)
{assert(ctx==windows && rh_lock && !strcmp(sha,CE_GPU_CONFIG_FDT_SHA256));return 0;}
static int config_validate(void *ctx){assert(ctx==windows && rh_lock);return revoked?-1:0;}
static void config_finish(void *ctx,int e){assert(ctx==windows && rh_lock && !e);++config_ends;}
static int qmp_begin(void *ctx){assert(ctx==windows && ceph_gpll0_lock);return 0;}
static int qmp_validate(void *ctx)
{
    assert(ctx==windows && ceph_gpll0_lock);++qmp_checks;
    if(qmp_expire && qmp_checks>=qmp_expire)return -1;
    return stage_validate(ctx);
}
static void qmp_finish(void *ctx,unsigned writes,unsigned notifications,int keep)
{
    assert(ctx==windows && ceph_gpll0_lock && writes==13 && notifications==4 && keep);
    if(qmp_final_loss)qmp_expire=qmp_checks+1;
}
static int run_qmp(void)
{
    volatile uint32_t *ram=(volatile uint32_t *)windows[4];ram[0]=0x4d41494c;ram[1]=1;
    for(unsigned c=0;c<2;++c)for(unsigned i=0;i<4;++i)ram[3+6*c+i]=CE_QMP_DOWN;
    ram[3]=CE_QMP_UP;ram[7]=64;ram[8]=64;ram[13]=128;ram[14]=128;
    struct ceph_qmp_native_owner qmp={windows,qmp_begin,qmp_validate,qmp_finish,1};
    struct ceph_qmp_native_result qr;
    int e=cepheus_gpu_qmp_acd_native(&qmp,&qr);
    assert(e || qr.acd.consumed);
    return e;
}
static int oob_drained(void *ctx){assert(ctx==windows && ceph_gpll0_lock);return 0;}
static void oob_finish(void *ctx,unsigned stores,int keep)
{assert(ctx==windows && ceph_gpll0_lock && stores==2 && keep);}
int test_hfi_native_pool(uint64_t snapshot,const struct ceph_gmu_workspace *workspace,
    const struct ceph_smmu_plan *plan,unsigned selected)
{
    assert(snapshot && workspace && plan && selected<=30);mode=selected;publication=workspace->publication;
    bin=mode==10 || mode>=24;boot_fail=mode>=11 && mode<=14?mode-10:0;late_boot=mode==15;late_start=mode==23;
    table=workspace->views[2].cpu;ticks=1000000;gpll0_scope_reset();
    for(unsigned n=0;n<6;++n) {
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
    config_fixture();rh_db=(uintptr_t)config_db;rh_db_size=sizeof(config_db);
    struct ceph_gpu_config_native_owner cfg_owner={windows,config_begin,config_validate,config_finish,1};
    struct ceph_gpu_config config;
    assert(!cepheus_gpu_config_build_native(&cfg_owner,&config) && config_ends==1);
    if(bin && mode!=25 && mode!=27)assert(!run_qmp());
    struct ceph_gmu_start_native_owner cm3={windows,cm3_begin,stage_validate,stage_finish};
    struct ceph_gmu_start started;
    if(mode==25 || mode==27 || mode==28 || mode==30) {
        if(mode==27){qmp_final_loss=1;assert(run_qmp()<0 && ceph_qmp_native.failed);}
        if(mode==28)ceph_qmp_native.owner.rpmh_token=0;
        if(mode==30)config.bw.ddr_data[3][1]^=1;
        unsigned before=maps;started=(struct ceph_gmu_start){CE_GS_NEW,123,456,789};
        assert(cepheus_gmu_start_cold_native(&cm3,&workspace->layout.start,&config,&started)<0 &&
            !ceph_cm3_native.attempted && maps==before && started.error==456 && started.effects==123);
        if(mode!=25) {
            for(unsigned n=0;n<6;++n)assert(!munmap((void *)windows[n],sizes[n]));
            printf("Native config/QMP cold admission mode=%u PASS (no CM3 effect, emulated hardware)\n",mode);
            return -1;
        }
        assert(!run_qmp());
    }
    if(mode==29) {
        assert(cepheus_gmu_start_cold_native(&cm3,&workspace->layout.start,&config,&started)<0 &&
            started.state==CE_GS_QUARANTINED && ceph_cm3_native.failed && maps==12);
        for(unsigned n=0;n<6;++n)assert(!munmap((void *)windows[n],sizes[n]));
        printf("Native config/QMP cold final callback mode=29 PASS (no HFI query, emulated hardware)\n");
        return -1;
    }
    assert(!cepheus_gmu_start_cold_native(&cm3,&workspace->layout.start,&config,&started) && started.state==CE_GS_HFI_READY);
    assert(maps==(bin?12u:10u));query_delays=delays;
    unsigned query_maps=maps;
    if(mode==1)publication.asid++;
    if(mode==5)*word(2,0x1f996*4-0x6a000)=1;
    if(mode==6)table[17]=4;
    struct ceph_hfi_native_owner owner={windows,query_begin,query_validate,query_finish,1};
    struct ceph_hfi_native_result result;
    int e=cepheus_gmu_hfi_version_native(&owner,&publication,&result);
    assert((e==0)==(mode==0 || mode==8 || mode>=9));
    if(!e)assert(result.firmware_version==0x20000042 && result.hfi_version==0x20000001 && result.retained);
    else assert(!result.firmware_version);
    assert(maps==query_maps && !ceph_gpll0_lock && begins==(mode!=1) && ends==begins);
    if(mode==1 || mode==5 || mode==6)assert(!result.retained && !result.doorbells);
    else assert(result.retained && excluded);
    if(mode==8)assert(delays-query_delays==3);
    result.error=123;
    assert(cepheus_gmu_hfi_version_native(&owner,&publication,&result)<0 && result.error==123 && ends==begins);
    if(mode>=9) {
        unsigned before=maps;booting=1;
        struct ceph_hfi_boot_native_result boot={.error=123};
        if(mode==16)config.bw.ddr_data[3][1]^=1;
        if(mode==17)ceph_cm3_native.failed=1;
        int be=cepheus_gmu_hfi_boot_native(&config,&boot);
        assert((be==0)==(mode==9 || mode==10 || (mode>=18 && mode<=26)));
        assert(maps==before);
        if(mode==16)assert(boot.error==123 && !ceph_hfi_native.boot_attempted && ends==1);
        else {
            assert(boot.retained && ends==2 && boot.doorbells==fw_received);
            if(be)assert(!boot.firmware_version && !ceph_hfi_native.boot_ready);
            else assert(boot.firmware_version==0x20000042 && boot.acked==(bin?7u:4u) && ceph_hfi_native.boot_ready);
        }
        boot.error=123;
        assert(cepheus_gmu_hfi_boot_native(&config,&boot)<0 && boot.error==123);
        if(mode>=18 && !be) {
            struct ceph_hfi_start_native_result sr={.error=123};
            assert(cepheus_gmu_hfi_start_native(&sr)<0 && sr.error==123);
            starting=1;if(mode==19)boot_fail=fw_received+1;
            if(mode==26)qmp_expire=qmp_checks+1;
            int de=cepheus_gmu_hfi_dcvs_native(&sr);
            assert((de==0)==(mode!=19 && mode!=26) && !sr.start_acked && sr.retained);
            sr.error=123;assert(cepheus_gmu_hfi_dcvs_native(&sr)<0 && sr.error==123);
            if(!de) {
                *word(2,0x1f992*4-0x6a000)=0;
                struct ceph_hfi_start_native_result absent={.error=123};
                assert(cepheus_gmu_hfi_start_native(&absent)<0 && absent.error==123 && !ceph_hfi_native.start_attempted);
                if(mode!=21) {
                    struct ceph_oob_native_owner oob={windows,qmp_begin,query_validate,oob_drained,oob_finish,1};
                    struct ceph_gmu_oob held;assert(!cepheus_gpu_oob_acquire_native(&oob,&held) && held.state==CE_OOB_HELD);
                    *word(2,0x1f992*4-0x6a000)=0;starting=2;
                    if(mode==20)boot_fail=fw_received+1;
                    if(mode==22)ceph_oob_native.failed=1;
                    sr.error=123;int se=cepheus_gmu_hfi_start_native(&sr);
                    assert((se==0)==(mode==18 || mode==24 || mode==25));
                    if(mode==22)assert(sr.error==123 && !ceph_hfi_native.start_attempted);
                    else assert(sr.retained && sr.start_acked==(mode==18 || mode==24 || mode==25));
                    sr.error=123;assert(cepheus_gmu_hfi_start_native(&sr)<0 && sr.error==123);
                }
            }
        }
    }
    for(unsigned n=0;n<6;++n)assert(!munmap((void *)windows[n],sizes[n]));
    printf("Actual SMMU/upload/CM3/HFI + actual pool mode=%u checks=%u PASS (emulated MMIO/ACK/IOKit, no GPU)\n",mode,checks);
    return e;
}
