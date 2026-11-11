/* Included after actual native GX/HFI/OOB records in the SWEET unit. */
#ifndef CEPHEUS_CP_NATIVE_PRIVATE_H
#define CEPHEUS_CP_NATIVE_PRIVATE_H
#include "cepheus_cp_native.h"
#include "cepheus_cp_protection.h"
static struct {
    struct ceph_cp_native_owner owner;
    struct ceph_cp_workspace workspace;
    struct ceph_gx_smmu_plan plan;
    struct ceph_cp_native_result result;
    uint32_t snapshot[CE_CP_REGLIST_WORDS];
    unsigned attempted,acquired,pinned,failed,sealed;
} ceph_cp_native;
static int ceph_cp_native_record(void)
{
    return !ceph_gx_native.acquired || ceph_gx_native.failed || !ceph_gx_native.result.configured ||
        !ceph_gx_native.result.setup || !ceph_gx_native.result.perfcounter || ceph_gx_native.result.error ||
        ceph_gx_native.sp_clock_off || !ceph_gx_native.gpu ||
        ceph_gx_native.owner.rpmh_token!=ceph_cp_native.owner.rpmh_token || ceph_gx_native_record()?-1:0;
}
static int ceph_cp_native_access(void)
{
    if(!ceph_cp_native.acquired || ceph_cp_native.failed)return -1;
    if(ceph_scm_info_context(NULL) || ceph_cp_native_record() || ceph_gx_native_window() ||
        ceph_cp_native.owner.validate(ceph_cp_native.owner.ctx) ||
        ceph_cp_native_record() || ceph_gx_native_window() ||
        (ceph_cp_native.pinned && ceph_memory_cp_build_validate(&ceph_cp_native.workspace)) ||
        (ceph_cp_native.sealed && ceph_memory_cp_validate(&ceph_cp_native.result.publication))) {
        ceph_cp_native.failed=1;return -1;
    }
    return 0;
}
static int ceph_cp_native_load(uint32_t word,uint32_t *out)
{
    if(!out || word>=0x10000 || ceph_cp_native_access())return -1;
    ceph_gmu_mmio_barrier();uint32_t value=ceph_gx_native.gpu[word];ceph_gmu_mmio_barrier();
    if(ceph_cp_native_access())return -1;
    *out=value;return 0;
}
static int ceph_cp_native_store(uint32_t word,uint32_t value)
{
    uint32_t actual;
    if(word>=0x10000 || ceph_cp_native_access())return -1;
    ++ceph_cp_native.result.writes;
    ceph_gmu_mmio_barrier();ceph_gx_native.gpu[word]=value;ceph_gmu_mmio_barrier();
    if(ceph_cp_native_access() || ceph_cp_native_load(word,&actual) || actual!=value || ceph_cp_native_access()) {
        ceph_cp_native.failed=1;return -1;
    }
    return 0;
}
static int ceph_cp_native_protection(void)
{
    for(unsigned n=0;n<45;++n) {
        uint32_t word,value;
        if(ceph_cp_protection_word(n,&word,&value) || ceph_cp_native_store(word,value))return -1;
    }
    return 0;
}
static int ceph_cp_native_capture(void)
{
    /* Recheck the complete configured set, including address modes that are
     * not present in the vendor powerup list. */
    for(unsigned n=0;n<45;++n) {
        uint32_t word,value,actual;
        if(ceph_cp_protection_word(n,&word,&value) || ceph_cp_native_load(word,&actual) || actual!=value)return -1;
    }
    ceph_cp_native.snapshot[3]=132u|(76u<<16);
    for(unsigned n=0;n<66;++n) {
        uint32_t value;
        if(ceph_cp_native_load(ceph_cp_restore_offsets[n],&value))return -1;
        if(n==4 && (value&11u)!=11u)return -1;
        if(n>=5 && n<37) {
            uint32_t expected;
            if(ceph_cp_protect_word(n-5,&expected) || value!=expected)return -1;
        }
        if(n>=38 && n<48 && !(value&1u))return -1;
        ceph_cp_native.snapshot[4+2*n]=ceph_cp_restore_offsets[n];
        ceph_cp_native.snapshot[5+2*n]=value;++ceph_cp_native.result.reads;
    }
    return ceph_cp_native_access();
}
static int ceph_cp_native_restore_check(void)
{
    if(ceph_cp_native_access())return -1;
    const volatile uint32_t *target=(const volatile uint32_t *)ceph_cp_native.workspace.views[CE_CP_RESTORE].cpu;
    for(unsigned n=0;n<CE_CP_REGLIST_WORDS;++n)if(target[n]!=ceph_cp_native.snapshot[n])return -1;
    const volatile uint8_t *bytes=(const volatile uint8_t *)target;
    for(size_t n=CE_CP_REGLIST_WORDS*4;n<ceph_cp_native.workspace.views[CE_CP_RESTORE].bytes;++n)
        if(bytes[n])return -1;
    return 0;
}
static int ceph_cp_native_provenance(const struct ceph_cp_publication *p)
{
    if(!p || !ceph_cp_native.sealed || ceph_cp_native.failed || ceph_cp_native.result.writes!=45 ||
        ceph_cp_native.result.reads!=66 || ceph_cp_native_restore_check())return -1;
    const struct ceph_cp_publication *saved=&ceph_cp_native.result.publication;
    const struct ceph_gpu_publication *a=&p->memory,*b=&saved->memory;
    if(a->group!=b->group || a->ttbr0!=b->ttbr0 || a->ttbr1!=b->ttbr1 || a->smr!=b->smr ||
        a->cb!=b->cb || a->slot!=b->slot || a->asid!=b->asid || p->table_used!=saved->table_used)return -1;
    for(unsigned n=0;n<CE_CP_BUFFERS;++n)if(a->handles[n]!=b->handles[n])return -1;
    for(unsigned n=0;n<CE_CP_ROOT;++n) {
        const struct ceph_vm_region *r=&p->regions[n],*s=&saved->regions[n];
        if(r->va!=s->va || r->pa!=s->pa || r->bytes!=s->bytes || r->permissions!=s->permissions)return -1;
    }
    for(unsigned n=0;n<32;++n)if(p->restore_sha256[n]!=saved->restore_sha256[n])return -1;
    return 0;
}
static int ceph_cp_native_output(const struct ceph_cp_native_result *out)
{
    uintptr_t dst=(uintptr_t)out;
    const volatile void *bases[]={ceph_gx_native.gpu,ceph_hfi_native.gmu.base,
        ceph_hfi_native.power.gcc.base,ceph_hfi_native.power.cc.base,ceph_hfi_native.smmu.base,
        &ceph_cp_native,&ceph_gx_native,&ceph_hfi_native};
    const size_t bytes[]={0x40000,0x30000,0x90000,0x9000,0x10000,
        sizeof(ceph_cp_native),sizeof(ceph_gx_native),sizeof(ceph_hfi_native)};
    if(!dst || (dst&7u) || dst>UINTPTR_MAX-sizeof(*out) || ceph_memory_cp_output_safe(out,sizeof(*out)))return -1;
    for(unsigned i=0;i<8;++i) {
        uintptr_t base=(uintptr_t)bases[i];
        if(base && (base>UINTPTR_MAX-bytes[i] || (dst<base+bytes[i] && base<dst+sizeof(*out))))return -1;
    }
    return 0;
}
int cepheus_gpu_cp_prepare_native(const struct ceph_cp_native_owner *owner,const struct ceph_cp_workspace *w,
    const struct ceph_gx_smmu_plan *plan,struct ceph_cp_native_result *out)
{
    if(!owner || !owner->begin || !owner->validate || !owner->finish || !w || !out ||
        !ceph_gx_smmu_plan_canonical(plan) ||
        plan->context.ttbr0!=(w->table.pa|((uint64_t)plan->context.asid<<48)) ||
        ceph_scm_info_context(NULL))return -1;
    if(__atomic_exchange_n(&ceph_gpll0_lock,1,__ATOMIC_ACQUIRE))return -1;
    int e=-1;
    if(ceph_cp_native.attempted)goto done;
    ceph_cp_native.owner=*owner;
    if(ceph_cp_native_record() || cepheus_gpu_rpmh_check(owner->rpmh_token) || ceph_cp_native_output(out) ||
        ceph_cp_memory_output(w->views,out,sizeof(*out),owner,sizeof(*owner)) ||
        ceph_cp_memory_output(w->views,out,sizeof(*out),w,sizeof(*w)) ||
        ceph_cp_memory_output(w->views,out,sizeof(*out),plan,sizeof(*plan)))goto done;
    ceph_cp_native.workspace=*w;ceph_cp_native.plan=*plan;ceph_cp_native.attempted=1;
    if(ceph_cp_native.owner.begin(ceph_cp_native.owner.ctx,&ceph_cp_native.workspace,&ceph_cp_native.plan))goto done;
    ceph_cp_native.acquired=1;
    if(ceph_cp_native_access() || ceph_memory_cp_build_acquire(&ceph_cp_native.workspace))goto finish;
    ceph_cp_native.pinned=1;
    if(ceph_cp_native_access() || ceph_cp_native_protection() || ceph_cp_native_capture())goto finish;
    e=0;
finish:
    ceph_cp_native.owner.finish(ceph_cp_native.owner.ctx,ceph_cp_native.result.writes,1);
    if(!e && ceph_cp_native_access())e=-1;
    if(!e) {
        const struct ceph_vm_region *r=&ceph_cp_native.workspace.regions[CE_CP_RESTORE];
        if(ceph_cp_me_init(ceph_cp_native.result.packet,12,r->va,r->bytes)!=12)e=-1;
    }
    if(!e) {
        volatile uint32_t *target=(volatile uint32_t *)ceph_cp_native.workspace.views[CE_CP_RESTORE].cpu;
        for(unsigned n=0;n<CE_CP_REGLIST_WORDS;++n)target[n]=ceph_cp_native.snapshot[n];
        if(ceph_cp_native_restore_check() || ceph_memory_cp_build_seal(&ceph_cp_native.workspace,
            &ceph_cp_native.plan,&ceph_cp_native.result.publication))e=-1;
        else {ceph_cp_native.pinned=0;ceph_cp_native.sealed=1;}
    }
    if(!e && (ceph_cp_native_restore_check() || ceph_cp_native_output(out)))e=-1;
    if(e) {
        ceph_cp_native.failed=1;
        if(ceph_cp_native.pinned &&
            ceph_memory_cp_build_finish(&ceph_cp_native.workspace,ceph_cp_native.result.writes!=0))
            (void)ceph_memory_cp_build_finish(&ceph_cp_native.workspace,1);
        if(ceph_cp_native.sealed)
            (void)ceph_memory_cp_finish(&ceph_cp_native.result.publication,CE_MEMORY_CP_UNCERTAIN);
        if(ceph_cp_native.result.writes)
            (void)ceph_memory_hfi_finish(&ceph_hfi_native.publication,CE_MEMORY_HFI_UNCERTAIN);
    }else *out=ceph_cp_native.result;
done:
    __atomic_store_n(&ceph_gpll0_lock,0,__ATOMIC_RELEASE);return e;
}
#endif
