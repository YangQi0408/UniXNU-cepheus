/* Included after actual HFI/OOB/CM3/SMMU/upload records in the SWEET unit. */
#ifndef CEPHEUS_GX_CORE_NATIVE_PRIVATE_H
#define CEPHEUS_GX_CORE_NATIVE_PRIVATE_H
#include "cepheus_gx_core_native.h"
#include "cepheus_gpu_memory_private.h"
static struct {
    struct ceph_gx_native_owner owner;
    struct ceph_gx_core_plan plan;
    volatile uint32_t *gpu;
    struct ceph_gx_native_result result;
    unsigned attempted,acquired,failed,sp_clock_off;
} ceph_gx_native;
static int ceph_gx_native_record(void)
{
    return !ceph_hfi_native.sealed || !ceph_hfi_native.pinned || ceph_hfi_native.failed ||
        ceph_hfi_native.result.error || !ceph_hfi_native.result.firmware_version ||
        !ceph_hfi_native.boot_ready || ceph_hfi_native.boot.error ||
        !ceph_hfi_native.dcvs_attempted || !ceph_hfi_native.start_attempted ||
        !ceph_hfi_native.start.dcvs_acked || !ceph_hfi_native.start.start_acked ||
        ceph_hfi_native.start.error || !ceph_hfi_native.link.bound ||
        ceph_hfi_native.link.session.poisoned || ceph_hfi_native.link.session.busy ||
        ceph_hfi_native.owner.rpmh_token!=ceph_gx_native.owner.rpmh_token ||
        !ceph_oob_native.acquired || ceph_oob_native.failed || ceph_oob_native.release_attempted ||
        ceph_oob_native.txn.state!=CE_OOB_HELD || ceph_oob_native.txn.error ||
        ceph_oob_native.owner.rpmh_token!=ceph_gx_native.owner.rpmh_token?-1:0;
}
static int ceph_gx_native_lease(void)
{
    if(!ceph_gx_native.acquired || ceph_gx_native.failed)return -1;
    if(ceph_scm_info_context(NULL) || ceph_gx_native_record() ||
        ceph_hfi_native_access(NULL) || ceph_gx_native.owner.validate(ceph_gx_native.owner.ctx) ||
        ceph_gx_native_record() || ceph_hfi_native_access(NULL)) {
        ceph_gx_native.failed=1;return -1;
    }
    return 0;
}
static int ceph_gx_native_bus(void)
{
    if(ceph_gx_native_lease())return -1;
    int e;
    if(ceph_gx_native.sp_clock_off) {
        uint32_t state,fence;
        /* Only the source HWCG interval permits SP_CLK_OFF. SPT power,
         * GX power/clock and fence still gate each individual access. */
        e=ceph_gmu_mmio_load(&ceph_hfi_native.gmu,0x1f8d0,&state) || (state&0xc4u) ||
            ceph_gmu_mmio_load(&ceph_hfi_native.gmu,0x23b10,&fence) || fence;
    }else e=ceph_gmu_mmio_gx_access(&ceph_hfi_native.gmu);
    if(e || ceph_gx_native_lease()) {
        ceph_gx_native.failed=1;return -1;
    }
    return 0;
}
static int ceph_gx_native_load(uint32_t word,uint32_t *out)
{
    if(!out || !ceph_gx_native.gpu || word>=0x10000 || ceph_gx_native_bus())return -1;
    ceph_gmu_mmio_barrier();uint32_t value=ceph_gx_native.gpu[word];ceph_gmu_mmio_barrier();
    if(ceph_gx_native_bus() || value==UINT32_MAX) {ceph_gx_native.failed=1;return -1;}
    *out=value;return 0;
}
static int ceph_gx_native_window(void)
{
    uint32_t halt,fault;
    if(ceph_gx_native_load(0x808,&halt) || !(halt&2u) ||
        ceph_gx_native_load(0x823,&fault) || fault) {
        ceph_gx_native.failed=1;return -1;
    }
    return ceph_gx_native_bus();
}
static int ceph_gx_native_gpu_store(uint32_t word,uint32_t value)
{
    uint32_t actual;
    if(word>=0x10000 || ceph_gx_native_window())return -1;
    ++ceph_gx_native.result.writes;
    ceph_gmu_mmio_barrier();ceph_gx_native.gpu[word]=value;ceph_gmu_mmio_barrier();
    return ceph_gx_native_window() || ceph_gx_native_load(word,&actual) ||
        actual!=value || ceph_gx_native_window()?-1:0;
}
static int ceph_gx_native_gmu_load(uint32_t word,uint32_t *out)
{
    uint32_t value;
    if(!out || ceph_gx_native_window() || ceph_gmu_mmio_load(&ceph_hfi_native.gmu,word,&value) ||
        ceph_gx_native_window())return -1;
    *out=value;return 0;
}
static int ceph_gx_native_gmu_store(uint32_t word,uint32_t value)
{
    uint32_t actual;
    if(ceph_gx_native_window())return -1;
    ++ceph_gx_native.result.writes;++ceph_gx_native.result.hwcg_writes;
    return ceph_gmu_mmio_store(&ceph_hfi_native.gmu,word,value) || ceph_gx_native_window() ||
        ceph_gx_native_gmu_load(word,&actual) || actual!=value?-1:0;
}
static int ceph_gx_native_hwcg(const struct ceph_gx_core_input *input)
{
    struct ceph_gx_hwcg_plan p;uint32_t top,control;
    if(ceph_gx_hwcg_plan(input,&p) ||
        ceph_gx_native_gmu_store(0x23b09,p.gmu_cgc_mode) ||
        ceph_gx_native_gmu_store(0x23b0a,p.gmu_cgc_delay) ||
        ceph_gx_native_gmu_store(0x23b0b,p.gmu_cgc_hyst) ||
        ceph_gx_native_load(0xae,&top))return -1;
    if(top==p.top_clock_cntl){ceph_gx_native.result.hwcg_skipped=1;return 0;}
    if(ceph_gx_native_gmu_load(0x1a880,&control))return -1;
    ceph_gx_native.sp_clock_off=1;
    if(ceph_gx_native_gmu_store(0x1a880,control&~1u))return -1;
    for(unsigned n=0;n<CE_HWCG_REGS;++n) {
        unsigned before=ceph_gx_native.result.writes;
        int e=ceph_gx_native_gpu_store(p.regs[n].word,p.regs[n].value);
        ceph_gx_native.result.hwcg_writes+=ceph_gx_native.result.writes-before;
        if(e)return -1;
    }
    if(ceph_gx_native_gmu_load(0x1a880,&control) ||
        ceph_gx_native_gmu_store(0x1a880,control|1u))return -1;
    ceph_gx_native.sp_clock_off=0;
    unsigned before=ceph_gx_native.result.writes;
    int e=ceph_gx_native_gpu_store(0xae,p.top_clock_cntl);
    ceph_gx_native.result.hwcg_writes+=ceph_gx_native.result.writes-before;
    return e;
}
static int ceph_gx_native_gbif(const struct ceph_gx_core_input *input)
{
    struct ceph_gx_gbif_plan p;
    if(ceph_gx_gbif_plan(input,&p))return -1;
    for(unsigned n=0;n<5;++n) {
        unsigned before=ceph_gx_native.result.writes;
        int e=ceph_gx_native_gpu_store(p.writes[n].word,p.writes[n].value);
        ceph_gx_native.result.gbif_writes+=ceph_gx_native.result.writes-before;
        if(e)return -1;
    }
    return 0;
}
static int ceph_gx_native_configure(const struct ceph_gx_native_owner *owner,struct ceph_gx_native_result *out,unsigned setup)
{
    if(!owner || !owner->begin || !owner->validate || !owner->finish || !out ||
        ceph_scm_info_context(NULL))return -1;
    if(__atomic_exchange_n(&ceph_gpll0_lock,1,__ATOMIC_ACQUIRE))return -1;
    int e=-1;
    if(ceph_gx_native.attempted)goto done;
    ceph_gx_native.owner=*owner;
    if(ceph_gx_native_record() || cepheus_gpu_rpmh_check(owner->rpmh_token))goto done;
    const struct ceph_gx_core_input input={0x06040001,0x100000,0x100000,15,32,3};
    if(ceph_gx_core_plan(&input,&ceph_gx_native.plan))goto done;
    ceph_gx_native.attempted=1;
    ceph_gx_native.result.setup=setup;
    e=ceph_gx_native.owner.begin(ceph_gx_native.owner.ctx,&ceph_hfi_native.publication,&input);
    if(e)goto publish;
    ceph_gx_native.acquired=1;ceph_gx_native.result.retained=1;
    if(ceph_gx_native_bus()) {e=-1;goto finish;}
    if(cepheus_gpu_rpmh_finish(ceph_gx_native.owner.rpmh_token,1) || ceph_gx_native_bus()) {e=-1;goto finish;}
    ceph_gx_native.gpu=(volatile uint32_t *)ml_io_map(0x2c00000,0x40000);
    if(ceph_gx_native_bus() || !ceph_gx_native.gpu || ((uintptr_t)ceph_gx_native.gpu&3u) ||
        (uintptr_t)ceph_gx_native.gpu>UINTPTR_MAX-0x40000) {e=-1;goto finish;}
    if(setup && (ceph_gx_native_hwcg(&input) || ceph_gx_native_gbif(&input))) {e=-1;goto finish;}
    for(unsigned n=0;n<23;++n) {
        const struct ceph_gx_write *w=&ceph_gx_native.plan.writes[n];
        if(ceph_gx_native_gpu_store(w->word,w->value)) {e=-1;goto finish;}
    }
    e=ceph_gx_native_window();
finish:
    ceph_gx_native.owner.finish(ceph_gx_native.owner.ctx,ceph_gx_native.result.writes,
        ceph_gx_native.result.retained);
    if(!e && ceph_gx_native_window())e=-1;
    if(e) {
        ceph_gx_native.failed=1;
        (void)ceph_memory_hfi_finish(&ceph_hfi_native.publication,CE_MEMORY_HFI_UNCERTAIN);
    }else ceph_gx_native.result.configured=1;
publish:
    ceph_gx_native.result.error=e;*out=ceph_gx_native.result;
done:
    __atomic_store_n(&ceph_gpll0_lock,0,__ATOMIC_RELEASE);return e;
}
int cepheus_gpu_gx_core_native(const struct ceph_gx_native_owner *owner,struct ceph_gx_native_result *out)
{return ceph_gx_native_configure(owner,out,0);}
int cepheus_gpu_gx_setup_native(const struct ceph_gx_native_owner *owner,struct ceph_gx_native_result *out)
{return ceph_gx_native_configure(owner,out,1);}
#endif
