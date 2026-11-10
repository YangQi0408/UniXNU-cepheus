/* Included inside the SWEET native translation unit after GPLL0 state. */
#ifndef CEPHEUS_QMP_NATIVE_PRIVATE_H
#define CEPHEUS_QMP_NATIVE_PRIVATE_H
#include "cepheus_qmp_native.h"
static struct {
    struct ceph_qmp_native_owner owner;
    struct ceph_qmp_mmio mmio;
    struct ceph_qmp_native_result result;
    unsigned attempted,acquired,finished,failed,keep_attempted,clock_seen;
    uint64_t last_ns;
} ceph_qmp_native;
static int ceph_qmp_native_completed(void)
{
    return !ceph_qmp_native.attempted || !ceph_qmp_native.acquired || !ceph_qmp_native.finished ||
        ceph_qmp_native.failed || ceph_qmp_native.result.error ||
        !ceph_qmp_native.result.connection.connected || !ceph_qmp_native.result.acd.consumed ||
        ceph_qmp_native.result.acd.uncertain?-1:0;
}
static int ceph_qmp_native_cold_record(uint64_t *token)
{
    if(!token || ceph_qmp_native_completed())return -1;
    if(ceph_scm_info_context(NULL) || cepheus_gpu_rpmh_check(ceph_qmp_native.owner.rpmh_token) ||
        ceph_qmp_native.owner.validate(ceph_qmp_native.owner.ctx) || ceph_qmp_native_completed() ||
        cepheus_gpu_rpmh_check(ceph_qmp_native.owner.rpmh_token)) {
        ceph_qmp_native.failed=1;ceph_qmp_native.result.acd.consumed=0;
        ceph_qmp_native.result.acd.uncertain=1;return -1;
    }
    *token=ceph_qmp_native.owner.rpmh_token;return 0;
}
static int ceph_qmp_native_lease(void *unused)
{
    (void)unused;
    if(!ceph_qmp_native.acquired || ceph_qmp_native.finished || ceph_qmp_native.failed)return -1;
    if(ceph_scm_info_context(NULL) || cepheus_gpu_rpmh_check(ceph_qmp_native.owner.rpmh_token) ||
        ceph_qmp_native.owner.validate(ceph_qmp_native.owner.ctx) ||
        cepheus_gpu_rpmh_check(ceph_qmp_native.owner.rpmh_token)) {
        ceph_qmp_native.failed=1;return -1;
    }
    return 0;
}
static int ceph_qmp_native_now(void *unused,uint64_t *out)
{
    if(!out || ceph_qmp_native_lease(unused))return -1;
    uint64_t value;absolutetime_to_nanoseconds(mach_absolute_time(),&value);
    if(ceph_qmp_native_lease(unused) || (ceph_qmp_native.clock_seen && value<ceph_qmp_native.last_ns)) {
        ceph_qmp_native.failed=1;return -1;
    }
    ceph_qmp_native.clock_seen=1;ceph_qmp_native.last_ns=value;*out=value;return 0;
}
static int ceph_qmp_native_pause(void *unused)
{
    uint64_t before,after;
    if(ceph_qmp_native_now(unused,&before))return -1;
    delay(500);
    if(ceph_qmp_native_now(unused,&after) || after-before<500000 || after-before>10000000) {
        ceph_qmp_native.failed=1;return -1;
    }
    return 0;
}
static void ceph_qmp_native_release(void *unused,int uncertain)
{
    (void)unused;
    if(ceph_qmp_native.finished)return;
    struct ceph_qmp_native_result *r=&ceph_qmp_native.result;
    if(!uncertain && ceph_qmp_native_lease(NULL)) {
        uncertain=1;r->acd.consumed=0;r->acd.uncertain=1;
    }
    ceph_qmp_native.finished=1;
    ceph_qmp_native.owner.finish(ceph_qmp_native.owner.ctx,
        r->connection.writes+ceph_qmp_native.mmio.writes,
        r->connection.notifies+ceph_qmp_native.mmio.notified,
        uncertain || ceph_qmp_native.keep_attempted || r->connection.connected);
}
int cepheus_gpu_qmp_acd_native(const struct ceph_qmp_native_owner *owner,struct ceph_qmp_native_result *out)
{
    if(!owner || !owner->begin || !owner->validate || !owner->finish || !out ||
        ceph_scm_info_context(NULL))return -1;
    if(__atomic_exchange_n(&ceph_gpll0_lock,1,__ATOMIC_ACQUIRE))return -1;
    int e=-1;
    if(ceph_qmp_native.attempted)goto done;
    ceph_qmp_native.attempted=1;ceph_qmp_native.owner=*owner;
    if(cepheus_gpu_rpmh_check(owner->rpmh_token))goto failed;
    e=ceph_qmp_native.owner.begin(ceph_qmp_native.owner.ctx);if(e)goto failed;
    ceph_qmp_native.acquired=1;
    if(ceph_qmp_native_lease(NULL)){e=-1;goto failed;}
    /* Connection success persists even before ACD. Keep scoped idle/transport
     * before any mapping or possible mailbox effect, never infer ownership. */
    ceph_qmp_native.keep_attempted=1;
    if(cepheus_gpu_rpmh_finish(ceph_qmp_native.owner.rpmh_token,1) || ceph_qmp_native_lease(NULL)) {
        e=-1;goto failed;
    }
    struct ceph_qmp_mmio *m=&ceph_qmp_native.mmio;
    m->owner=NULL;m->lease=ceph_qmp_native_lease;m->now_ns=ceph_qmp_native_now;
    m->pause=ceph_qmp_native_pause;m->release=ceph_qmp_native_release;
    const uint64_t pa[]={0xc300000,0x17c0000c};const size_t bytes[]={4096,4};
    for(unsigned n=0;n<2;++n) {
        if(ceph_qmp_native_lease(NULL)){e=-1;goto failed;}
        uintptr_t base=ml_io_map(pa[n],bytes[n]);
        if(ceph_qmp_native_lease(NULL) || !base){e=-1;goto failed;}
        if(!n){m->ram=(volatile uint32_t *)base;m->ram_pa=pa[n];m->bytes=bytes[n];}
        else {m->irq=(volatile uint32_t *)base;m->irq_pa=pa[n];}
    }
    struct ceph_qmp_native_result *r=&ceph_qmp_native.result;
    e=ceph_qmp_connect_cold(&r->connection,m);if(e)goto failed;
    struct ceph_qmp_ops ops;
    if(ceph_qmp_mmio_bind(m,1) || ceph_qmp_mmio_ops(m,&ops)){e=-1;goto failed;}
    e=ceph_qmp_send_acd(&r->acd,&ops,1);
    if(!e && (!r->acd.consumed || r->acd.uncertain))e=-1;
failed:
    ceph_qmp_native.result.error=e;
    if(ceph_qmp_native.acquired && !ceph_qmp_native.finished)ceph_qmp_native_release(NULL,e!=0);
    uint64_t token;
    if(!e && ceph_qmp_native_cold_record(&token))e=-1;
    ceph_qmp_native.result.error=e;
    *out=ceph_qmp_native.result;
done:
    __atomic_store_n(&ceph_gpll0_lock,0,__ATOMIC_RELEASE);return e;
}
#endif
