#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#define __unused __attribute__((unused))
#define RH_TCS_STRIDE 0x2a0u
#define RH_CMD_STRIDE 0x14u
#define RH_IRQ_EN 0u
#define RH_IRQ_STATUS 4u
#define RH_IRQ_CLEAR 8u
#define RH_WAIT 0x10u
#define RH_CONTROL 0x14u
#define RH_STATUS 0x18u
#define RH_ENABLE 0x1cu
#define RH_MSGID 0x30u
#define RH_ADDR 0x34u
#define RH_DATA 0x38u
#define RH_CMD_STATUS 0x3cu
#define RH_AMC_EN (1u<<16)
#define RH_TRIGGER (1u<<24)
#define RH_ISSUED (1u<<8)
#define RH_COMPLETE (1u<<16)
#define RH_WRITE_ACK (8u|RH_ISSUED|RH_COMPLETE)
#define RH_ACTIVE_N 2u
#define RH_NORMAL_N 8u
#define RH_TIMEOUT_US 100000u
static uintptr_t rh_tcs=1,rh_db=2;
static uint32_t rh_db_size=256,tcs[0x3000/4],db[256/4];
static unsigned rh_ncpt=16,rh_lock,reads,writes,lease_calls,fail_lease,fail_write;
static bool rh_poisoned;
static int context,map_error,complete,corrupt_payload,triggered;
static unsigned elapsed,slot;
static int ml_get_interrupts_enabled(void){return context!=1;}
static int ml_at_interrupt_context(void){return context==2;}
static int get_preemption_level(void){return context==3;}
static int rh_map(void){return map_error;}
static uint32_t rh_read(uintptr_t base,uint32_t off)
{
    assert(!(off&3));++reads;
    if(base==rh_db){assert(off<sizeof(db));return db[off/4];}
    assert(base==rh_tcs && off<sizeof(tcs));
    if(fail_write && writes==fail_write)return UINT32_MAX;
    return tcs[off/4];
}
static void rh_write(uintptr_t base,uint32_t off,uint32_t value)
{
    assert(base==rh_tcs && !(off&3) && off<sizeof(tcs));++writes;
    assert(off<8*RH_TCS_STRIDE);
    if(off>RH_IRQ_CLEAR)assert(off/RH_TCS_STRIDE<RH_ACTIVE_N);
    if(off==RH_IRQ_CLEAR)tcs[RH_IRQ_STATUS/4]&=~value;
    else tcs[off/4]=value;
    if(off%RH_TCS_STRIDE==RH_CONTROL && value==(RH_AMC_EN|RH_TRIGGER)) {
        slot=off/RH_TCS_STRIDE;triggered=1;tcs[(slot*RH_TCS_STRIDE+RH_STATUS)/4]=0;
    }
}
static void delay(unsigned us)
{
    assert(us==1);elapsed+=us;
    if(triggered && complete) {
        unsigned b=slot*RH_TCS_STRIDE;
        tcs[(b+RH_STATUS)/4]=1;
        tcs[(b+RH_CMD_STATUS)/4]=RH_ISSUED|RH_COMPLETE;
        if(complete==1)tcs[RH_IRQ_STATUS/4]|=1u<<slot;
        if(corrupt_payload==1)tcs[(b+RH_DATA)/4]=15;
        if(corrupt_payload==2)tcs[(b+RH_ENABLE)/4]=3;
        if(corrupt_payload==3)tcs[(b+RH_WAIT)/4]=3;
        if(corrupt_payload==4)tcs[(b+RH_CONTROL)/4]=0;
    }
}
#include "cepheus_arc_active_private.h"
static int validate(void *ctx,const struct ceph_arc_resource *r,unsigned current,unsigned target)
{
    assert(ctx==tcs && r->address==0x30010 && r->count==4 && current==1 && target==2);
    ++lease_calls;return fail_lease && lease_calls>=fail_lease?-1:0;
}
static const struct ceph_arc_active_owner owner={tcs,validate};
static void init(void)
{
    memset(tcs,0,sizeof(tcs));memset(db,0,sizeof(db));
    db[0]=1;db[1]=0x0c0330db;db[2]=3;db[3]=(1u<<16)|24u;
    memcpy((uint8_t *)db+144,"cx.lvl",6);db[160/4]=0x30010;db[164/4]=8;
    db[168/4]=16u<<16;db[172/4]=48u|(64u<<16);
    for(unsigned t=0;t<RH_NORMAL_N;++t)tcs[(t*RH_TCS_STRIDE+RH_STATUS)/4]=1;
    tcs[RH_IRQ_EN/4]=0xa5;tcs[RH_IRQ_STATUS/4]=0x80;
    tcs[RH_CMD_STATUS/4]=RH_ISSUED|RH_COMPLETE; /* stale from an older request */
    rh_ncpt=16;rh_lock=0;rh_poisoned=false;reads=writes=lease_calls=fail_lease=fail_write=0;
    context=map_error=corrupt_payload=triggered=0;complete=1;elapsed=slot=0;
}
static int run(void){return cepheus_gpu_arc_raise("cx.lvl",1,2,&owner);}
int main(void)
{
    init();assert(!run() && !rh_poisoned && !rh_lock && writes && elapsed==1);
    assert(tcs[RH_IRQ_EN/4]==0xa5 && tcs[RH_IRQ_STATUS/4]==0x80);
    assert(!tcs[RH_CONTROL/4] && !tcs[RH_WAIT/4] && !tcs[RH_ENABLE/4]);
    assert(tcs[RH_DATA/4]==2 && tcs[RH_MSGID/4]==RH_WRITE_ACK);
    unsigned success_leases=lease_calls,success_writes=writes;
    /* Fail each lease boundary of the successful path. No store follows a
     * revoked capability; any earlier write permanently poisons transport. */
    for(unsigned n=1;n<=success_leases;++n) {
        init();fail_lease=n;int e=run();
        if(e!=EBUSY || lease_calls!=n || rh_lock)fprintf(stderr,"lease %u: e=%d calls=%u writes=%u\n",n,e,lease_calls,writes);
        assert(e==EBUSY && lease_calls==n && !rh_lock);
        assert(rh_poisoned==(writes!=0));
        unsigned before=writes;assert(run()!=0 && writes==before);
    }
    for(unsigned n=1;n<=success_writes;++n) {
        init();fail_write=n;assert(run()==EIO && rh_poisoned && !rh_lock);
        assert(writes==n);unsigned before=reads;assert(run()==EIO && reads==before);
    }
    for(unsigned t=0;t<RH_NORMAL_N;++t) {
        init();unsigned b=t*RH_TCS_STRIDE;
        tcs[(b+RH_ENABLE)/4]=1u<<15;tcs[(b+RH_ADDR+15*RH_CMD_STRIDE)/4]=0x30010;
        assert(run()==EBUSY && !writes && !rh_poisoned);
    }
    init();tcs[RH_CONTROL/4]=RH_AMC_EN;tcs[RH_ENABLE/4]=1;
    tcs[RH_ADDR/4]=0x40000;assert(!run() && slot==1);
    assert(tcs[RH_CONTROL/4]==RH_AMC_EN && tcs[RH_ENABLE/4]==1 && tcs[RH_ADDR/4]==0x40000);
    init();tcs[RH_CONTROL/4]=1;tcs[(RH_TCS_STRIDE+RH_CONTROL)/4]=1;
    assert(run()==EBUSY && !writes && !rh_poisoned);
    init();complete=2;assert(run()==ETIMEDOUT && rh_poisoned && elapsed==RH_TIMEOUT_US);
    assert(tcs[RH_CONTROL/4]==(RH_AMC_EN|RH_TRIGGER) && tcs[RH_ENABLE/4]==1 && !(tcs[RH_IRQ_EN/4]&1));
    init();complete=0;assert(run()==ETIMEDOUT && rh_poisoned);
    for(int n=1;n<=4;++n){init();corrupt_payload=n;assert(run()==EBUSY && rh_poisoned && tcs[RH_ENABLE/4]);}
    for(int n=1;n<=3;++n){init();context=n;assert(run()==EBUSY && !reads && !writes);}
    init();rh_lock=1;assert(run()==EBUSY && rh_lock==1 && !reads);
    init();rh_poisoned=true;assert(run()==EIO && !reads && !writes && !rh_lock);
    init();map_error=ENOMEM;assert(run()==ENOMEM && !reads && !writes);
    init();db[164/4]=7;assert(run()==EINVAL && !writes);
    init();db[160/4]=0x30011;assert(run()==EINVAL && !writes);
    init();rh_ncpt=17;assert(run()==EINVAL && !writes);
    init();tcs[RH_ENABLE/4]=UINT32_MAX;assert(run()==EIO && !writes && !rh_poisoned);
    init();assert(cepheus_gpu_arc_raise("gfx.lvl",1,2,&owner)==EINVAL && !reads);
    assert(cepheus_gpu_arc_raise("cx.lvl",2,1,&owner)==EINVAL && !reads);
    assert(cepheus_gpu_arc_raise("cx.lvl",1,1,&owner)==EINVAL && !reads);
    assert(cepheus_gpu_arc_raise("cx.lvl",1,16,&owner)==EINVAL && !reads);
    assert(cepheus_gpu_arc_raise("cx.lvl",1,2,NULL)==EINVAL && !reads);
    printf("ARC ACTIVE: %u lease boundaries/%u uncertain writes, all-bank conflicts, fresh ACK, shared quarantine PASS (RAM only)\n",
        success_leases,success_writes);
}
