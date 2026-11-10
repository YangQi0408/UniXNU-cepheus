#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
static uintptr_t rh_rsc=1,rh_tcs=1,rh_db=1;
static uint32_t rh_db_size=1;
static unsigned rh_lock;
static bool rh_poisoned;
static int context,idle_held,idle_retained,idle_fault;
static unsigned maps,lookups,sends;
static unsigned release_reader,reader_active,reader_done;
int ml_get_interrupts_enabled(void){return context!=1;}
int ml_at_interrupt_context(void){return context==2;}
int get_preemption_level(void){return context==3;}
int cepheus_gpu_idle_check(uint64_t token){return token==1 && idle_held && !idle_fault?0:-1;}
int cepheus_gpu_idle_finish(uint64_t token,int keep)
{
    if(cepheus_gpu_idle_check(token))return -1;
    if(keep){idle_retained=1;return 0;}
    if(idle_retained)return -1;
    idle_held=0;return 0;
}
#include "cepheus_rpmh_scope_private.h"
static int rh_map(void){++maps;return 0;}
static int rh_lookup(const char *name,uint32_t *address)
{assert(!strcmp(name,"ldoc1") || !strcmp(name,"ldoc11"));++lookups;*address=0x40000;return 0;}
static int rh_send(uint32_t address,unsigned mv,bool xob)
{assert(address==0x40000 && (xob?mv==0:mv==3200));++sends;return 0;}
/* APPLIED_LEGACY_VREG */
/* APPLIED_TOUCH_POWER */
static void reset(void)
{
    rh_rsc=rh_tcs=rh_db=1;rh_db_size=1;rh_lock=0;rh_poisoned=false;
    ceph_rpmh_scope_phase=CE_RS_OPEN;
    ceph_rpmh_scope_effects=ceph_rpmh_scope_attempted=ceph_rpmh_scope_failed=0;
    ceph_rpmh_scope_idle=0;context=idle_retained=idle_fault=0;idle_held=1;
    maps=lookups=sends=0;
}
static void *reader(void *unused)
{
    (void)unused;
    while(!__atomic_load_n(&reader_active,__ATOMIC_ACQUIRE)){}
    while(!__atomic_load_n(&release_reader,__ATOMIC_ACQUIRE))
        assert(!cepheus_gpu_rpmh_check(1));
    __atomic_store_n(&reader_done,1,__ATOMIC_RELEASE);
    return NULL;
}
int main(void)
{
    uint64_t token;
    reset();assert(!cepheus_touch_power_enable() && maps==1 && lookups==1 && sends==1);
    reset();assert(!sweet_rpmh_vreg_enable("ldoc11",3200) && sends==1);
    for(int mode=1;mode<=3;++mode){
        reset();context=mode;token=99;
        assert(cepheus_gpu_rpmh_hold(1,&token)==EBUSY && token==99 && !rh_lock);
    }
    reset();rh_lock=1;token=99;assert(cepheus_gpu_rpmh_hold(1,&token)==EBUSY && token==99 && rh_lock);
    for(unsigned missing=0;missing<5;++missing){
        reset();token=99;
        if(missing==0)rh_rsc=0;
        if(missing==1)rh_tcs=0;
        if(missing==2)rh_db=0;
        if(missing==3)rh_db_size=0;
        if(missing==4)rh_poisoned=true;
        assert(cepheus_gpu_rpmh_hold(1,&token)==EBUSY && token==99 && !maps && !sends);
        assert(!ceph_rpmh_scope_legacy());
        rh_rsc=rh_tcs=rh_db=rh_db_size=1;rh_poisoned=false;
        assert(cepheus_gpu_rpmh_hold(1,&token)==EBUSY && token==99);
    }
    reset();token=99;assert(cepheus_gpu_rpmh_hold(2,&token)==EBUSY && token==99);
    reset();token=99;assert(!cepheus_gpu_rpmh_hold(1,&token) && token==1 && !rh_lock);
    assert(cepheus_touch_power_enable()==EBUSY && sweet_rpmh_vreg_enable("ldoc11",3200)==EBUSY);
    assert(!maps && !lookups && !sends && !rh_lock);
    assert(cepheus_gpu_rpmh_check(0)==EBUSY && !cepheus_gpu_rpmh_check(1));
    assert(cepheus_gpu_rpmh_finish(1,2)==EBUSY && !cepheus_gpu_rpmh_check(1));
    assert(!cepheus_gpu_rpmh_finish(1,0) && !ceph_rpmh_scope_legacy() && idle_held);
    assert(!cepheus_touch_power_enable() && sends==1);
    assert(cepheus_gpu_rpmh_hold(1,&token)==EBUSY);
    reset();assert(!cepheus_gpu_rpmh_hold(1,&token));
    assert(!ceph_rpmh_scope_effect() && idle_retained);
    assert(cepheus_gpu_idle_finish(1,0)!=0 && idle_held);
    assert(cepheus_gpu_rpmh_finish(1,0)==EBUSY && ceph_rpmh_scope_legacy()==EBUSY);
    assert(!cepheus_gpu_rpmh_finish(1,1) && !cepheus_gpu_rpmh_check(1));
    reset();assert(!cepheus_gpu_rpmh_hold(1,&token));idle_fault=1;
    assert(cepheus_gpu_rpmh_check(1)==EBUSY);idle_fault=0;
    assert(cepheus_gpu_rpmh_finish(1,1)==EBUSY && cepheus_gpu_rpmh_check(1)==EBUSY);
    assert(cepheus_touch_power_enable()==EBUSY && !maps && !sends);
    /* Repeated readers run against the actual release-published held state. */
    for(unsigned trial=0;trial<1000;++trial){
        reset();assert(!cepheus_gpu_rpmh_hold(1,&token));
        pthread_t thread;release_reader=reader_done=reader_active=0;
        assert(!pthread_create(&thread,NULL,reader,NULL));
        __atomic_store_n(&reader_active,1,__ATOMIC_RELEASE);
        assert(cepheus_touch_power_enable()==EBUSY && !maps && !sends);
        __atomic_store_n(&release_reader,1,__ATOMIC_RELEASE);
        while(!__atomic_load_n(&reader_done,__ATOMIC_ACQUIRE)){}
        assert(!pthread_join(thread,NULL));
    }
    puts("Actual transformed touch/vreg writers refuse held/retained scopes with zero hardware callbacks; one-shot/context/fault/1000 readers PASS");
}
