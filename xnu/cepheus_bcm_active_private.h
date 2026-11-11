/* Included after ARC in the actual sweet_rpmh.c translation unit. */
#ifndef CEPHEUS_BCM_ACTIVE_PRIVATE_H
#define CEPHEUS_BCM_ACTIVE_PRIVATE_H
#include "cepheus_bcm_active.h"
#include "cepheus_rpmh_scope.h"
#include "cepheus_rpmh_active_private.h"
struct ceph_bcm_active_call {
    struct ceph_bcm_active_owner owner;
    struct ceph_bcm_active_vote votes[CE_BA_NRESOURCE];
    struct ceph_bcm_resource resources[CE_BA_NRESOURCE];
    unsigned count;
    int db_failed;
};
static int ceph_bcm_active_lease(void *ctx)
{
    const struct ceph_bcm_active_call *c=ctx;
    if(cepheus_gpu_rpmh_check(c->owner.rpmh_token) || c->owner.validate(c->owner.ctx))return EBUSY;
    for(unsigned n=0;n<c->count;++n) {
        const struct ceph_bcm_active_vote *v=&c->votes[n];
        if(c->owner.current(c->owner.ctx,v->id,&c->resources[n],v->current_x,v->current_y))return EBUSY;
    }
    return 0;
}
static uint32_t ceph_bcm_active_db_read(void *ctx,uint32_t off)
{
    struct ceph_bcm_active_call *c=ctx;
    if(c->db_failed || cepheus_gpu_rpmh_check(c->owner.rpmh_token) || c->owner.validate(c->owner.ctx)){c->db_failed=1;return UINT32_MAX;}
    uint32_t value=rh_read(rh_db,off);
    if(cepheus_gpu_rpmh_check(c->owner.rpmh_token) || c->owner.validate(c->owner.ctx)){c->db_failed=1;return UINT32_MAX;}
    if(value==UINT32_MAX)c->db_failed=1;
    return value;
}
int cepheus_gpu_bcm_raise(const struct ceph_bcm_active_vote *votes,unsigned count,
    const struct ceph_bcm_active_owner *owner)
{
    if(!votes || !count || count>CE_BA_NRESOURCE || !owner || !owner->validate ||
        !owner->current || !owner->finish)return EINVAL;
    struct ceph_bcm_active_call c={.owner=*owner,.count=count};
    for(unsigned n=0;n<count;++n) {
        c.votes[n]=votes[n];const struct ceph_bcm_active_vote *v=&c.votes[n];
        if(v->id<0 || v->id>=CE_BA_NRESOURCE || v->current_x>0x3fff || v->current_y>0x3fff ||
            v->target_x>0x3fff || v->target_y>0x3fff || v->target_x<v->current_x ||
            v->target_y<v->current_y || (v->target_x==v->current_x && v->target_y==v->current_y))return EINVAL;
        if(v->id==CE_BA_ACV && (v->current_x || v->target_x || v->current_y || v->target_y!=8))return EINVAL;
        for(unsigned j=0;j<n;++j)if(v->id==c.votes[j].id)return EINVAL;
    }
    if(!ml_get_interrupts_enabled() || ml_at_interrupt_context() || get_preemption_level())return EBUSY;
    if(__atomic_exchange_n(&rh_lock,1,__ATOMIC_ACQUIRE))return EBUSY;
    int e=EIO;
    struct ceph_rpmh_active_call call={.ctx=&c,.validate=ceph_bcm_active_lease,
        .count=count,.reject_armed_bcm=1};
    if(rh_poisoned)goto done;
    if(cepheus_gpu_rpmh_check(c.owner.rpmh_token)){e=EBUSY;goto done;}
    /* Do not map/read shared hardware outside the supplied ownership guard. */
    if(!rh_rsc || !rh_tcs || !rh_db || !rh_db_size){e=ENODEV;goto done;}
    if(c.owner.validate(c.owner.ctx)){e=EBUSY;goto done;}
    static const char *const names[] = {"SH2","SN15","SN3","SH0","MC0","ACV"};
    struct ceph_arc_db db={&c,rh_db_size,ceph_bcm_active_db_read};
    struct ceph_bcm_vote encoded[CE_BA_NRESOURCE];
    int acv=-1;
    for(unsigned n=0;n<count;++n) {
        enum ceph_bcm_active_id id=c.votes[n].id;
        e=id==CE_BA_ACV?ceph_bcm_lookup_acv(&db,&c.resources[n]):ceph_bcm_lookup(&db,names[id],&c.resources[n]);
        if(c.db_failed){e=EBUSY;goto done;}
        if(e){e=EINVAL;goto done;}
        encoded[n]=(struct ceph_bcm_vote){c.resources[n],c.votes[n].target_x,c.votes[n].target_y};
        if(id==CE_BA_ACV)acv=(int)n;
    }
    struct ceph_bcm_command commands[CE_BA_NRESOURCE];uint32_t wait;
    if(ceph_bcm_commands_kind(encoded,count,commands,CE_BA_NRESOURCE,&wait,acv)!=(int)count){e=EINVAL;goto done;}
    call.wait=wait;
    for(unsigned n=0;n<count;++n)
        call.command[n]=(struct ceph_rpmh_active_command){
            8u|RH_ISSUED|((wait&(1u<<n))?RH_COMPLETE:0),commands[n].address,commands[n].data};
    e=ceph_bcm_active_lease(&c);if(e)goto done;
    e=ceph_rpmh_active_send(&call);
done:
    if(call.attempted_stores || (call.lease.state==CEPH_RPMH_ACTIVE_LEASE_ARMED && e))
        c.owner.finish(c.owner.ctx,&call.lease,e!=0);
    __atomic_store_n(&rh_lock,0,__ATOMIC_RELEASE);
    return e==ENOSPC?EBUSY:e;
}
#endif
