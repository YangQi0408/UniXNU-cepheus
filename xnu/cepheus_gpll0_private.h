/* Included by cepheus_gpu_compile.c inside the native SWEET arm64 branch. */
#ifndef CEPHEUS_GPLL0_PRIVATE_H
#define CEPHEUS_GPLL0_PRIVATE_H
static unsigned ceph_gpll0_lock;
static struct ceph_gpll0 ceph_gpll0_native;
static int ceph_gpll0_native_delay(void *unused,unsigned us)
{
    (void)unused;
    if(!us || us>500 || ceph_scm_info_context(NULL))return -1;
    delay(us);return 0;
}
int cepheus_gpu_gpll0_hold(const struct ceph_gpll0_owner *owner,struct ceph_gpll0_snapshot *out)
{
    if(!owner || !owner->validate || !owner->retained || !out ||
        ceph_scm_info_context(NULL))return -1;
    if(__atomic_exchange_n(&ceph_gpll0_lock,1,__ATOMIC_ACQUIRE))return -1;
    struct ceph_gpll0 *s=&ceph_gpll0_native;
    int e=-1;
    if(s->phase!=CE_GP_NEW)goto done;
    /* Validate before mapping; this does not acquire the shared GCC owner. */
    s->phase=CE_GP_REJECTED;s->owner=*owner;
    if(s->owner.validate(s->owner.ctx))goto done;
    s->gcc=(volatile uint8_t *)ml_io_map(CE_GPLL0_PA,CE_GPLL0_BYTES);
    s->pa=CE_GPLL0_PA;s->bytes=CE_GPLL0_BYTES;
    s->delay_us=ceph_gpll0_native_delay;
    s->phase=CE_GP_NEW;e=ceph_gpll0_hold(s,out);
done:
    __atomic_store_n(&ceph_gpll0_lock,0,__ATOMIC_RELEASE);return e;
}
int cepheus_gpu_gpll0_check(struct ceph_gpll0_snapshot *out)
{
    if(!out || ceph_scm_info_context(NULL) ||
        __atomic_exchange_n(&ceph_gpll0_lock,1,__ATOMIC_ACQUIRE))return -1;
    int e=ceph_gpll0_check(&ceph_gpll0_native,out);
    __atomic_store_n(&ceph_gpll0_lock,0,__ATOMIC_RELEASE);return e;
}
#endif
