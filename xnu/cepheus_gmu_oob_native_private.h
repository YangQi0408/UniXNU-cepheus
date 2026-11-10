/* Included after actual native CM3 state in the SWEET translation unit. */
#ifndef CEPHEUS_GMU_OOB_NATIVE_PRIVATE_H
#define CEPHEUS_GMU_OOB_NATIVE_PRIVATE_H
#include "cepheus_gmu_oob_native.h"
static struct {
    struct ceph_oob_native_owner owner;
    struct ceph_gmu_mmio mmio;
    struct ceph_power_mmio power;
    struct ceph_gmu_oob txn;
    unsigned attempted,acquired,failed,kept,ends,release_attempted,clock_seen;
    uint64_t last_ns;
} ceph_oob_native;
static int ceph_oob_native_record(void)
{
    return !ceph_cm3_native.attempted || !ceph_cm3_native.acquired ||
        !ceph_cm3_native.finished || ceph_cm3_native.failed ||
        ceph_oob_native.power.gcc.base!=ceph_cm3_native.power.gcc.base ||
        ceph_oob_native.power.cc.base!=ceph_cm3_native.power.cc.base ||
        ceph_oob_native.power.gmu.base!=ceph_cm3_native.power.gmu.base ||
        ceph_oob_native.mmio.base!=ceph_cm3_native.mmio.base ||
        ceph_oob_native.mmio.config.chipid!=ceph_cm3_native.mmio.config.chipid ||
        ceph_oob_native.mmio.config.hfi_va!=ceph_cm3_native.mmio.config.hfi_va ||
        ceph_oob_native.mmio.config.hfi_bytes!=ceph_cm3_native.mmio.config.hfi_bytes ||
        ceph_oob_native.mmio.config.log_va!=ceph_cm3_native.mmio.config.log_va ||
        ceph_oob_native.mmio.config.log_bytes!=ceph_cm3_native.mmio.config.log_bytes ||
        ceph_cm3_native.txn.state!=CE_GS_HFI_READY || ceph_cm3_native.txn.error ||
        (ceph_cm3_native.txn.hfi_version>>28)!=2 ||
        ceph_cm3_native.txn.hfi_version!=ceph_oob_native.mmio.hfi_version?-1:0;
}
static int ceph_oob_native_lease(void *unused)
{
    (void)unused;
    if(!ceph_oob_native.acquired || ceph_oob_native.failed)return -1;
    if(ceph_scm_info_context(NULL) || ceph_oob_native_record() ||
        cepheus_gpu_rpmh_check(ceph_oob_native.owner.rpmh_token) ||
        ceph_oob_native.owner.validate(ceph_oob_native.owner.ctx) ||
        ceph_oob_native_record() || cepheus_gpu_rpmh_check(ceph_oob_native.owner.rpmh_token)) {
        ceph_oob_native.failed=1;return -1;
    }
    return 0;
}
static int ceph_oob_native_power(void *unused)
{
    if(ceph_oob_native_lease(unused))return -1;
    if(ceph_power_mmio_gmu_ready(&ceph_oob_native.power)) {
        ceph_oob_native.failed=1;return -1;
    }
    return 0;
}
static int ceph_oob_native_access(void *unused)
{
    struct ceph_gmu_mmio *m=&ceph_oob_native.mmio;
    uint32_t reset,result,status,version;
    if(ceph_oob_native_power(unused))return -1;
    if(ceph_gmu_mmio_start_read(m,CE_GMU_SYSRESET,&reset) || reset ||
        ceph_gmu_mmio_start_read(m,CE_GMU_INIT_RESULT,&result) || result!=0xbabeface ||
        ceph_gmu_mmio_start_read(m,CE_GMU_HFI_STATUS,&status) || !(status&1u) ||
        ceph_gmu_mmio_start_read(m,CE_GMU_HFI_VERSION,&version) || version!=m->hfi_version ||
        ceph_gmu_mmio_fault(m) || ceph_oob_native_lease(NULL)) {
        ceph_oob_native.failed=1;return -1;
    }
    return 0;
}
static void ceph_oob_native_finish(void *unused,int keep)
{
    (void)unused;
    if(!ceph_oob_native.acquired || ceph_oob_native.ends>=2)return;
    if(!ceph_oob_native.failed && ceph_oob_native_access(NULL)) {
        ceph_oob_native.txn.state=CE_OOB_FAILED;ceph_oob_native.txn.error=-1;
    }
    ++ceph_oob_native.ends;
    ceph_oob_native.owner.finish(ceph_oob_native.owner.ctx,ceph_oob_native.txn.writes,
        keep || ceph_oob_native.kept);
}
static int ceph_oob_native_begin(void *unused)
{
    (void)unused;
    if(ceph_oob_native_record() || cepheus_gpu_rpmh_check(ceph_oob_native.owner.rpmh_token))return -1;
    int e=ceph_oob_native.owner.begin(ceph_oob_native.owner.ctx);if(e)return e;
    ceph_oob_native.acquired=1;
    if(ceph_oob_native_lease(NULL))goto failed;
    ceph_oob_native.kept=1;
    if(cepheus_gpu_rpmh_finish(ceph_oob_native.owner.rpmh_token,1) ||
        ceph_oob_native_access(NULL))goto failed;
    /* Refuse a visibly pending inherited doorbell; a clear raw bit alone
     * cannot prove absence of firmware-held OOB state or foreign ownership. */
    struct ceph_oob_bits bits;uint32_t raw;
    if(ceph_gmu_oob_gpu_bits(ceph_oob_native.mmio.hfi_version,&bits) ||
        ceph_oob_native_access(NULL) ||
        ceph_gmu_mmio_load(&ceph_oob_native.mmio,0x1f996,&raw) ||
        ceph_oob_native_access(NULL) || (raw&(bits.request|bits.release)))goto failed;
    return 0;
failed:
    ceph_oob_native.failed=1;ceph_oob_native_finish(NULL,ceph_oob_native.kept);return -1;
}
static int ceph_oob_native_read(void *unused,enum ceph_oob_reg r,uint32_t *out)
{
    (void)unused;uint32_t value;
    if(!out || ceph_oob_native_access(NULL) ||
        ceph_gmu_mmio_oob_read(&ceph_oob_native.mmio,r,&value) ||
        ceph_oob_native_access(NULL))return -1;
    *out=value;return 0;
}
static int ceph_oob_native_write(void *unused,enum ceph_oob_reg r,uint32_t value)
{
    if(ceph_oob_native_access(unused) ||
        ceph_gmu_mmio_oob_write(&ceph_oob_native.mmio,r,value))return -1;
    return ceph_oob_native_access(unused);
}
static int ceph_oob_native_barrier(void *unused)
{
    if(ceph_oob_native_access(unused))return -1;
    ceph_gmu_mmio_barrier();return ceph_oob_native_access(unused);
}
static int ceph_oob_native_now(void *unused,uint64_t *out)
{
    if(!out || ceph_oob_native_access(unused))return -1;
    uint64_t value;absolutetime_to_nanoseconds(mach_absolute_time(),&value);
    if(ceph_oob_native_access(unused) ||
        (ceph_oob_native.clock_seen && value<ceph_oob_native.last_ns)) {
        ceph_oob_native.failed=1;return -1;
    }
    ceph_oob_native.clock_seen=1;ceph_oob_native.last_ns=value;*out=value;return 0;
}
static int ceph_oob_native_pause(void *unused)
{
    uint64_t before,after;
    if(ceph_oob_native_now(unused,&before))return -1;
    delay(1);
    if(ceph_oob_native_now(unused,&after) || after-before<1000 || after-before>100000) {
        ceph_oob_native.failed=1;return -1;
    }
    return 0;
}
static int ceph_oob_native_drained(void *unused)
{
    if(ceph_oob_native_access(unused) ||
        ceph_oob_native.owner.drained(ceph_oob_native.owner.ctx))return -1;
    return ceph_oob_native_access(unused);
}
static struct ceph_oob_ops ceph_oob_native_ops(void)
{
    return (struct ceph_oob_ops){NULL,ceph_oob_native_begin,ceph_oob_native_access,
        ceph_oob_native_read,ceph_oob_native_write,ceph_oob_native_barrier,
        ceph_oob_native_now,ceph_oob_native_pause,ceph_oob_native_drained,ceph_oob_native_finish};
}
int cepheus_gpu_oob_acquire_native(const struct ceph_oob_native_owner *owner,struct ceph_gmu_oob *out)
{
    if(!owner || !owner->begin || !owner->validate || !owner->drained || !owner->finish ||
        !out || ceph_scm_info_context(NULL))return -1;
    if(__atomic_exchange_n(&ceph_gpll0_lock,1,__ATOMIC_ACQUIRE))return -1;
    int e=-1;
    if(ceph_oob_native.attempted)goto done;
    ceph_oob_native.attempted=1;ceph_oob_native.owner=*owner;
    ceph_oob_native.power=ceph_cm3_native.power;
    ceph_oob_native.power.owner=NULL;ceph_oob_native.power.lease=ceph_oob_native_lease;
    ceph_oob_native.mmio=ceph_cm3_native.mmio;
    ceph_oob_native.mmio.owner=NULL;ceph_oob_native.mmio.lease=ceph_oob_native_power;
    ceph_oob_native.mmio.hfi_version=ceph_cm3_native.txn.hfi_version;
    struct ceph_oob_ops ops=ceph_oob_native_ops();
    e=ceph_gmu_oob_acquire(&ceph_oob_native.txn,&ops,ceph_oob_native.mmio.hfi_version);
    if(!e && ceph_oob_native.txn.state!=CE_OOB_HELD)e=-1;
    *out=ceph_oob_native.txn;
done:
    __atomic_store_n(&ceph_gpll0_lock,0,__ATOMIC_RELEASE);return e;
}
int cepheus_gpu_oob_release_native(struct ceph_gmu_oob *out)
{
    if(!out || ceph_scm_info_context(NULL))return -1;
    if(__atomic_exchange_n(&ceph_gpll0_lock,1,__ATOMIC_ACQUIRE))return -1;
    int e=-1;
    if(ceph_oob_native.txn.state!=CE_OOB_HELD || ceph_oob_native.release_attempted ||
        ceph_oob_native.failed || !ceph_oob_native.acquired)goto done;
    ceph_oob_native.release_attempted=1;
    struct ceph_oob_ops ops=ceph_oob_native_ops();
    e=ceph_gmu_oob_release(&ceph_oob_native.txn,&ops);
    if(!e && ceph_oob_native.txn.state!=CE_OOB_RELEASED)e=-1;
    *out=ceph_oob_native.txn;
done:
    __atomic_store_n(&ceph_gpll0_lock,0,__ATOMIC_RELEASE);return e;
}
#endif
