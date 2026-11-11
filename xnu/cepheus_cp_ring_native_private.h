/* Included after actual CP and enabled GX SMMU records in the SWEET unit. */
#ifndef CEPHEUS_CP_RING_NATIVE_PRIVATE_H
#define CEPHEUS_CP_RING_NATIVE_PRIVATE_H
#include "cepheus_cp_ring_native.h"
#include "cepheus_cp_ring_config.h"
static struct {
    struct ceph_cp_ring_native_owner owner;
    struct ceph_cp_ring_plan plan;
    struct ceph_cp_ring_native_result result;
    unsigned attempted,acquired,failed;
} ceph_cp_ring_native;
static int ceph_cp_ring_native_record(void)
{
    return !ceph_gx_smmu_native.acquired || !ceph_gx_smmu_native.finished ||
        ceph_gx_smmu_native.failed || ceph_gx_smmu_native.uncertain || ceph_gx_smmu_native.pinned ||
        ceph_gx_smmu_native.txn.state!=CE_SM_ENABLED || ceph_gx_smmu_native.txn.effects!=15 ||
        ceph_gx_smmu_native.txn.error || !ceph_gx_smmu_plan_canonical(&ceph_gx_smmu_native.plan) ||
        ceph_gx_smmu_native.owner.rpmh_token!=ceph_cp_ring_native.owner.rpmh_token ||
        ceph_cp_native_provenance(&ceph_gx_smmu_native.publication) ||
        ceph_memory_cp_installed(&ceph_gx_smmu_native.publication)?-1:0;
}
static int ceph_cp_ring_native_live(void)
{
    if(!ceph_cp_ring_native.acquired || ceph_cp_ring_native.failed)return -1;
    if(ceph_scm_info_context(NULL) || ceph_cp_ring_native_record() ||
        ceph_gx_smmu_native_access(NULL) || ceph_cp_ring_native.owner.validate(ceph_cp_ring_native.owner.ctx) ||
        ceph_cp_ring_native_record() || ceph_gx_smmu_native_access(NULL)) {
        ceph_cp_ring_native.failed=1;return -1;
    }
    return 0;
}
static int ceph_cp_ring_native_context(void)
{
    const struct ceph_smmu_plan *p=&ceph_gx_smmu_native.plan.context;
    const enum ceph_smmu_reg regs[]={CE_SM_SMR,CE_SM_S2CR,CE_SM_CBAR,CE_SM_CBA2R,CE_SM_SCTLR,
        CE_SM_ACTLR,CE_SM_TCR2,CE_SM_TTBR0,CE_SM_TTBR1,CE_SM_TCR,CE_SM_MAIR0,CE_SM_MAIR1,
        CE_SM_FSR,CE_SM_TLBSTATUS};
    const uint64_t expected[]={p->smr,p->s2cr_translate,p->cbar,p->cba2r,p->sctlr,
        ceph_gx_smmu_native.plan.actlr,p->tcr2,p->ttbr0,p->ttbr1,p->tcr,p->mair0,p->mair1,0,0};
    for(unsigned n=0;n<14;++n) {
        uint64_t value;
        if(ceph_cp_ring_native_live() ||
            ceph_gx_smmu_mmio_read(&ceph_gx_smmu_native.mmio,regs[n],&value) ||
            ceph_cp_ring_native_live() || value!=expected[n])return -1;
    }
    return 0;
}
static int ceph_cp_ring_native_access(void)
{
    if(ceph_cp_ring_native_live() || ceph_cp_ring_native_context() || ceph_cp_ring_native_live()) {
        ceph_cp_ring_native.failed=1;return -1;
    }
    return 0;
}
static int ceph_cp_ring_native_load(uint32_t word,uint32_t *out)
{
    if(!out || word>=0x10000 || ceph_cp_ring_native_live())return -1;
    ceph_gmu_mmio_barrier();uint32_t value=ceph_gx_native.gpu[word];ceph_gmu_mmio_barrier();
    if(ceph_cp_ring_native_live())return -1;
    *out=value;return 0;
}
static int ceph_cp_ring_native_protection(void)
{
    for(unsigned n=0;n<45;++n) {
        uint32_t word,value,actual;
        if(ceph_cp_protection_word(n,&word,&value) || ceph_cp_ring_native_load(word,&actual) || actual!=value)return -1;
    }
    return 0;
}
static int ceph_cp_ring_native_readback(void)
{
    for(unsigned n=0;n<7;++n) {
        uint32_t value;
        if(ceph_cp_ring_native_load(ceph_cp_ring_native.plan.writes[n].word,&value) ||
            value!=ceph_cp_ring_native.plan.writes[n].value)return -1;
    }
    uint32_t rptr,wptr;
    return ceph_cp_ring_native_load(0x806,&rptr) || ceph_cp_ring_native_load(0x807,&wptr) || rptr || wptr?-1:0;
}
static int ceph_cp_ring_native_output(const struct ceph_cp_ring_native_result *out,
    const struct ceph_cp_ring_native_owner *owner)
{
    uintptr_t dst=(uintptr_t)out;
    const volatile void *bases[]={ceph_gx_native.gpu,ceph_hfi_native.gmu.base,
        ceph_hfi_native.power.gcc.base,ceph_hfi_native.power.cc.base,ceph_hfi_native.smmu.base,
        ceph_gx_smmu_native.mmio.base,&ceph_cp_ring_native,&ceph_cp_native,&ceph_gx_smmu_native,
        &ceph_gx_native,&ceph_hfi_native,owner};
    const size_t bytes[]={0x40000,0x30000,0x90000,0x9000,0x10000,0x10000,
        sizeof(ceph_cp_ring_native),sizeof(ceph_cp_native),sizeof(ceph_gx_smmu_native),
        sizeof(ceph_gx_native),sizeof(ceph_hfi_native),sizeof(*owner)};
    if(!dst || (dst&7u) || dst>UINTPTR_MAX-sizeof(*out) || ceph_memory_cp_output_safe(out,sizeof(*out)))return -1;
    for(unsigned n=0;n<12;++n) {
        uintptr_t base=(uintptr_t)bases[n];
        if(base && (base>UINTPTR_MAX-bytes[n] || (dst<base+bytes[n] && base<dst+sizeof(*out))))return -1;
    }
    return 0;
}
int cepheus_gpu_cp_ring_native(const struct ceph_cp_ring_native_owner *owner,struct ceph_cp_ring_native_result *out)
{
    if(!owner || !owner->begin || !owner->validate || !owner->finish || !out || ceph_scm_info_context(NULL))return -1;
    if(__atomic_exchange_n(&ceph_gpll0_lock,1,__ATOMIC_ACQUIRE))return -1;
    int e=-1;
    if(ceph_cp_ring_native.attempted)goto done;
    ceph_cp_ring_native.owner=*owner;
    if(ceph_cp_ring_native_record() || cepheus_gpu_rpmh_check(owner->rpmh_token) ||
        ceph_cp_ring_native_output(out,owner))goto done;
    const struct ceph_vm_region *r=ceph_gx_smmu_native.publication.regions;
    const struct ceph_cp_ring_input in={r[CE_CP_RING].va,r[CE_CP_RPTR].va,r[CE_CP_SQE].va,
        r[CE_CP_RING].bytes,r[CE_CP_RPTR].bytes,r[CE_CP_SQE].bytes};
    if(ceph_cp_ring_plan(&in,&ceph_cp_ring_native.plan))goto done;
    ceph_cp_ring_native.attempted=1;
    if(ceph_cp_ring_native.owner.begin(ceph_cp_ring_native.owner.ctx,&ceph_gx_smmu_native.publication))goto done;
    ceph_cp_ring_native.acquired=1;
    if(ceph_cp_ring_native_access() || ceph_cp_ring_native_protection())goto finish;
    /* Halted inherited pointers/addresses still belong to the prior context. */
    static const uint32_t empty[]={0x800,0x801,0x804,0x805,0x806,0x807,0x830,0x831};
    for(unsigned n=0;n<8;++n) {
        uint32_t value;
        if(ceph_cp_ring_native_load(empty[n],&value) || value)goto finish;
    }
    if(ceph_cp_ring_native_access() || ceph_memory_cp_ring_publish(&ceph_gx_smmu_native.publication) ||
        ceph_cp_ring_native_access())goto finish;
    for(unsigned n=0;n<7;++n) {
        const struct ceph_cp_ring_write *w=&ceph_cp_ring_native.plan.writes[n];uint32_t actual;
        if(ceph_cp_ring_native_access())goto finish;
        ++ceph_cp_ring_native.result.writes;
        ceph_gmu_mmio_barrier();ceph_gx_native.gpu[w->word]=w->value;ceph_gmu_mmio_barrier();
        if(ceph_cp_ring_native_access() || ceph_cp_ring_native_load(w->word,&actual) || actual!=w->value)goto finish;
    }
    if(ceph_cp_ring_native_protection() || ceph_cp_ring_native_readback())goto finish;
    e=0;
finish:
    ceph_cp_ring_native.owner.finish(ceph_cp_ring_native.owner.ctx,ceph_cp_ring_native.result.writes,1);
    if(!e && (ceph_cp_ring_native_access() || ceph_cp_ring_native_protection() ||
        ceph_cp_ring_native_readback() || ceph_memory_cp_ring_publish(&ceph_gx_smmu_native.publication) ||
        ceph_cp_ring_native_access() || ceph_cp_ring_native_output(out,owner)))e=-1;
    if(e) {
        ceph_cp_ring_native.failed=1;
        /* The SMMU already exposes these buffers even when no ring store ran. */
        (void)ceph_memory_cp_finish(&ceph_gx_smmu_native.publication,CE_MEMORY_CP_UNCERTAIN);
        (void)ceph_memory_hfi_finish(&ceph_hfi_native.publication,CE_MEMORY_HFI_UNCERTAIN);
    }else {ceph_cp_ring_native.result.configured=1;*out=ceph_cp_ring_native.result;}
done:
    __atomic_store_n(&ceph_gpll0_lock,0,__ATOMIC_RELEASE);return e;
}
#endif
