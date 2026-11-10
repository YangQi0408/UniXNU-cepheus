/* Included after GPLL0 state inside the SWEET translation unit. */
#ifndef CEPHEUS_SMMU_NATIVE_PRIVATE_H
#define CEPHEUS_SMMU_NATIVE_PRIVATE_H
static struct {
    struct ceph_smmu_native_owner owner;
    struct ceph_gpu_publication publication;
    struct ceph_smmu_mmio mmio;
    struct ceph_power_mmio power;
    struct ceph_gmu_mmio gmu;
    struct ceph_smmu_install txn;
    unsigned attempted,acquired,pinned,finished,failed;
} ceph_smmu_native;
static int ceph_smmu_native_lease(void *unused)
{
    (void)unused;
    if(!ceph_smmu_native.acquired || ceph_smmu_native.finished || ceph_smmu_native.failed)return -1;
    if(ceph_scm_info_context(NULL) || ceph_smmu_native.owner.validate(ceph_smmu_native.owner.ctx) ||
        (ceph_smmu_native.pinned && ceph_memory_smmu_validate(&ceph_smmu_native.publication))) {
        ceph_smmu_native.failed=1;return -1;
    }
    return 0;
}
static int ceph_smmu_native_gmu_access(void *unused)
{
    if(ceph_smmu_native_lease(unused))return -1;
    if(ceph_power_mmio_gmu_ready(&ceph_smmu_native.power)) {
        ceph_smmu_native.failed=1;return -1;
    }
    return 0;
}
static int ceph_smmu_native_access(void *unused)
{
    uint32_t reset,memnoc,dvm;
    if(ceph_gmu_mmio_start_read(&ceph_smmu_native.gmu,CE_GMU_SYSRESET,&reset) || reset!=1) {
        ceph_smmu_native.failed=1;return -1;
    }
    struct ceph_power_mmio *p=&ceph_smmu_native.power;
    if(ceph_power_mmio_load(p,&p->gcc,CE_GPU_GCC_MEMNOC,&memnoc) || !ceph_gpu_branch_active(memnoc) ||
        ceph_power_mmio_load(p,&p->gcc,CE_GPU_GCC_DVM,&dvm) || !ceph_gpu_branch_active(dvm)) {
        ceph_smmu_native.failed=1;return -1;
    }
    return ceph_smmu_native_lease(unused);
}
static int ceph_smmu_native_raw_read(uint32_t offset,uint32_t *out)
{
    if(!out || (offset&3u) || offset>0x10000u-4u || !ceph_smmu_native.mmio.base ||
        ((uintptr_t)ceph_smmu_native.mmio.base&3u) ||
        (uintptr_t)ceph_smmu_native.mmio.base>UINTPTR_MAX-0x10000u ||
        ceph_smmu_native_access(NULL))return -1;
    uint32_t value=*(volatile uint32_t *)(ceph_smmu_native.mmio.base+offset);
    ceph_smmu_mmio_order();
    if(ceph_smmu_native_access(NULL) || value==UINT32_MAX) {
        ceph_smmu_native.failed=1;return -1;
    }
    *out=value;return 0;
}
static void ceph_smmu_native_finish(void *unused,int keep)
{
    (void)unused;
    if(ceph_smmu_native.txn.state==CE_SM_ENABLED && ceph_smmu_native_access(NULL)) {
        ceph_smmu_native.txn.state=CE_SM_QUARANTINED;ceph_smmu_native.txn.error=-1;keep=1;
    }
    if(ceph_smmu_native.pinned) {
        enum ceph_memory_smmu_result result=ceph_smmu_native.txn.state==CE_SM_ENABLED?
            CE_MEMORY_SMMU_INSTALLED:keep?CE_MEMORY_SMMU_UNCERTAIN:CE_MEMORY_SMMU_REFUSED;
        if(ceph_memory_smmu_finish(&ceph_smmu_native.publication,result)) {
            ceph_smmu_native.failed=1;ceph_smmu_native.txn.state=CE_SM_QUARANTINED;
            ceph_smmu_native.txn.error=-1;keep=1;
        } else ceph_smmu_native.pinned=0;
    }
    ceph_smmu_native.finished=1;
    ceph_smmu_native.owner.finish(ceph_smmu_native.owner.ctx,keep);
}
static int ceph_smmu_native_begin(void *unused,const struct ceph_smmu_plan *plan,uint32_t actlr)
{
    (void)unused;
    int e=ceph_smmu_native.owner.begin(ceph_smmu_native.owner.ctx,plan,&ceph_smmu_native.publication);
    if(e)return e;
    ceph_smmu_native.acquired=1;
    if(ceph_smmu_native_lease(NULL) ||
        ceph_memory_smmu_acquire(&ceph_smmu_native.publication,&ceph_smmu_native.gmu.config))goto failed;
    ceph_smmu_native.pinned=1;
    if(ceph_smmu_native_lease(NULL))goto failed;
    struct ceph_power_mmio *p=&ceph_smmu_native.power;
    p->owner=NULL;p->lease=ceph_smmu_native_lease;
    struct ceph_power_window *windows[]={&p->gcc,&p->cc,&p->gmu};
    const uint64_t pa[]={0x100000,0x2c90000,0x2c6a000,0x2ca0000};
    const size_t bytes[]={0x90000,0x9000,0x30000,0x10000};
    for(unsigned n=0;n<4;++n) {
        if(ceph_smmu_native_lease(NULL))goto failed;
        uintptr_t base=ml_io_map(pa[n],bytes[n]);
        if(ceph_smmu_native_lease(NULL) || !base)goto failed;
        if(n<3)*windows[n]=(struct ceph_power_window){(volatile uint8_t *)base,pa[n],bytes[n]};
        else ceph_smmu_native.mmio.base=(volatile uint8_t *)base;
    }
    struct ceph_gmu_mmio *g=&ceph_smmu_native.gmu;
    g->base=p->gmu.base;g->bytes=p->gmu.bytes;g->owner=NULL;g->lease=ceph_smmu_native_gmu_access;
    struct ceph_smmu_mmio *m=&ceph_smmu_native.mmio;
    uint32_t id[3];
    for(unsigned i=0;i<3;++i)if(ceph_smmu_native_raw_read(0x20+4*i,&id[i]))goto failed;
    if(ceph_smmu_geometry(id[0],id[1],id[2],0x10000,&m->geometry) || m->geometry.page_shift!=12 ||
        plan->cb<m->geometry.s2_contexts || plan->cb>=m->geometry.contexts)goto failed;
    m->mapped_bytes=0x10000;m->actlr=actlr;m->owner=NULL;m->lease=ceph_smmu_native_access;
    struct ceph_smmu_stream streams[128];
    for(unsigned i=0;i<m->geometry.groups;++i)
        if(ceph_smmu_native_raw_read(0x800+4*i,&streams[i].smr) ||
            ceph_smmu_native_raw_read(0xc00+4*i,&streams[i].s2cr))goto failed;
    uint64_t sctlr;struct ceph_smmu_plan observed;
    if(ceph_smmu_mmio_read(m,CE_SM_SCTLR,&sctlr) ||
        ceph_smmu_gmu_plan(streams,m->geometry.groups,plan->slot,plan->cb,m->geometry.contexts,
            5,plan->asid,plan->ttbr0&0xffffffffffULL,(uint32_t)sctlr,&observed))goto failed;
    return 0;
failed:
    ceph_smmu_native.failed=1;ceph_smmu_native_finish(NULL,0);return -1;
}
static int ceph_smmu_native_read(void *unused,enum ceph_smmu_reg r,uint64_t *out)
{
    (void)unused;int e=ceph_smmu_mmio_read(&ceph_smmu_native.mmio,r,out);
    if(e)ceph_smmu_native.failed=1;
    return e;
}
static int ceph_smmu_native_write(void *unused,enum ceph_smmu_reg r,uint64_t value)
{
    (void)unused;int e=ceph_smmu_mmio_write(&ceph_smmu_native.mmio,r,value);
    if(e)ceph_smmu_native.failed=1;
    return e;
}
static int ceph_smmu_native_publish(void *unused)
{
    if(ceph_smmu_native_access(unused))return -1;
    int e=ceph_memory_smmu_publish(&ceph_smmu_native.publication);
    if(e){ceph_smmu_native.failed=1;return e;}
    return ceph_smmu_native_access(unused);
}
static int ceph_smmu_native_barrier(void *unused)
{
    if(ceph_smmu_native_access(unused))return -1;
    ceph_smmu_mmio_order();return ceph_smmu_native_access(unused);
}
static int ceph_smmu_native_delay(void *unused,unsigned us)
{
    if(us!=1 || ceph_smmu_native_access(unused))return -1;
    uint64_t before,after;absolutetime_to_nanoseconds(mach_absolute_time(),&before);
    if(ceph_smmu_native_access(unused))return -1;
    delay(us);
    if(ceph_smmu_native_access(unused))return -1;
    absolutetime_to_nanoseconds(mach_absolute_time(),&after);
    if(ceph_smmu_native_access(unused) || after<before || after-before<1000 || after-before>100000) {
        ceph_smmu_native.failed=1;return -1;
    }
    return 0;
}
int cepheus_gpu_smmu_install_native(const struct ceph_smmu_native_owner *owner,
    const struct ceph_smmu_plan *plan,const struct ceph_gpu_publication *publication,struct ceph_smmu_install *out)
{
    if(!owner || !owner->begin || !owner->validate || !owner->finish || !out ||
        !ceph_smmu_plan_canonical(plan) || (plan->smr&0x7fffu)!=5 || !publication || !publication->group ||
        plan->ttbr0!=publication->ttbr0 || plan->ttbr1!=publication->ttbr1 || plan->smr!=publication->smr ||
        plan->cb!=publication->cb || plan->slot!=publication->slot || plan->asid!=publication->asid ||
        ceph_scm_info_context(NULL))return -1;
    if(__atomic_exchange_n(&ceph_gpll0_lock,1,__ATOMIC_ACQUIRE))return -1;
    int e=-1;
    if(ceph_smmu_native.attempted)goto done;
    ceph_smmu_native.attempted=1;ceph_smmu_native.owner=*owner;
    ceph_smmu_native.publication=*publication;ceph_smmu_native.mmio.plan=*plan;
    struct ceph_smmu_ops ops={NULL,ceph_smmu_native_begin,ceph_smmu_native_access,
        ceph_smmu_native_read,ceph_smmu_native_write,ceph_smmu_native_publish,
        ceph_smmu_native_barrier,ceph_smmu_native_delay,ceph_smmu_native_finish};
    e=ceph_smmu_install_context(&ceph_smmu_native.txn,&ops,&ceph_smmu_native.mmio.plan,0x303);
    if(ceph_smmu_native.pinned) {
        ceph_smmu_native.txn.state=CE_SM_QUARANTINED;
        ceph_smmu_native.txn.error=-1;e=-1;
    }
    if(!e && ceph_smmu_native.txn.error)e=ceph_smmu_native.txn.error;
    *out=ceph_smmu_native.txn;
done:
    __atomic_store_n(&ceph_gpll0_lock,0,__ATOMIC_RELEASE);return e;
}
#endif
