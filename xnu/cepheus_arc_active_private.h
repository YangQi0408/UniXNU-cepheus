/* Included by sweet_rpmh.c after its definitions. Shares rh_lock/rh_poisoned
 * with touch power. No startup caller; no sleeping/hidden TCS or global reset.
 */
#ifndef CEPHEUS_ARC_ACTIVE_PRIVATE_H
#define CEPHEUS_ARC_ACTIVE_PRIVATE_H
#include "cepheus_arc_active.h"
#include "cepheus_rpmh_active_private.h"

struct ceph_arc_active_call {
    struct ceph_arc_active_owner owner;
    struct ceph_arc_resource resource;
    unsigned current,target;
    int db_failed;
};
static int ceph_arc_active_lease(void *ctx)
{
    const struct ceph_arc_active_call *c=ctx;
    return c->owner.validate_access(c->owner.ctx) ||
        c->owner.validate(c->owner.ctx,&c->resource,c->current,c->target)?EBUSY:0;
}
static uint32_t ceph_arc_active_db_read(void *ctx,uint32_t off)
{
    struct ceph_arc_active_call *c=ctx;
    if(c->db_failed || c->owner.validate_access(c->owner.ctx)){c->db_failed=1;return UINT32_MAX;}
    uint32_t value=rh_read(rh_db,off);
    if(c->owner.validate_access(c->owner.ctx)){c->db_failed=1;return UINT32_MAX;}
    if(value==UINT32_MAX)c->db_failed=1;
    return value;
}
int cepheus_gpu_arc_raise(const char *name,unsigned current,unsigned target,
    const struct ceph_arc_active_owner *owner)
{
    if(!name || (strcmp(name,"cx.lvl") && strcmp(name,"mx.lvl")) ||
        !owner || !owner->validate || !owner->validate_access || !owner->finish ||
        current>=16 || target>=16 || target<=current)return EINVAL;
    static const char resource_names[2][9]={"cx.lvl","mx.lvl"};
    const char *resource_name=resource_names[!!strcmp(name,"cx.lvl")];
    struct ceph_arc_active_call c={.owner=*owner,.current=current,.target=target};
    if(!ml_get_interrupts_enabled() || ml_at_interrupt_context() || get_preemption_level())return EBUSY;
    if(__atomic_exchange_n(&rh_lock,1,__ATOMIC_ACQUIRE))return EBUSY;
    int e=EIO;
    struct ceph_rpmh_active_call call={.ctx=&c,.validate=ceph_arc_active_lease,.count=1,.wait=1};
    if(rh_poisoned)goto done;
    if(!rh_rsc || !rh_tcs || !rh_db || !rh_db_size){e=ENODEV;goto done;}
    if(c.owner.validate_access(c.owner.ctx)){e=EBUSY;goto done;}
    struct ceph_arc_db db={&c,rh_db_size,ceph_arc_active_db_read};
    e=ceph_arc_lookup(&db,resource_name,&c.resource);
    if(c.db_failed){e=EBUSY;goto done;}
    if(e || current>=c.resource.count || target>=c.resource.count){e=EINVAL;goto done;}
    e=ceph_arc_active_lease(&c);if(e)goto done;
    call.command[0]=(struct ceph_rpmh_active_command){RH_WRITE_ACK,c.resource.address,target};
    e=ceph_rpmh_active_send(&call);
done:
    if(call.attempted_stores)c.owner.finish(c.owner.ctx,call.attempted_stores,e!=0);
    __atomic_store_n(&rh_lock,0,__ATOMIC_RELEASE);
    return e==ENOSPC?EBUSY:e;
}
#endif
