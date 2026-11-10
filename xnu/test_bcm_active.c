#define _DEFAULT_SOURCE 1
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#ifndef __unused
#define __unused __attribute__((unused))
#endif
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
#define APERTURE 0x4000u
static uint32_t *tcs,*db;
static uintptr_t rh_rsc=1,rh_tcs,rh_db;
static uint32_t rh_db_size=1024;
static unsigned rh_ncpt=16,rh_lock,reads,writes,checks,expire,fail_write;
static unsigned finished,kept,effects,elapsed,slot,model_stores;
static unsigned context,complete,corruption,late_bcm,triggered,current_bad,mutate_input;
static unsigned written_offsets[64],written_values[64];
static bool rh_poisoned;
static int ml_get_interrupts_enabled(void){return context!=1;}
static int ml_at_interrupt_context(void){return context==2;}
static int get_preemption_level(void){return context==3;}
static uint32_t rh_read(uintptr_t base,uint32_t off)
{
    assert(!(off&3) && off<(base==rh_db?rh_db_size:APERTURE));++reads;
    if(fail_write && writes==fail_write)return UINT32_MAX;
    return *(volatile uint32_t *)(base+off);
}
static void rh_write(uintptr_t base,uint32_t off,uint32_t value)
{
    assert(base==rh_tcs && !(off&3) && off<8*0x2a0);
    assert(writes<64);written_offsets[writes]=off;written_values[writes]=value;++writes;
    if(off>8)assert(off/0x2a0<2);
    if(off==8)tcs[1]&=~value;
    else *(volatile uint32_t *)(base+off)=value;
    if(off%0x2a0==0x14 && value==0x1010000) {
        slot=off/0x2a0;triggered=1;tcs[(slot*0x2a0+0x18)/4]=0;
    }
}
static void delay(unsigned us)
{
    assert(us==1);elapsed+=us;
    if(triggered && complete) {
        unsigned b=slot*0x2a0,enabled=tcs[(b+0x1c)/4],wait=tcs[(b+0x10)/4];
        tcs[(b+0x18)/4]=1;
        for(unsigned n=0;n<16;++n)if(enabled&(1u<<n))
            tcs[(b+0x3c+n*0x14)/4]=0x100|((wait&(1u<<n))?0x10000:0);
        if(complete!=2)tcs[1]|=1u<<slot;
        if(complete==3)tcs[(b+0x3c)/4]=0;
        if(complete==4)tcs[(b+0x3c+0x14)/4]=0x100;
        if(corruption==1)tcs[(b+0x38)/4]^=1;
        if(corruption==2)tcs[(b+0x1c)/4]^=0x40;
        if(corruption==3)tcs[(b+0x10)/4]^=1;
        if(corruption==4)tcs[(b+0x14)/4]=0;
    }
}
#include "cepheus_arc_active_private.h"
#include "cepheus_bcm_active_private.h"
static struct ceph_bcm_active_owner owner;
static struct ceph_bcm_active_vote votes[6];
static void check_aperture(void)
{
    ++checks;
    if(expire && checks>=expire) {
        assert(!mprotect(tcs,APERTURE,PROT_NONE));
        assert(!mprotect(db,APERTURE,PROT_NONE));
    }
}
static int validate(void *ctx)
{
    assert(ctx==tcs);check_aperture();
    if(expire && checks>=expire)return -1;
    if(mutate_input) {
        mutate_input=0;votes[0].target_x=99;votes[0].target_y=99;owner.finish=NULL;
    }
    if(late_bcm && writes && model_stores!=writes) {
        model_stores=writes;
        tcs[(7*0x2a0+0x1c)/4]=1;
        tcs[(7*0x2a0+0x34)/4]=0x5fff0;
    }
    return 0;
}
static int current(void *ctx,enum ceph_bcm_active_id id,const struct ceph_bcm_resource *r,uint32_t x,uint32_t y)
{
    assert(ctx==tcs && id>=0 && id<6);
    check_aperture();if(expire && checks>=expire)return -1;
    assert(r->address==0x50000+4u*(unsigned)id);
    assert((r->vcd==(const unsigned[]){2,1,2,0,0,3}[id]));
    assert(r->unit==(id==CE_BA_ACV?0u:100u) && r->width==(id==CE_BA_ACV?0u:8u));
    assert(id==CE_BA_ACV?(x==0 && y==0):(x==1 && y==10));
    return current_bad?-1:0;
}
static void finish(void *ctx,unsigned stores,int keep)
{assert(ctx==tcs && rh_lock && stores==writes);++finished;effects=stores;kept=(unsigned)keep;}
static int forbidden_arc_owner(void *ctx,const struct ceph_arc_resource *r,unsigned old,unsigned target)
{(void)ctx;(void)r;(void)old;(void)target;assert(0);return -1;}
static int forbidden_arc_access(void *ctx){(void)ctx;assert(0);return -1;}
static void reset(void)
{
    assert(!mprotect(tcs,APERTURE,PROT_READ|PROT_WRITE));
    assert(!mprotect(db,APERTURE,PROT_READ|PROT_WRITE));
    memset(tcs,0,APERTURE);memset(db,0,APERTURE);
    rh_rsc=1;rh_tcs=(uintptr_t)tcs;rh_db=(uintptr_t)db;rh_db_size=1024;
    rh_ncpt=16;rh_lock=0;rh_poisoned=false;reads=writes=checks=expire=fail_write=0;
    finished=kept=effects=elapsed=slot=model_stores=context=corruption=late_bcm=triggered=current_bad=mutate_input=0;
    complete=1;owner=(struct ceph_bcm_active_owner){tcs,validate,current,finish};
    for(unsigned n=0;n<8;++n)tcs[(n*0x2a0+0x18)/4]=1;
    tcs[0]=0xa5;tcs[1]=0x80;
    db[0]=1;db[1]=0x0c0330db;db[2]=5;db[3]=(6u<<16)|144u;
    const char *names[]={"SH2","SN15","SN3","SH0","MC0","ACV"};
    const unsigned vcd[]={2,1,2,0,0,3};
    for(unsigned n=0;n<6;++n) {
        unsigned e=144+n*24,a=288+n*8;
        memcpy((uint8_t *)db+e,names[n],strlen(names[n]));
        db[(e+16)/4]=0x50000+4*n;db[(e+20)/4]=8u|((8*n)<<16);
        db[a/4]=n==5?0:100;db[(a+4)/4]=(n==5?0:8)|(vcd[n]<<16);
        votes[n]=(struct ceph_bcm_active_vote){(enum ceph_bcm_active_id)n,n==5?0:1,n==5?0:10,n==5?0:2,n==5?8:20};
    }
}
static int run(void){return cepheus_gpu_bcm_raise(votes,6,&owner);}
int main(void)
{
    tcs=mmap(NULL,APERTURE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    db=mmap(NULL,APERTURE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(tcs!=MAP_FAILED && db!=MAP_FAILED);reset();
    uint32_t before[APERTURE/4];memcpy(before,tcs,APERTURE);
    assert(!run() && finished==1 && !kept && !rh_poisoned && !rh_lock);
    unsigned total=checks,stores=writes;
    assert(stores==29 && effects==stores && elapsed==1);
    const unsigned order[]={3,4,1,0,2,5};
    for(unsigned n=0;n<6;++n) {
        unsigned b=0x30+n*0x14,commit=!!(0x36&(1u<<n));
        assert(tcs[b/4]==(commit?0x10108u:0x108u));
        assert(tcs[(b+4)/4]==0x50000+4*order[n]);
        assert(tcs[(b+8)/4]==(order[n]==5?0x60000008u:0x20008014u|(commit<<30)));
        for(unsigned k=0;k<3;++k) {
            assert(written_offsets[1+3*n+k]==b+4*k);
            assert(written_values[1+3*n+k]==tcs[(b+4*k)/4]);
        }
    }
    assert(written_offsets[0]==0 && written_values[0]==0xa4);
    const unsigned tail_off[]={0x10,0x1c,0x14,0x14,0x14,0x14,0x1c,0x10,8,0};
    const unsigned tail_val[]={0x36,0x3f,0x10000,0x1010000,0x10000,0,0,0,1,0xa5};
    for(unsigned n=0;n<10;++n)
        assert(written_offsets[19+n]==tail_off[n] && written_values[19+n]==tail_val[n]);
    assert(tcs[0]==0xa5 && tcs[1]==0x80 && !tcs[0x10/4] && !tcs[0x14/4] && !tcs[0x1c/4]);
    for(unsigned n=0x30+6*0x14;n<APERTURE;n+=4)assert(tcs[n/4]==before[n/4]);
    for(unsigned n=1;n<=total;++n) {
        reset();expire=n;assert(run()==EBUSY && checks==n && !rh_lock);
        assert(finished==(writes!=0) && rh_poisoned==(writes!=0));
        if(finished)assert(kept && effects==writes);
        if(writes){unsigned old=reads;assert(run()==EIO && old==reads && finished==1);}
    }
    for(unsigned n=1;n<=stores;++n) {
        reset();fail_write=n;assert(run()==EIO && writes==n && finished==1 && kept && rh_poisoned);
        unsigned old=reads;assert(run()==EIO && old==reads && finished==1);
        struct ceph_arc_active_owner a={tcs,forbidden_arc_owner,forbidden_arc_access,finish};
        assert(cepheus_gpu_arc_raise("cx.lvl",1,2,&a)==EIO && reads==old);
    }
    for(unsigned n=0;n<8;++n) {
        reset();unsigned b=n*0x2a0;tcs[(b+0x1c)/4]=1u<<15;
        tcs[(b+0x34+15*0x14)/4]=0x5fff0;
        assert(run()==EBUSY && !writes && !finished && !rh_poisoned);
    }
    reset();late_bcm=1;assert(run()==EBUSY && writes==1 && finished && kept);
    reset();tcs[0x14/4]=0x10000;tcs[0x1c/4]=1;tcs[0x34/4]=0x40000;
    assert(!run() && slot==1 && tcs[0x14/4]==0x10000 && tcs[0x1c/4]==1 && tcs[0x34/4]==0x40000);
    for(unsigned n=0;n<=4;++n)if(n!=1) {
        reset();complete=n;assert(run()==ETIMEDOUT && elapsed==RH_TIMEOUT_US && finished && kept);
        assert(tcs[0x14/4]==0x1010000 && tcs[0x1c/4]==0x3f && tcs[0x10/4]==0x36 && !(tcs[0]&1));
    }
    for(unsigned n=1;n<=4;++n){reset();corruption=n;assert(run()==EBUSY && kept && rh_poisoned);}
    for(unsigned n=1;n<=3;++n){reset();context=n;assert(run()==EBUSY && !reads && !writes && !checks);}
    reset();rh_lock=1;assert(run()==EBUSY && rh_lock && !reads);
    reset();rh_poisoned=true;assert(run()==EIO && !reads && !writes && !checks);
    reset();rh_db=0;assert(run()==ENODEV && !reads && !writes && !checks);
    reset();current_bad=1;assert(run()==EBUSY && !writes && !finished);
    reset();db[288/4]=0;assert(run()==EINVAL && !writes && !finished);
    reset();db[(288+4)/4]|=16u<<16;assert(run()==EINVAL && !writes);
    reset();db[(144+16)/4]=0x50004;assert(run()==EINVAL && !writes);
    reset();rh_ncpt=5;assert(run()==EINVAL && !writes && !finished);
    for(unsigned n=0;n<6;++n) {
        reset();votes[n].target_x=0x4000;assert(run()==EINVAL && !reads && !checks);
        reset();votes[n].target_y=0x4000;assert(run()==EINVAL && !reads);
    }
    reset();votes[0].target_x=0;assert(run()==EINVAL && !reads);
    reset();votes[0].target_y=9;assert(run()==EINVAL && !reads);
    reset();votes[0].target_x=1;votes[0].target_y=10;assert(run()==EINVAL && !reads);
    reset();votes[5].target_y=7;assert(run()==EINVAL && !reads);
    reset();votes[1].id=CE_BA_SH2;assert(run()==EINVAL && !reads);
    reset();owner.finish=NULL;assert(run()==EINVAL && !reads);
    reset();assert(cepheus_gpu_bcm_raise(votes,7,&owner)==EINVAL && !reads);
    reset();assert(cepheus_gpu_bcm_raise(NULL,1,&owner)==EINVAL && !reads);
    reset();assert(cepheus_gpu_bcm_raise(votes,1,&owner)==0 && finished && !kept);
    reset();votes[0].target_x=0x3fff;votes[0].target_y=0x3fff;
    assert(!cepheus_gpu_bcm_raise(votes,1,&owner) && tcs[0x38/4]==0x6fffffff);
    reset();assert(!cepheus_gpu_bcm_raise(votes+5,1,&owner) && tcs[0x38/4]==0x60000008);
    reset();struct ceph_bcm_active_vote reordered[6];
    for(unsigned n=0;n<6;++n)reordered[n]=votes[5-n];
    assert(!cepheus_gpu_bcm_raise(reordered,6,&owner));
    const unsigned reverse_order[]={4,3,1,2,0,5};
    for(unsigned n=0;n<6;++n)assert(tcs[(0x34+n*0x14)/4]==0x50000+4*reverse_order[n]);
    reset();mutate_input=1;assert(!run() && finished && !kept && owner.finish==NULL);
    assert(tcs[(0x38+3*0x14)/4]==0x20008014);
    assert(!munmap(tcs,APERTURE) && !munmap(db,APERTURE));
    printf("BCM ACTIVE: %u protected-aperture owner boundaries/%u uncertain stores, fixed GPU routes/VCD commit/fresh ACK, nondecreasing votes/shared quarantine PASS (host model, no phone votes)\n",total,stores);
}
