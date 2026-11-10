/* Included after GPLL0 native state in the actual SWEET translation unit. */
#ifndef CEPHEUS_CLOCK_MEASURE_PRIVATE_H
#define CEPHEUS_CLOCK_MEASURE_PRIVATE_H
static struct ceph_clock_measure ceph_measure_native;
static int ceph_measure_native_now(void *unused,uint64_t *out)
{
    (void)unused;
    if(!out || ceph_scm_info_context(NULL))return -1;
    absolutetime_to_nanoseconds(mach_absolute_time(),out);return 0;
}
int cepheus_gpu_gpll0_measure(const struct ceph_measure_owner *owner,struct ceph_clock_sample *out)
{
    if(!owner || !owner->validate || !owner->finish || !out || ceph_scm_info_context(NULL) ||
        __atomic_exchange_n(&ceph_gpll0_lock,1,__ATOMIC_ACQUIRE))return CE_ME_INVALID;
    struct ceph_clock_measure *s=&ceph_measure_native;int e=CE_ME_INVALID;
    if(s->phase!=CE_ME_NEW)goto done;
    s->parent=&ceph_gpll0_native;s->owner=*owner;
    s->now_ns=ceph_measure_native_now;s->pause_us=ceph_gpll0_native_delay;
    e=ceph_clock_measure_run(s,out);
done:
    __atomic_store_n(&ceph_gpll0_lock,0,__ATOMIC_RELEASE);return e;
}
#endif
