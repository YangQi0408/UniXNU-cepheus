/* Included by sweet_rpmh.c after its definitions. Shares rh_lock/rh_poisoned
 * with touch power. No startup caller; no sleeping/hidden TCS or global reset.
 */
#ifndef CEPHEUS_ARC_ACTIVE_PRIVATE_H
#define CEPHEUS_ARC_ACTIVE_PRIVATE_H
#include "cepheus_arc_active.h"

struct ceph_arc_active_call {
    const struct ceph_arc_active_owner *owner;
    struct ceph_arc_resource resource;
    unsigned current,target;
};
static int ceph_arc_active_lease(const struct ceph_arc_active_call *c)
{
    return c->owner->validate(c->owner->ctx,&c->resource,c->current,c->target)?EBUSY:0;
}
static int ceph_arc_active_read(const struct ceph_arc_active_call *c,uint32_t off,uint32_t *v)
{
    if(ceph_arc_active_lease(c))return EBUSY;
    *v=rh_read(rh_tcs,off);
    return *v==UINT32_MAX?EIO:0;
}
static int ceph_arc_active_write(const struct ceph_arc_active_call *c,uint32_t off,uint32_t value,int sync)
{
    if(ceph_arc_active_lease(c))return EBUSY;
    rh_poisoned=true; /* includes IRQ mask and failed/readback-uncertain writes */
    rh_write(rh_tcs,off,value);
    if(!sync)return 0;
    for(unsigned us=0;us<1000;++us) {
        uint32_t observed;int e=ceph_arc_active_read(c,off,&observed);
        if(e)return e;
        if(observed==value)return 0;
        delay(1);
    }
    return ETIMEDOUT;
}
static int ceph_arc_active_empty(const struct ceph_arc_active_call *c,unsigned t)
{
    uint32_t b=t*RH_TCS_STRIDE,status,control,enable,wait,irq;
    int e=ceph_arc_active_read(c,b+RH_STATUS,&status);
    if(!e)e=ceph_arc_active_read(c,b+RH_CONTROL,&control);
    if(!e)e=ceph_arc_active_read(c,b+RH_ENABLE,&enable);
    if(!e)e=ceph_arc_active_read(c,b+RH_WAIT,&wait);
    if(!e)e=ceph_arc_active_read(c,RH_IRQ_STATUS,&irq);
    if(e)return e;
    return status && !control && !enable && !wait && !(irq&(1u<<t))?0:ENOSPC;
}
static int ceph_arc_active_send(const struct ceph_arc_active_call *c)
{
    if(rh_ncpt<3 || rh_ncpt>16)return EINVAL;
    int chosen=-1;
    /* Idle payload is not a current vote. Reject armed requests in ALL normal
     * banks, including sleep/wake, without changing them or inferring zeros. */
    for(unsigned t=0;t<RH_NORMAL_N;++t) {
        uint32_t b=t*RH_TCS_STRIDE,enabled;
        int e=ceph_arc_active_read(c,b+RH_ENABLE,&enabled);if(e)return e;
        if(enabled&~((1u<<rh_ncpt)-1u))return EIO;
        for(unsigned n=0;n<rh_ncpt;++n)if(enabled&(1u<<n)) {
            uint32_t address;e=ceph_arc_active_read(c,b+RH_ADDR+n*RH_CMD_STRIDE,&address);
            if(e)return e;
            if(address==c->resource.address)return EBUSY;
        }
        if(t<RH_ACTIVE_N && chosen<0) {
            e=ceph_arc_active_empty(c,t);
            if(!e)chosen=(int)t;else if(e!=ENOSPC)return e;
        }
    }
    if(chosen<0)return EBUSY;
    unsigned t=(unsigned)chosen;uint32_t b=t*RH_TCS_STRIDE,bit=1u<<t,irq_en;
    int e=ceph_arc_active_read(c,RH_IRQ_EN,&irq_en);if(e)return e;
    e=ceph_arc_active_empty(c,t);if(e)return e;
    e=ceph_arc_active_write(c,RH_IRQ_EN,irq_en&~bit,1);if(e)return e;
    /* A late foreign change is quarantined, not acknowledged or repaired. */
    e=ceph_arc_active_empty(c,t);if(e)return e;
    const uint32_t offsets[]={RH_MSGID,RH_ADDR,RH_DATA,RH_WAIT,RH_ENABLE,RH_CONTROL};
    const uint32_t values[]={RH_WRITE_ACK,c->resource.address,c->target,1,1,RH_AMC_EN};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);++i) {
        e=ceph_arc_active_write(c,b+offsets[i],values[i],1);if(e)return e;
    }
    e=ceph_arc_active_write(c,b+RH_CONTROL,RH_AMC_EN|RH_TRIGGER,0);if(e)return e;
    int done=0;
    for(unsigned us=0;us<RH_TIMEOUT_US;++us) {
        uint32_t irq,idle,status;
        e=ceph_arc_active_read(c,RH_IRQ_STATUS,&irq);if(e)return e;
        e=ceph_arc_active_read(c,b+RH_STATUS,&idle);if(e)return e;
        e=ceph_arc_active_read(c,b+RH_CMD_STATUS,&status);if(e)return e;
        /* Fresh IRQ was absent before trigger. Stale CMD_STATUS or observed
         * busy transitions alone never satisfy this completion predicate. */
        if((irq&bit) && idle && (status&(RH_ISSUED|RH_COMPLETE))==(RH_ISSUED|RH_COMPLETE)) {
            done=1;break;
        }
        delay(1);
    }
    if(!done)return ETIMEDOUT;
    for(unsigned i=0;i<3;++i) {
        uint32_t observed;e=ceph_arc_active_read(c,b+offsets[i],&observed);
        if(e)return e;
        if(observed!=values[i])return EBUSY;
    }
    uint32_t enabled,wait,control;
    e=ceph_arc_active_read(c,b+RH_ENABLE,&enabled);if(e)return e;
    e=ceph_arc_active_read(c,b+RH_WAIT,&wait);if(e)return e;
    e=ceph_arc_active_read(c,b+RH_CONTROL,&control);if(e)return e;
    if(enabled!=1 || wait!=1 || control!=(RH_AMC_EN|RH_TRIGGER))return EBUSY;
    e=ceph_arc_active_write(c,b+RH_CONTROL,RH_AMC_EN,1);if(e)return e;
    e=ceph_arc_active_write(c,b+RH_CONTROL,0,1);if(e)return e;
    e=ceph_arc_active_write(c,b+RH_ENABLE,0,1);if(e)return e;
    e=ceph_arc_active_write(c,b+RH_WAIT,0,1);if(e)return e;
    e=ceph_arc_active_write(c,RH_IRQ_CLEAR,bit,0);if(e)return e;
    uint32_t irq;e=ceph_arc_active_read(c,RH_IRQ_STATUS,&irq);if(e)return e;
    if(irq&bit)return EIO;
    /* Restore only our source, preserving unrelated IRQ enables. */
    e=ceph_arc_active_read(c,RH_IRQ_EN,&irq);if(e)return e;
    e=ceph_arc_active_write(c,RH_IRQ_EN,(irq&~bit)|(irq_en&bit),1);if(e)return e;
    e=ceph_arc_active_empty(c,t);if(e)return e;
    rh_poisoned=false;return 0;
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
            else if(!(e=ceph_arc_active_lease(&c)))e=ceph_arc_active_send(&c);
        }
    }
    __atomic_store_n(&rh_lock,0,__ATOMIC_RELEASE);
    return e==ENOSPC?EBUSY:e;
}
#endif
