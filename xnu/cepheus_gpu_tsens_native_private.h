/* Included inside the actual SWEET native translation unit. */
#ifndef CEPHEUS_GPU_TSENS_NATIVE_PRIVATE_H
#define CEPHEUS_GPU_TSENS_NATIVE_PRIVATE_H
static unsigned ceph_tsens_native_lock;
static struct {
    struct ceph_gpu_tsens_native_owner owner;
    struct ceph_gpu_tsens mmio;
    uint64_t last_ns;
    unsigned attempted,ready,failed,clock_seen;
} ceph_tsens_native;
static int ceph_tsens_native_lease(void *unused)
{
    (void)unused;
    if(!ceph_tsens_native.attempted || ceph_tsens_native.failed)return -1;
    if(ceph_scm_info_context(NULL) ||
        ceph_tsens_native.owner.validate(ceph_tsens_native.owner.ctx)) {
        ceph_tsens_native.failed=1;return -1;
    }
    return 0;
}
static int ceph_tsens_native_now(void *unused,uint64_t *out)
{
    (void)unused;
    if(!out || ceph_tsens_native_lease(NULL))return -1;
    uint64_t value;absolutetime_to_nanoseconds(mach_absolute_time(),&value);
    if(ceph_tsens_native_lease(NULL) ||
        (ceph_tsens_native.clock_seen && value<ceph_tsens_native.last_ns))return -1;
    ceph_tsens_native.last_ns=value;ceph_tsens_native.clock_seen=1;
    *out=value;return 0;
}
int cepheus_gpu_tsens_bind_native(const struct ceph_gpu_tsens_native_owner *owner)
{
    if(!owner || !owner->validate || ceph_scm_info_context(NULL))return -1;
    if(__atomic_exchange_n(&ceph_tsens_native_lock,1,__ATOMIC_ACQUIRE))return -1;
    int e=-1;
    if(ceph_tsens_native.attempted)goto done;
    ceph_tsens_native.attempted=1;ceph_tsens_native.owner=*owner;
    struct ceph_gpu_tsens *m=&ceph_tsens_native.mmio;
    m->owner=NULL;m->lease=ceph_tsens_native_lease;m->now_ns=ceph_tsens_native_now;
    const uint64_t pa[]={0xc263000,0xc265000,0xc222000,0xc223000};
    for(unsigned n=0;n<4;++n) {
        if(ceph_tsens_native_lease(NULL))goto failed;
        volatile uint32_t *base=(volatile uint32_t *)ml_io_map(pa[n],4096);
        if(ceph_tsens_native_lease(NULL) || !base)goto failed;
        if(n<2){m->tm[n]=base;m->pa[n]=pa[n];m->bytes[n]=4096;}
        else {m->srot[n-2]=base;m->srot_pa[n-2]=pa[n];m->srot_bytes[n-2]=4096;}
    }
    if(ceph_gpu_tsens_valid(m))goto failed;
    ceph_tsens_native.ready=1;e=0;goto done;
failed:
    ceph_tsens_native.failed=1;
done:
    __atomic_store_n(&ceph_tsens_native_lock,0,__ATOMIC_RELEASE);return e;
}
int cepheus_gpu_temperature_sample_native(struct ceph_gpu_temperature *out)
{
    if(!out)return -1;
    *out=(struct ceph_gpu_temperature){0};
    if(ceph_scm_info_context(NULL) ||
        __atomic_exchange_n(&ceph_tsens_native_lock,1,__ATOMIC_ACQUIRE))return -1;
    int e=-1;
    if(ceph_tsens_native.ready && !ceph_tsens_native.failed) {
        e=ceph_gpu_tsens_sample(&ceph_tsens_native.mmio,out);
        if(e)ceph_tsens_native.failed=1;
    }
    __atomic_store_n(&ceph_tsens_native_lock,0,__ATOMIC_RELEASE);return e;
}
int cepheus_gpu_thermal_now_native(uint64_t *out)
{
    if(!out || ceph_scm_info_context(NULL) ||
        __atomic_exchange_n(&ceph_tsens_native_lock,1,__ATOMIC_ACQUIRE))return -1;
    int e=-1;
    if(ceph_tsens_native.ready && !ceph_tsens_native.failed) {
        e=ceph_tsens_native_now(NULL,out);
        if(e)ceph_tsens_native.failed=1;
    }
    __atomic_store_n(&ceph_tsens_native_lock,0,__ATOMIC_RELEASE);return e;
}
#endif
