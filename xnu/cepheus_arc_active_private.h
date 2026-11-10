/* Included by sweet_rpmh.c after its definitions. Shares rh_lock/rh_poisoned
 * with touch power. No startup caller; no sleeping/hidden TCS or global reset.
 */
#ifndef CEPHEUS_ARC_ACTIVE_PRIVATE_H
#define CEPHEUS_ARC_ACTIVE_PRIVATE_H
#include "cepheus_arc_active.h"
#include "cepheus_rpmh_active_private.h"

struct ceph_arc_active_call {
    const struct ceph_arc_active_owner *owner;
    struct ceph_arc_resource resource;
    unsigned current,target;
};
static int ceph_arc_active_lease(void *ctx)
{
    const struct ceph_arc_active_call *c=ctx;
    return c->owner->validate(c->owner->ctx,&c->resource,c->current,c->target)?EBUSY:0;
}
static uint32_t ceph_arc_active_db_read(void *ctx __unused,uint32_t off)
{return rh_read(rh_db,off);}
int cepheus_gpu_arc_raise(const char *name,unsigned current,unsigned target,
    const struct ceph_arc_active_owner *owner)
{
    if(!name || (strcmp(name,"cx.lvl") && strcmp(name,"mx.lvl")) ||
        !owner || !owner->validate || current>=16 || target>=16 || target<=current)return EINVAL;
    if(!ml_get_interrupts_enabled() || ml_at_interrupt_context() || get_preemption_level())return EBUSY;
    if(__atomic_exchange_n(&rh_lock,1,__ATOMIC_ACQUIRE))return EBUSY;
    int e=EIO;
    if(!rh_poisoned) {
        e=rh_map();
        if(!e) {
            struct ceph_arc_db db={NULL,rh_db_size,ceph_arc_active_db_read};
            struct ceph_arc_active_call c={owner,{0},current,target};
            e=ceph_arc_lookup(&db,name,&c.resource);
            if(e)e=EINVAL;
            else if(target>=c.resource.count)e=EINVAL;
            else if(!(e=ceph_arc_active_lease(&c))) {
                struct ceph_rpmh_active_call call={.ctx=&c,.validate=ceph_arc_active_lease,
                    .command={{RH_WRITE_ACK,c.resource.address,target}},.count=1,.wait=1};
                e=ceph_rpmh_active_send(&call);
            }
        }
    }
    __atomic_store_n(&rh_lock,0,__ATOMIC_RELEASE);
    return e==ENOSPC?EBUSY:e;
}
#endif
