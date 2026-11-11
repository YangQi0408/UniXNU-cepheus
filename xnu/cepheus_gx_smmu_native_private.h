/* Included after actual native GX/HFI/OOB records in the SWEET unit. */
#ifndef CEPHEUS_GX_SMMU_NATIVE_PRIVATE_H
#define CEPHEUS_GX_SMMU_NATIVE_PRIVATE_H
#include "cepheus_gx_smmu_native.h"
static struct {
    struct ceph_gx_smmu_native_owner owner;
    struct ceph_gx_smmu_plan plan;
    struct ceph_cp_memory_view view;
    struct ceph_cp_publication publication;
    struct ceph_smmu_mmio mmio;
    struct ceph_smmu_install txn;
    struct ceph_smmu_install *output;
    unsigned attempted,acquired,pinned,failed,finished,output_safe,uncertain;
} ceph_gx_smmu_native;
static int ceph_gx_smmu_native_record(void)
{
    return !ceph_gx_native.attempted || !ceph_gx_native.acquired || ceph_gx_native.failed ||
        !ceph_gx_native.result.configured || !ceph_gx_native.result.setup ||
        ceph_gx_native.result.error || ceph_gx_native.sp_clock_off ||
        ceph_gx_native.owner.rpmh_token!=ceph_gx_smmu_native.owner.rpmh_token ||
        ceph_gx_native_record()?-1:0;
}
static int ceph_gx_smmu_native_access(void *unused)
{
    (void)unused;
    if(!ceph_gx_smmu_native.acquired || ceph_gx_smmu_native.failed)return -1;
    if(ceph_scm_info_context(NULL) || ceph_gx_smmu_native_record() ||
        ceph_gx_native_window() ||
        ceph_gx_smmu_native.owner.validate(ceph_gx_smmu_native.owner.ctx) ||
        ceph_gx_smmu_native_record() || ceph_gx_native_window() ||
        (ceph_gx_smmu_native.pinned && ceph_memory_cp_validate(&ceph_gx_smmu_native.publication))) {
        ceph_gx_smmu_native.failed=1;return -1;
    }
    return 0;
}
static int ceph_gx_smmu_native_raw_read(uint32_t offset,uint32_t *out)
{
    volatile uint8_t *base=ceph_gx_smmu_native.mmio.base;
    if(!out || !base || ((uintptr_t)base&7u) || (uintptr_t)base>UINTPTR_MAX-0x10000 ||
        (offset&3u) || offset>0x10000-4 || ceph_gx_smmu_native_access(NULL))return -1;
    ceph_smmu_mmio_order();uint32_t value=*(volatile uint32_t *)(base+offset);ceph_smmu_mmio_order();
    if(ceph_gx_smmu_native_access(NULL) || value==UINT32_MAX)return -1;
    *out=value;return 0;
}
static int ceph_gx_smmu_native_output(void)
{
    uintptr_t dst=(uintptr_t)ceph_gx_smmu_native.output;
    const volatile void *bases[]={ceph_gx_native.gpu,ceph_hfi_native.gmu.base,
        ceph_hfi_native.power.gcc.base,ceph_hfi_native.power.cc.base,
        ceph_hfi_native.smmu.base,ceph_gx_smmu_native.mmio.base};
    const size_t bytes[]={0x40000,0x30000,0x90000,0x9000,0x10000,0x10000};
    if(dst>UINTPTR_MAX-sizeof(*ceph_gx_smmu_native.output))return -1;
    for(unsigned i=0;i<6;++i) {
        uintptr_t base=(uintptr_t)bases[i];
        if(base && (base>UINTPTR_MAX-bytes[i] ||
            (dst<base+bytes[i] && base<dst+sizeof(*ceph_gx_smmu_native.output))))return -1;
    }
    return 0;
}
static void ceph_gx_smmu_native_finish(void *unused,int keep)
{
    (void)unused;
    if(ceph_gx_smmu_native.txn.state==CE_SM_ENABLED && ceph_gx_smmu_native_access(NULL))keep=1;
    if(ceph_gx_smmu_native.failed && ceph_gx_smmu_native.txn.effects)keep=1;
    if(ceph_gx_smmu_native.pinned) {
        int result=(ceph_gx_smmu_native.failed && ceph_gx_smmu_native.txn.effects) ||
            (keep && ceph_gx_smmu_native.txn.state!=CE_SM_ENABLED)?
            CE_MEMORY_CP_UNCERTAIN:ceph_gx_smmu_native.txn.state==CE_SM_ENABLED?
            CE_MEMORY_CP_INSTALLED:CE_MEMORY_CP_REFUSED;
        if(result==CE_MEMORY_CP_UNCERTAIN)ceph_gx_smmu_native.uncertain=1;
        if(ceph_memory_cp_finish(&ceph_gx_smmu_native.publication,result)) {
            ceph_gx_smmu_native.failed=1;keep=1;
            ceph_gx_smmu_native.uncertain=1;
            (void)ceph_memory_cp_finish(&ceph_gx_smmu_native.publication,CE_MEMORY_CP_UNCERTAIN);
        }else ceph_gx_smmu_native.pinned=0;
    }
    ceph_gx_smmu_native.finished=1;
    ceph_gx_smmu_native.owner.finish(ceph_gx_smmu_native.owner.ctx,ceph_gx_smmu_native.txn.effects,1);
    if(ceph_gx_smmu_native.txn.state==CE_SM_ENABLED && !ceph_gx_smmu_native.failed &&
        (ceph_gx_smmu_native_access(NULL) || ceph_memory_cp_validate(&ceph_gx_smmu_native.publication))) {
        ceph_gx_smmu_native.failed=1;
        ceph_gx_smmu_native.uncertain=1;
        (void)ceph_memory_cp_finish(&ceph_gx_smmu_native.publication,CE_MEMORY_CP_UNCERTAIN);
    }
    if(ceph_gx_smmu_native.failed && (keep || ceph_gx_smmu_native.txn.effects || ceph_gx_smmu_native.pinned)) {
        ceph_gx_smmu_native.txn.state=CE_SM_QUARANTINED;ceph_gx_smmu_native.txn.error=-1;
    }
}
static int ceph_gx_smmu_native_begin(void *unused,const struct ceph_smmu_plan *plan,uint32_t actlr)
{
    (void)unused;(void)actlr;
    if(ceph_gx_smmu_native.owner.begin(ceph_gx_smmu_native.owner.ctx,&ceph_gx_smmu_native.plan,
        &ceph_gx_smmu_native.publication))return -1;
    ceph_gx_smmu_native.acquired=1;
    if(ceph_gx_smmu_native_access(NULL) ||
        ceph_memory_cp_acquire(&ceph_gx_smmu_native.publication,&ceph_gx_smmu_native.view))goto failed;
    ceph_gx_smmu_native.pinned=1;
    if(ceph_cp_memory_output(ceph_gx_smmu_native.view.views,ceph_gx_smmu_native.output,
        sizeof(*ceph_gx_smmu_native.output),&ceph_gx_smmu_native,sizeof(ceph_gx_smmu_native)))goto failed;
    ceph_gx_smmu_native.output_safe=1;
    if(ceph_gx_smmu_native_access(NULL))goto failed;
    uintptr_t base=ml_io_map(0x2ca0000,0x10000);
    if(ceph_gx_smmu_native_access(NULL) || !base || (base&7u) || base>UINTPTR_MAX-0x10000)goto failed;
    ceph_gx_smmu_native.mmio.base=(volatile uint8_t *)base;
    if(ceph_gx_smmu_native_output()){ceph_gx_smmu_native.output_safe=0;goto failed;}
    ceph_gx_smmu_native.mmio.mapped_bytes=0x10000;
    ceph_gx_smmu_native.mmio.plan=*plan;ceph_gx_smmu_native.mmio.actlr=0x303;
    ceph_gx_smmu_native.mmio.lease=ceph_gx_smmu_native_access;
    uint32_t id[3];
    for(unsigned i=0;i<3;++i)if(ceph_gx_smmu_native_raw_read(0x20+4*i,&id[i]))goto failed;
    struct ceph_smmu_geometry *g=&ceph_gx_smmu_native.mmio.geometry;
    if(ceph_smmu_geometry(id[0],id[1],id[2],0x10000,g))goto failed;
    struct ceph_smmu_stream streams[128];
    for(unsigned i=0;i<g->groups;++i)
        if(ceph_gx_smmu_native_raw_read(0x800+4*i,&streams[i].smr) ||
            ceph_gx_smmu_native_raw_read(0xc00+4*i,&streams[i].s2cr))goto failed;
    uint64_t sctlr;struct ceph_gx_smmu_plan observed;
    if(ceph_gx_smmu_mmio_read(&ceph_gx_smmu_native.mmio,CE_SM_SCTLR,&sctlr) ||
        ceph_gx_smmu_plan(streams,g->groups,g,plan->slot,plan->cb,plan->asid,
            plan->ttbr0&0xffffffffffULL,(uint32_t)sctlr,&observed) ||
        ceph_gx_smmu_native_access(NULL))goto failed;
    return 0;
failed:
    ceph_gx_smmu_native.failed=1;ceph_gx_smmu_native_finish(NULL,0);return -1;
}
static int ceph_gx_smmu_native_read(void *unused,enum ceph_smmu_reg reg,uint64_t *out)
{
    (void)unused;int e=ceph_gx_smmu_mmio_read(&ceph_gx_smmu_native.mmio,reg,out);
    if(e)ceph_gx_smmu_native.failed=1;
    return e;
}
static int ceph_gx_smmu_native_write(void *unused,enum ceph_smmu_reg reg,uint64_t value)
{
    (void)unused;int e=ceph_gx_smmu_mmio_write(&ceph_gx_smmu_native.mmio,reg,value);
    if(e)ceph_gx_smmu_native.failed=1;
    return e;
}
static int ceph_gx_smmu_native_publish(void *unused)
{
    if(ceph_gx_smmu_native_access(unused) || ceph_memory_cp_publish(&ceph_gx_smmu_native.publication) ||
        ceph_gx_smmu_native_access(unused)) {ceph_gx_smmu_native.failed=1;return -1;}
    return 0;
}
static int ceph_gx_smmu_native_barrier(void *unused)
{
    if(ceph_gx_smmu_native_access(unused))return -1;
    ceph_smmu_mmio_order();return ceph_gx_smmu_native_access(unused);
}
static int ceph_gx_smmu_native_delay(void *unused,unsigned us)
{
    uint64_t before,after;
    if(us!=1 || ceph_gx_smmu_native_access(unused))return -1;
    absolutetime_to_nanoseconds(mach_absolute_time(),&before);
    if(ceph_gx_smmu_native_access(unused))return -1;
    delay(us);
    if(ceph_gx_smmu_native_access(unused))return -1;
    absolutetime_to_nanoseconds(mach_absolute_time(),&after);
    if(ceph_gx_smmu_native_access(unused) || after<before || after-before<1000 || after-before>100000) {
        ceph_gx_smmu_native.failed=1;return -1;
    }
    return 0;
}
int cepheus_gpu_gx_smmu_install_native(const struct ceph_gx_smmu_native_owner *owner,
    const struct ceph_gx_smmu_plan *plan,const struct ceph_cp_publication *publication,struct ceph_smmu_install *out)
{
    if(!owner || !owner->begin || !owner->validate || !owner->finish || !out || ((uintptr_t)out&7u) ||
        !ceph_gx_smmu_plan_canonical(plan) || !publication || !publication->memory.group ||
        plan->context.ttbr0!=publication->memory.ttbr0 || plan->context.ttbr1!=publication->memory.ttbr1 ||
        plan->context.smr!=publication->memory.smr || plan->context.cb!=publication->memory.cb ||
        plan->context.slot!=publication->memory.slot || plan->context.asid!=publication->memory.asid ||
        ceph_scm_info_context(NULL))return -1;
    if(__atomic_exchange_n(&ceph_gpll0_lock,1,__ATOMIC_ACQUIRE))return -1;
    int e=-1;
    if(ceph_gx_smmu_native.attempted)goto done;
    ceph_gx_smmu_native.owner=*owner;
    if(ceph_gx_smmu_native_record() || cepheus_gpu_rpmh_check(owner->rpmh_token) ||
        ceph_memory_cp_validate(publication))goto done;
    uintptr_t dst=(uintptr_t)out;
    const void *identities[]={owner,plan,publication,&ceph_gx_smmu_native};
    const size_t bytes[]={sizeof(*owner),sizeof(*plan),sizeof(*publication),sizeof(ceph_gx_smmu_native)};
    if(dst>UINTPTR_MAX-sizeof(*out))goto done;
    for(unsigned i=0;i<4;++i) {
        uintptr_t base=(uintptr_t)identities[i];
        if(base>UINTPTR_MAX-bytes[i] || (dst<base+bytes[i] && base<dst+sizeof(*out)))goto done;
    }
    ceph_gx_smmu_native.plan=*plan;ceph_gx_smmu_native.publication=*publication;ceph_gx_smmu_native.output=out;
    if(ceph_gx_smmu_native_output())goto done;
    ceph_gx_smmu_native.attempted=1;
    struct ceph_smmu_ops ops={NULL,ceph_gx_smmu_native_begin,ceph_gx_smmu_native_access,
        ceph_gx_smmu_native_read,ceph_gx_smmu_native_write,ceph_gx_smmu_native_publish,
        ceph_gx_smmu_native_barrier,ceph_gx_smmu_native_delay,ceph_gx_smmu_native_finish};
    e=ceph_gx_smmu_install_context(&ceph_gx_smmu_native.txn,&ops,&ceph_gx_smmu_native.plan);
    if(ceph_gx_smmu_native.uncertain)ceph_gx_smmu_native.txn.state=CE_SM_QUARANTINED;
    if(ceph_gx_smmu_native.pinned || ceph_gx_smmu_native.txn.error) {
        e=-1;
        if(ceph_gx_smmu_native.pinned)ceph_gx_smmu_native.txn.state=CE_SM_QUARANTINED;
    }
    if(ceph_gx_smmu_native.output_safe)*out=ceph_gx_smmu_native.txn;
done:
    __atomic_store_n(&ceph_gpll0_lock,0,__ATOMIC_RELEASE);return e;
}
#endif
