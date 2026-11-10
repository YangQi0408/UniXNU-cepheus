/* Included after GPLL0 native state inside the SWEET translation unit. */
#ifndef CEPHEUS_GPU_PDC_NATIVE_PRIVATE_H
#define CEPHEUS_GPU_PDC_NATIVE_PRIVATE_H
static struct {
    struct ceph_pdc_native_owner owner;
    struct ceph_pdc_input input;
    struct ceph_gmu_mmio gmu;
    struct ceph_power_mmio power;
    struct ceph_pdc_mmio mmio;
    struct ceph_pdc_install txn;
    unsigned attempted,acquired,finished,failed;
} ceph_pdc_native;
static int ceph_pdc_native_lease(void *unused)
{
    (void)unused;
    if(!ceph_pdc_native.acquired || ceph_pdc_native.finished || ceph_pdc_native.failed)return -1;
    if(ceph_scm_info_context(NULL) || ceph_pdc_native.owner.validate(ceph_pdc_native.owner.ctx)) {
        ceph_pdc_native.failed=1;return -1;
    }
    return 0;
}
static int ceph_pdc_native_access(void *unused)
{
    if(ceph_pdc_native_lease(unused))return -1;
    if(ceph_power_mmio_gmu_ready(&ceph_pdc_native.power)) {
        ceph_pdc_native.failed=1;return -1;
    }
    return 0;
}
static void ceph_pdc_native_finish(void *unused,int keep)
{
    (void)unused;
    ceph_pdc_native.finished=1;
    ceph_pdc_native.owner.finish(ceph_pdc_native.owner.ctx,keep);
}
static int ceph_pdc_native_begin(void *unused,const struct ceph_pdc_input *input)
{
    (void)unused;
    int e=ceph_pdc_native.owner.begin(ceph_pdc_native.owner.ctx,input,&ceph_pdc_native.gmu.config);
    if(e)return e;
    ceph_pdc_native.acquired=1;
    struct ceph_power_mmio *p=&ceph_pdc_native.power;
    p->owner=NULL;p->lease=ceph_pdc_native_lease;
    struct ceph_power_window *windows[]={&p->gcc,&p->cc,&p->gmu};
    const uint64_t pa[]={0x100000,0x2c90000,0x2c6a000,0xb290000,0xb490000};
    const size_t bytes[]={0x90000,0x9000,0x30000,0x10000,0x10000};
    struct ceph_pdc_mmio *m=&ceph_pdc_native.mmio;
    for(unsigned n=0;n<5;++n) {
        if(ceph_pdc_native_lease(NULL))goto failed;
        uintptr_t base=ml_io_map(pa[n],bytes[n]);
        if(ceph_pdc_native_lease(NULL) || !base)goto failed;
        if(n<3)*windows[n]=(struct ceph_power_window){(volatile uint8_t *)base,pa[n],bytes[n]};
        else if(n==3){m->cfg=(volatile uint32_t *)base;m->cfg_pa=pa[n];m->cfg_bytes=bytes[n];}
        else {m->seq=(volatile uint32_t *)base;m->seq_pa=pa[n];m->seq_bytes=bytes[n];}
    }
    struct ceph_gmu_mmio *g=&ceph_pdc_native.gmu;
    g->base=p->gmu.base;g->bytes=p->gmu.bytes;g->owner=NULL;g->lease=ceph_pdc_native_access;
    m->gmu=g;m->owner=NULL;m->lease=ceph_pdc_native_lease;m->finish=ceph_pdc_native_finish;
    /* Retention was an explicit earlier stage, not a hidden acquisition write. */
    uint32_t retained;
    if(ceph_gmu_mmio_start_read(g,CE_GMU_TCM_RETENTION,&retained) || retained!=1 ||
        ceph_pdc_mmio_begin(m,input))goto failed;
    return 0;
failed:
    ceph_pdc_native.failed=1;ceph_pdc_native_finish(NULL,0);return -1;
}
static int ceph_pdc_native_validate(void *unused)
{(void)unused;return ceph_pdc_mmio_validate(&ceph_pdc_native.mmio);}
static int ceph_pdc_native_read(void *unused,enum ceph_pdc_window w,uint32_t off,uint32_t *out)
{(void)unused;return ceph_pdc_mmio_read(&ceph_pdc_native.mmio,w,off,out);}
static int ceph_pdc_native_write(void *unused,enum ceph_pdc_window w,uint32_t off,uint32_t value)
{(void)unused;return ceph_pdc_mmio_write(&ceph_pdc_native.mmio,w,off,value);}
static int ceph_pdc_native_barrier(void *unused)
{(void)unused;return ceph_pdc_mmio_barrier(&ceph_pdc_native.mmio);}
static void ceph_pdc_native_end(void *unused,int keep)
{(void)unused;ceph_pdc_mmio_end(&ceph_pdc_native.mmio,keep);}
int cepheus_gpu_pdc_install_native(const struct ceph_pdc_native_owner *owner,
    const struct ceph_pdc_input *input,const struct ceph_gmu_start_config *config,struct ceph_pdc_install *out)
{
    struct ceph_pdc_plan plan;struct ceph_gmu_start_plan gmu_plan;
    if(!owner || !owner->begin || !owner->validate || !owner->finish || !out ||
        ceph_pdc_plan_a640v2(input,&plan) || ceph_gmu_start_plan(config,&gmu_plan) ||
        ceph_scm_info_context(NULL))return -1;
    if(__atomic_exchange_n(&ceph_gpll0_lock,1,__ATOMIC_ACQUIRE))return -1;
    int e=-1;
    if(ceph_pdc_native.attempted)goto done;
    ceph_pdc_native.attempted=1;ceph_pdc_native.owner=*owner;
    ceph_pdc_native.input=*input;ceph_pdc_native.gmu.config=*config;
    struct ceph_pdc_ops ops={NULL,ceph_pdc_native_begin,ceph_pdc_native_validate,
        ceph_pdc_native_read,ceph_pdc_native_write,ceph_pdc_native_barrier,ceph_pdc_native_end};
    e=ceph_pdc_install_a640v2(&ceph_pdc_native.txn,&ops,&ceph_pdc_native.input);
    *out=ceph_pdc_native.txn;
done:
    __atomic_store_n(&ceph_gpll0_lock,0,__ATOMIC_RELEASE);return e;
}
#endif
