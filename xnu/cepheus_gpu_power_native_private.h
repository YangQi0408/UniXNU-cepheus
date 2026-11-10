/* Included after GPLL0 native state in the actual SWEET translation unit. */
#ifndef CEPHEUS_GPU_POWER_NATIVE_PRIVATE_H
#define CEPHEUS_GPU_POWER_NATIVE_PRIVATE_H
static struct {
    struct ceph_power_native_owner owner;
    struct ceph_power_mmio mmio;
    struct ceph_power_txn txn;
    unsigned attempted,acquired,finished,parent_failed;
} ceph_power_native;
static int ceph_power_native_parent(void)
{
    if(ceph_power_native.parent_failed)return -1;
    struct ceph_gpll0 *parent=&ceph_gpll0_native;
    struct ceph_clock_sample sample=ceph_measure_native_sample;
    struct ceph_gpll0_snapshot snapshot;
    int e=parent->phase!=CE_GP_HELD || !parent->scope_retained ||
        ceph_measure_native.phase!=CE_ME_DONE || ceph_measure_native.parent!=parent ||
        ceph_measure_counts(&sample) ||
        sample.short_hz!=ceph_measure_native_sample.short_hz ||
        sample.full_hz!=ceph_measure_native_sample.full_hz ||
        sample.short_quantum_hz!=ceph_measure_native_sample.short_quantum_hz ||
        sample.full_quantum_hz!=ceph_measure_native_sample.full_quantum_hz ||
        !sample.short_ns || !sample.full_ns || ceph_gpll0_check(parent,&snapshot);
    if(!e) {
        uint64_t short_delta=sample.short_hz>300000000?sample.short_hz-300000000:300000000-sample.short_hz;
        uint64_t full_delta=sample.full_hz>300000000?sample.full_hz-300000000:300000000-sample.full_hz;
        e=snapshot.configured_div_hz!=300000000 || short_delta>sample.short_quantum_hz ||
            full_delta>sample.full_quantum_hz;
    }
    if(e)ceph_power_native.parent_failed=1;
    return e?-1:0;
}
static int ceph_power_native_lease(void *unused)
{
    (void)unused;
    return !ceph_power_native.acquired || ceph_power_native.finished ||
        ceph_scm_info_context(NULL) || ceph_power_native_parent() ||
        ceph_power_native.owner.validate(ceph_power_native.owner.ctx) ||
        ceph_power_native_parent()?-1:0;
}
static void ceph_power_native_finish(void *unused,int keep)
{
    (void)unused;
    ceph_power_native.finished=1;
    ceph_power_native.owner.finish(ceph_power_native.owner.ctx,keep);
}
static int ceph_power_native_prepare(void *unused,struct ceph_power_input *in)
{
    (void)unused;
    if(ceph_power_native_parent())return CE_PWR_BUSY;
    int e=ceph_power_native.owner.prepare(ceph_power_native.owner.ctx,in);
    if(e)return e;
    ceph_power_native.acquired=1;
    if(ceph_power_native_parent())goto fail;
    struct ceph_power_mmio *m=&ceph_power_native.mmio;
    m->owner=NULL;m->lease=ceph_power_native_lease;
    struct ceph_power_window *windows[]={&m->gcc,&m->cc,&m->gmu};
    const uint64_t pa[]={0x100000,0x2c90000,0x2c6a000};
    const size_t bytes[]={0x90000,0x9000,0x30000};
    /* Mapping alone never reads an off-domain aperture. Guards enclose each
     * mapping, and the register adapter separately gates every GMU load. */
    for(unsigned n=0;n<3;++n) {
        if(ceph_power_native_lease(NULL))goto fail;
        *windows[n]=(struct ceph_power_window){
            (volatile uint8_t *)ml_io_map(pa[n],bytes[n]),pa[n],bytes[n]};
        if(ceph_power_native_lease(NULL) || !windows[n]->base)goto fail;
    }
    return 0;
fail:
    ceph_power_native_finish(NULL,0);return CE_PWR_IO;
}
static int ceph_power_native_read(void *unused,enum ceph_power_reg reg,uint32_t *out)
{(void)unused;return ceph_power_mmio_read(&ceph_power_native.mmio,reg,out);}
static int ceph_power_native_write(void *unused,enum ceph_power_reg reg,uint32_t value)
{(void)unused;return ceph_power_mmio_write(&ceph_power_native.mmio,reg,value);}
static int ceph_power_native_vote(void *unused,uint32_t address,unsigned index)
{
    (void)unused;
    if(ceph_power_native_lease(NULL))return CE_PWR_BUSY;
    int e=ceph_power_native.owner.vote(ceph_power_native.owner.ctx,address,index);
    if(e)return e;
    return ceph_power_native_lease(NULL)?CE_PWR_BUSY:0;
}
static int ceph_power_native_delay(void *unused,unsigned us)
{
    (void)unused;
    if(!us || us>2 || ceph_power_native_lease(NULL))return CE_PWR_IO;
    delay(us);return ceph_power_native_lease(NULL)?CE_PWR_IO:0;
}
int cepheus_gpu_power_start_native(const struct ceph_power_native_owner *owner,struct ceph_power_txn *out)
{
    if(!owner || !owner->prepare || !owner->validate || !owner->vote || !owner->finish ||
        !out || ceph_scm_info_context(NULL))return CE_PWR_INVALID;
    if(__atomic_exchange_n(&ceph_gpll0_lock,1,__ATOMIC_ACQUIRE))return CE_PWR_BUSY;
    int e=CE_PWR_BUSY;
    if(ceph_power_native.attempted)goto done;
    ceph_power_native.attempted=1;ceph_power_native.owner=*owner;
    struct ceph_power_ops ops={NULL,ceph_power_native_prepare,ceph_power_native_lease,
        ceph_power_native_read,ceph_power_native_write,ceph_power_native_vote,
        ceph_power_native_delay,ceph_power_native_finish};
    e=ceph_power_start(&ceph_power_native.txn,&ops);
    *out=ceph_power_native.txn;
done:
    __atomic_store_n(&ceph_gpll0_lock,0,__ATOMIC_RELEASE);return e;
}
#endif
