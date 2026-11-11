/* PAS13 lifecycle over the actual private ZAP workspace. The production
 * adapter uses native SMC, never a firmware function pointer. This engine does
 * not supply shared resource ownership or override supported=false. Sixteen
 * invocations bound resumption locally, not time inside one secure call. */
#ifndef CEPHEUS_ZAP_PAS_H
#define CEPHEUS_ZAP_PAS_H
#include "cepheus_scm_discovery.h"
#include "cepheus_zap_memory_private.h"
enum ceph_zap_pas_phase {CE_ZP_NEW,CE_ZP_MEMORY,CE_ZP_OWNED,CE_ZP_INIT,CE_ZP_MEM,
    CE_ZP_IMAGE,CE_ZP_AUTH,CE_ZP_LIVE,CE_ZP_STOP,CE_ZP_STOPPED,CE_ZP_REFUSED,CE_ZP_RETAINED};
enum ceph_zap_pas_command {CE_ZP_CMD_INIT=1,CE_ZP_CMD_MEM=2,CE_ZP_CMD_AUTH=5,CE_ZP_CMD_STOP=6};
struct ceph_zap_pas_resources {
    void *ctx;
    /* Acquire exclusive PAS13/GPU epoch and crypto bandwidth/clocks; validate
     * secure PA reachability, XPU granules and CPU/secure cache attributes for
     * this exact pair. Failure must resolve partial ownership itself. No
     * inherited-vote zero assumption or "ready" flag is an implementation. */
    int (*acquire)(void *,const struct ceph_zap_claim *,uint64_t *epoch);
    int (*lease)(void *,const struct ceph_zap_claim *,uint64_t epoch);
    /* Stop/drain every GPU user bound to this epoch before PAS shutdown.
     * An idle-status bit or fence completion alone is insufficient. */
    int (*quiesce)(void *,uint64_t epoch);
    /* keep=1 retains resource votes/ownership, including on uncertain SMC.
     * keep=0 follows a never-exposed abort or successful PAS shutdown. */
    void (*release)(void *,uint64_t epoch,int keep);
};
struct ceph_zap_pas_transport {
    void *ctx;
    int (*invoke)(void *,struct ceph_scm_regs *); /* exactly one native SMC */
};
struct ceph_zap_pas {
    enum ceph_zap_pas_phase phase,failed_phase;
    enum ceph_scm_convention convention;
    int error,memory_error;
    unsigned calls,exposed,owned;
    uint64_t epoch,transport,secure;
    struct ceph_zap_claim claim;
    struct ceph_zap_pas_resources resources;
};
static inline int ceph_zap_pas_regs(enum ceph_scm_convention c,enum ceph_zap_pas_command cmd,
    const struct ceph_zap_memory_view *v,struct ceph_scm_regs *r)
{
    if(!v || !r || (c!=CE_SCM_ARMV8_64 && c!=CE_SCM_ARMV8_32) ||
        !v->mdt_pa || !v->image_pa || (v->mdt_pa&4095u) || (v->image_pa&1048575u) ||
        v->mdt_capacity!=16384 || v->image_capacity!=1048576 || v->mdt_bytes!=6860 ||
        v->logical_bytes!=4096 || v->entry_offset ||
        v->mdt_pa>((1ULL<<40)-v->mdt_capacity) ||
        v->image_pa>((1ULL<<40)-v->image_capacity) ||
        (v->mdt_pa<v->image_pa+v->image_capacity && v->image_pa<v->mdt_pa+v->mdt_capacity) ||
        (c==CE_SCM_ARMV8_32 && (v->mdt_pa+v->mdt_capacity>(1ULL<<32) ||
            v->image_pa+v->image_capacity>(1ULL<<32))))return -1;
    *r=(struct ceph_scm_regs){{0x02000200u|(unsigned)cmd|
        (c==CE_SCM_ARMV8_64?0x40000000u:0),1,13,0,0,0,0,0}};
    switch(cmd) {
    case CE_ZP_CMD_INIT:r->x[1]=0x82;r->x[3]=v->mdt_pa;break;
    case CE_ZP_CMD_MEM:r->x[1]=3;r->x[3]=v->image_pa;r->x[4]=v->logical_bytes;break;
    case CE_ZP_CMD_AUTH:case CE_ZP_CMD_STOP:break;
    default:return -1;
    }
    return 0;
}
static inline int ceph_zap_pas_lease(struct ceph_zap_pas *s)
{return s->resources.lease(s->resources.ctx,&s->claim,s->epoch);}
static inline int ceph_zap_pas_call(struct ceph_zap_pas *s,struct ceph_zap_pas_transport *t,
    enum ceph_zap_pas_command cmd)
{
    struct ceph_scm_regs r;
    if(ceph_zap_pas_regs(s->convention,cmd,&s->claim.memory,&r))return -1;
    for(;;) {
        if(s->calls>=16 || ceph_zap_pas_lease(s))return -1;
        ++s->calls;
        if(t->invoke(t->ctx,&r))return -1;
        s->transport=r.x[0];s->secure=r.x[1];
        int32_t status=(int32_t)r.x[0];
        if(status==CE_SCM_INTERRUPTED)continue; /* resume unchanged x0..x7 */
        /* No BUSY retry or alternate ABI after any possible exposure. */
        return status || r.x[1]?-1:0;
    }
}
static inline int ceph_zap_pas_fail(struct ceph_zap_pas *s,int error)
{
    s->failed_phase=s->phase;s->error=error?error:-1;
    if(s->claim.handle)s->memory_error=ceph_zap_memory_finish(s->claim.handle,
        s->exposed?CE_ZAP_KEEP:CE_ZAP_CPU_ABORT);
    if(s->owned)s->resources.release(s->resources.ctx,s->epoch,s->exposed!=0);
    s->phase=s->exposed || s->memory_error?CE_ZP_RETAINED:CE_ZP_REFUSED;
    return s->error;
}
static inline int ceph_zap_pas_start(struct ceph_zap_pas *s,uint64_t snapshot,
    const struct ceph_scm_discovery *d,const struct ceph_zap_pas_resources *r,
    struct ceph_zap_pas_transport *t)
{
    if(!s || s->phase!=CE_ZP_NEW || s->claim.handle || s->calls || s->owned || s->exposed ||
        !d || !r || !r->acquire || !r->lease || !r->quiesce || !r->release || !t || !t->invoke)return -1;
    /* Admission is cached production discovery, not an additional phone test. */
    if(!d->terminal || d->error || d->supported!=1 || d->available_mask!=0x1f ||
        (d->convention!=CE_SCM_ARMV8_64 && d->convention!=CE_SCM_ARMV8_32))
        return ceph_zap_pas_fail(s,-1);
    s->convention=d->convention;s->resources=*r;s->phase=CE_ZP_MEMORY;
    int e=ceph_zap_memory_claim(snapshot,&s->claim);
    if(e)return ceph_zap_pas_fail(s,e);
    struct ceph_scm_regs check;
    if(ceph_zap_pas_regs(s->convention,CE_ZP_CMD_INIT,&s->claim.memory,&check))return ceph_zap_pas_fail(s,-1);
    s->phase=CE_ZP_OWNED;
    e=r->acquire(r->ctx,&s->claim,&s->epoch);
    if(e)return ceph_zap_pas_fail(s,e);
    s->owned=1;
    if(!s->epoch || ceph_zap_pas_lease(s))return ceph_zap_pas_fail(s,-1);
    if(ceph_zap_memory_expose(s->claim.handle))return ceph_zap_pas_fail(s,-1);
    s->exposed=1;s->phase=CE_ZP_INIT;
    if(ceph_zap_pas_call(s,t,CE_ZP_CMD_INIT))return ceph_zap_pas_fail(s,-1);
    s->phase=CE_ZP_MEM;
    if(ceph_zap_pas_call(s,t,CE_ZP_CMD_MEM))return ceph_zap_pas_fail(s,-1);
    s->phase=CE_ZP_IMAGE;
    if(ceph_zap_pas_lease(s) || ceph_zap_memory_reload(s->claim.handle))return ceph_zap_pas_fail(s,-1);
    s->phase=CE_ZP_AUTH;
    if(ceph_zap_pas_call(s,t,CE_ZP_CMD_AUTH) || ceph_zap_pas_lease(s))return ceph_zap_pas_fail(s,-1);
    e=ceph_zap_memory_finish(s->claim.handle,CE_ZAP_AUTHENTICATED);
    if(e)return ceph_zap_pas_fail(s,e);
    s->phase=CE_ZP_LIVE;s->error=0;return 0;
}
static inline int ceph_zap_pas_stop(struct ceph_zap_pas *s,struct ceph_zap_pas_transport *t)
{
    if(!s || s->phase!=CE_ZP_LIVE || !t || !t->invoke)return -1;
    s->phase=CE_ZP_STOP;
    if(ceph_zap_pas_lease(s) || s->resources.quiesce(s->resources.ctx,s->epoch) ||
        ceph_zap_pas_call(s,t,CE_ZP_CMD_STOP) || ceph_zap_pas_lease(s))return ceph_zap_pas_fail(s,-1);
    s->memory_error=ceph_zap_memory_finish(s->claim.handle,CE_ZAP_STOPPED);
    /* IOKit completion can fail even after secure shutdown. Retain the
     * uncertain backing; do not turn a cleanup failure into auth/stop success. */
    s->resources.release(s->resources.ctx,s->epoch,s->memory_error!=0);
    s->error=s->memory_error;s->phase=s->error?CE_ZP_RETAINED:CE_ZP_STOPPED;
    return s->error;
}
#if defined(KERNEL)
/* Serialized native one-shot entrypoints, no startup hook. Resource methods
 * must be real platform owners; no default implementation is installed. */
int cepheus_gpu_zap_load(uint64_t,const struct ceph_zap_pas_resources *,uint64_t *);
int cepheus_gpu_zap_stop(uint64_t);
int cepheus_gpu_zap_status(uint64_t,enum ceph_zap_pas_phase *);
/* Private admission snapshot: requires exact LIVE epoch and copied claim. */
int cepheus_gpu_zap_authenticated_status(uint64_t,struct ceph_zap_claim *,uint64_t *);
#endif
#endif
