#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cepheus_gpu_idle.h"
static uint64_t clock_ns;
static int bad_context,time_mode,drain;
static unsigned pauses;
static int ml_get_interrupts_enabled(void){return !bad_context;}
static int ml_at_interrupt_context(void){return bad_context==2;}
static int get_preemption_level(void){return bad_context==3;}
static uint64_t mach_absolute_time(void)
{
    if(time_mode==1)return clock_ns;
    if(time_mode==2)return clock_ns-=100;
    return clock_ns+=100;
}
static void absolutetime_to_nanoseconds(uint64_t t,uint64_t *ns){*ns=t;}
static void delay(unsigned);
#include "cepheus_gpu_idle_native_private.h"
static void delay(unsigned us)
{
    assert(us==1);pauses++;
    if(time_mode!=1)clock_ns+=1000;
    if(drain && pauses==3)ceph_idle_leave(&ceph_gpu_idle_gate,7);
}
static void reset(void)
{
    memset(&ceph_gpu_idle_gate,0,sizeof(ceph_gpu_idle_gate));
    ceph_gpu_idle_lock=0;ceph_gpu_idle_phase=CE_ID_NEW;
    clock_ns=1000000;bad_context=time_mode=drain=0;pauses=0;
}
int main(void)
{
    uint64_t token=123;
    for(int i=1;i<=3;i++){
        reset();bad_context=i;assert(cepheus_gpu_idle_hold(&token));
        assert(token==123 && ceph_gpu_idle_phase==CE_ID_NEW);
    }
    reset();assert(cepheus_gpu_idle_hold(NULL));
    ceph_gpu_idle_lock=1;assert(cepheus_gpu_idle_hold(&token));
    ceph_gpu_idle_lock=0;assert(!cepheus_gpu_idle_hold(&token) && token==1);
    assert(!cepheus_gpu_idle_check(token));assert(cepheus_gpu_idle_check(2));
    assert(cepheus_gpu_idle_finish(token,2));
    assert(!ceph_idle_enter(&ceph_gpu_idle_gate,0));
    assert(!cepheus_gpu_idle_finish(token,0));
    assert(cepheus_gpu_idle_check(token) && cepheus_gpu_idle_hold(&token));
    assert(ceph_idle_enter(&ceph_gpu_idle_gate,0));ceph_idle_leave(&ceph_gpu_idle_gate,0);
    reset();assert(!cepheus_gpu_idle_hold(&token));
    assert(!cepheus_gpu_idle_finish(token,1));
    assert(cepheus_gpu_idle_finish(token,0));assert(!cepheus_gpu_idle_check(token));
    assert(!ceph_idle_enter(&ceph_gpu_idle_gate,0));
    reset();assert(ceph_idle_enter(&ceph_gpu_idle_gate,7));drain=1;
    assert(!cepheus_gpu_idle_hold(&token) && pauses==3);
    assert(!cepheus_gpu_idle_finish(token,0));
    reset();token=123;assert(ceph_idle_enter(&ceph_gpu_idle_gate,7));
    assert(cepheus_gpu_idle_hold(&token));
    assert(token==123 && pauses>0 && ceph_gpu_idle_phase==CE_ID_FAILED);
    assert(ceph_idle_state(&ceph_gpu_idle_gate)==128);ceph_idle_leave(&ceph_gpu_idle_gate,7);
    assert(cepheus_gpu_idle_hold(&token));
    for(int i=1;i<=2;i++){
        reset();token=123;time_mode=i;assert(ceph_idle_enter(&ceph_gpu_idle_gate,7));
        assert(cepheus_gpu_idle_hold(&token));
        assert(token==123 && !(ceph_idle_state(&ceph_gpu_idle_gate)&CE_IDLE_BLOCKED));
        ceph_idle_leave(&ceph_gpu_idle_gate,7);
    }
    reset();assert(!cepheus_gpu_idle_hold(&token));
    ceph_idle_fault(&ceph_gpu_idle_gate);assert(cepheus_gpu_idle_check(token));
    assert(cepheus_gpu_idle_finish(token,0));
    assert(ceph_gpu_idle_phase==CE_ID_RETAINED && !ceph_idle_enter(&ceph_gpu_idle_gate,0));
    puts("Native idle one-shot/drain/context/timeout/retention PASS; emulated time");
    return 0;
}
