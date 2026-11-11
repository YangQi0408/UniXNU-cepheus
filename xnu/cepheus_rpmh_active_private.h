/* Shared private ACTIVE sender in sweet_rpmh.c. No mapping or owner grant. */
#ifndef CEPHEUS_RPMH_ACTIVE_PRIVATE_H
#define CEPHEUS_RPMH_ACTIVE_PRIVATE_H
/* The sender allocates the identity; slot 0 is a real ARC table entry. */
#define CEPH_RPMH_ACTIVE_LEASE_MAGIC 0x41435456u /* "ACTV" */
enum ceph_rpmh_active_lease_state {
    CEPH_RPMH_ACTIVE_LEASE_EMPTY = 0,
    CEPH_RPMH_ACTIVE_LEASE_SELECTED = 1,
    CEPH_RPMH_ACTIVE_LEASE_ARMED = 2,
    CEPH_RPMH_ACTIVE_LEASE_ACKED = 3,
    CEPH_RPMH_ACTIVE_LEASE_CLEAN = 4,
    CEPH_RPMH_ACTIVE_LEASE_UNCERTAIN = 5
};
struct ceph_rpmh_active_command {uint32_t message,address,data;};
/* This record is owned by one sender call and is only exposed read-only to
 * finish. The identity is allocated here; callers cannot submit a lease to
 * the transport. */
struct ceph_rpmh_active_lease {
    uint32_t magic;
    uint64_t identity;
    enum ceph_rpmh_active_lease_state state;
    unsigned slot,count,wait,attempted_stores;
    struct ceph_rpmh_active_command command[8];
    uint32_t command_status[8];
    uint32_t irq_enable_before,irq_enable_masked,irq_enable_final,irq_bit,irq_status_last;
    uint32_t enable,wait_register,control;
};
struct ceph_rpmh_active_call {
    void *ctx;
    int (*validate)(void *);
    struct ceph_rpmh_active_command command[8];
    unsigned count,wait,attempted_stores;
    int reject_armed_bcm;
    struct ceph_rpmh_active_lease lease;
};
static int ceph_rpmh_active_lease(const struct ceph_rpmh_active_call *c)
{return !c->validate || c->validate(c->ctx)?EBUSY:0;}
static int ceph_rpmh_active_read(const struct ceph_rpmh_active_call *c,uint32_t off,uint32_t *v)
{
    if(ceph_rpmh_active_lease(c))return EBUSY;
    uint32_t value=rh_read(rh_tcs,off);
    if(ceph_rpmh_active_lease(c))return EBUSY;
    if(value==UINT32_MAX)return EIO;
    *v=value;return 0;
}
static int ceph_rpmh_active_write(struct ceph_rpmh_active_call *c,uint32_t off,uint32_t value,int sync)
{
    if(ceph_rpmh_active_lease(c))return EBUSY;
    if(ceph_rpmh_scope_effect())return EBUSY;
    rh_poisoned=true;++c->attempted_stores;c->lease.attempted_stores=c->attempted_stores;
    rh_write(rh_tcs,off,value);
    if(!sync)return ceph_rpmh_active_lease(c);
    for(unsigned us=0;us<1000;++us) {
        uint32_t observed;int e=ceph_rpmh_active_read(c,off,&observed);
        if(e)return e;
        if(observed==value)return 0;
        delay(1);
    }
    return ETIMEDOUT;
}
static int ceph_rpmh_active_empty(const struct ceph_rpmh_active_call *c,unsigned t)
{
    uint32_t b=t*RH_TCS_STRIDE,status,control,enable,wait,irq;
    int e=ceph_rpmh_active_read(c,b+RH_STATUS,&status);
    if(!e)e=ceph_rpmh_active_read(c,b+RH_CONTROL,&control);
    if(!e)e=ceph_rpmh_active_read(c,b+RH_ENABLE,&enable);
    if(!e)e=ceph_rpmh_active_read(c,b+RH_WAIT,&wait);
    if(!e)e=ceph_rpmh_active_read(c,RH_IRQ_STATUS,&irq);
    if(e)return e;
    return status && !control && !enable && !wait && !(irq&(1u<<t))?0:ENOSPC;
}
static int ceph_rpmh_active_available(const struct ceph_rpmh_active_call *c,int expected,int *chosen)
{
    *chosen=-1;
    if(expected>=(int)RH_ACTIVE_N)return EINVAL;
    for(unsigned t=0;t<RH_NORMAL_N;++t) {
        uint32_t b=t*RH_TCS_STRIDE,enabled;
        int e=ceph_rpmh_active_read(c,b+RH_ENABLE,&enabled);if(e)return e;
        if(enabled&~((1u<<rh_ncpt)-1u))return EIO;
        for(unsigned n=0;n<rh_ncpt;++n)if(enabled&(1u<<n)) {
            uint32_t address;e=ceph_rpmh_active_read(c,b+RH_ADDR+n*RH_CMD_STRIDE,&address);
            if(e)return e;
            /* Any armed BCM can have an uncommitted VCD dependency. */
            if(c->reject_armed_bcm && (address>>16)==5)return EBUSY;
            for(unsigned i=0;i<c->count;++i)
                if(address==c->command[i].address)return EBUSY;
        }
        if(t<RH_ACTIVE_N && (expected<0 || (int)t==expected) && *chosen<0) {
            e=ceph_rpmh_active_empty(c,t);
            if(!e)*chosen=(int)t;else if(e!=ENOSPC)return e;
        }
    }
    if(expected>=0 && *chosen!=expected)return EBUSY;
    return *chosen<0?EBUSY:0;
}
static int ceph_rpmh_active_send_body(struct ceph_rpmh_active_call *c)
{
    if(rh_ncpt<3 || rh_ncpt>16 || !c->count || c->count>8 || c->count>rh_ncpt ||
        !c->wait || (c->wait&~((1u<<c->count)-1u)))return EINVAL;
    int chosen,e=ceph_rpmh_active_available(c,-1,&chosen);if(e)return e;
    unsigned t=(unsigned)chosen;uint32_t b=t*RH_TCS_STRIDE,bit=1u<<t,irq_en;
    c->lease.slot=t;c->lease.state=CEPH_RPMH_ACTIVE_LEASE_SELECTED;
    e=ceph_rpmh_active_read(c,RH_IRQ_EN,&irq_en);if(e)return e;
    c->lease.irq_enable_before=irq_en;c->lease.irq_bit=bit;
    e=ceph_rpmh_active_empty(c,t);if(e)return e;
    e=ceph_rpmh_active_write(c,RH_IRQ_EN,irq_en&~bit,1);if(e)return e;
    c->lease.state=CEPH_RPMH_ACTIVE_LEASE_ARMED;
    int again;e=ceph_rpmh_active_available(c,(int)t,&again);if(e)return e;
    if(again!=(int)t)return EBUSY;
    e=ceph_rpmh_active_empty(c,t);if(e)return e;
    const uint32_t offsets[]={RH_MSGID,RH_ADDR,RH_DATA};
    for(unsigned n=0;n<c->count;++n) {
        const uint32_t values[]={c->command[n].message,c->command[n].address,c->command[n].data};
        for(unsigned i=0;i<3;++i) {
            e=ceph_rpmh_active_write(c,b+n*RH_CMD_STRIDE+offsets[i],values[i],1);if(e)return e;
        }
    }
    uint32_t enabled=(1u<<c->count)-1u;
    c->lease.wait_register=c->wait;c->lease.enable=enabled;
    const uint32_t control_off[]={RH_WAIT,RH_ENABLE,RH_CONTROL};
    const uint32_t control_value[]={c->wait,enabled,RH_AMC_EN};
    for(unsigned i=0;i<3;++i) {
        e=ceph_rpmh_active_write(c,b+control_off[i],control_value[i],1);if(e)return e;
    }
    e=ceph_rpmh_active_write(c,b+RH_CONTROL,RH_AMC_EN|RH_TRIGGER,0);if(e)return e;
    int done=0;
    for(unsigned us=0;us<RH_TIMEOUT_US;++us) {
        uint32_t irq,idle;
        e=ceph_rpmh_active_read(c,RH_IRQ_STATUS,&irq);if(e)return e;
        e=ceph_rpmh_active_read(c,b+RH_STATUS,&idle);if(e)return e;
        int complete=1;
        for(unsigned n=0;n<c->count;++n) {
            uint32_t status;e=ceph_rpmh_active_read(c,b+RH_CMD_STATUS+n*RH_CMD_STRIDE,&status);
            if(e)return e;
            c->lease.command_status[n]=status;c->lease.irq_status_last=irq;
            if(!(status&RH_ISSUED) || ((c->wait&(1u<<n)) && !(status&RH_COMPLETE)))complete=0;
        }
        if((irq&bit) && idle && complete){done=1;break;}
        delay(1);
    }
    if(!done)return ETIMEDOUT;
    c->lease.state=CEPH_RPMH_ACTIVE_LEASE_ACKED;
    for(unsigned n=0;n<c->count;++n) {
        const uint32_t values[]={c->command[n].message,c->command[n].address,c->command[n].data};
        for(unsigned i=0;i<3;++i) {
            uint32_t observed;e=ceph_rpmh_active_read(c,b+n*RH_CMD_STRIDE+offsets[i],&observed);
            if(e)return e;
            if(observed!=values[i])return EBUSY;
        }
    }
    uint32_t observed_enable,wait,control;
    e=ceph_rpmh_active_read(c,b+RH_ENABLE,&observed_enable);if(e)return e;
    e=ceph_rpmh_active_read(c,b+RH_WAIT,&wait);if(e)return e;
    e=ceph_rpmh_active_read(c,b+RH_CONTROL,&control);if(e)return e;
    c->lease.enable=observed_enable;c->lease.wait_register=wait;c->lease.control=control;
    if(observed_enable!=enabled || wait!=c->wait || control!=(RH_AMC_EN|RH_TRIGGER))return EBUSY;
    e=ceph_rpmh_active_write(c,b+RH_CONTROL,RH_AMC_EN,1);if(e)return e;
    e=ceph_rpmh_active_write(c,b+RH_CONTROL,0,1);if(e)return e;
    e=ceph_rpmh_active_write(c,b+RH_ENABLE,0,1);if(e)return e;
    e=ceph_rpmh_active_write(c,b+RH_WAIT,0,1);if(e)return e;
    e=ceph_rpmh_active_write(c,RH_IRQ_CLEAR,bit,0);if(e)return e;
    uint32_t irq;e=ceph_rpmh_active_read(c,RH_IRQ_STATUS,&irq);if(e)return e;
    c->lease.irq_status_last=irq;
    if(irq&bit)return EIO;
    e=ceph_rpmh_active_read(c,RH_IRQ_EN,&irq);if(e)return e;
    c->lease.irq_enable_masked=irq;
    e=ceph_rpmh_active_write(c,RH_IRQ_EN,(irq&~bit)|(irq_en&bit),1);if(e)return e;
    c->lease.irq_enable_final=(irq&~bit)|(irq_en&bit);
    e=ceph_rpmh_active_empty(c,t);if(e)return e;
    c->lease.state=CEPH_RPMH_ACTIVE_LEASE_CLEAN;
    rh_poisoned=false;return 0;
}
static uint64_t ceph_rpmh_active_lease_epoch;
static int ceph_rpmh_active_send(struct ceph_rpmh_active_call *c)
{
    c->lease.magic=CEPH_RPMH_ACTIVE_LEASE_MAGIC;
    c->lease.identity=++ceph_rpmh_active_lease_epoch;
    c->lease.state=CEPH_RPMH_ACTIVE_LEASE_EMPTY;
    c->lease.slot=UINT32_MAX;c->lease.count=c->count;c->lease.wait=c->wait;
    c->lease.attempted_stores=c->attempted_stores;
    for(unsigned n=0;n<8;++n)c->lease.command[n]=c->command[n];
    int e=ceph_rpmh_active_send_body(c);
    if(e && c->attempted_stores)c->lease.state=CEPH_RPMH_ACTIVE_LEASE_UNCERTAIN;
    return e;
}
#endif
