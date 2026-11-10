/* Included only by the patched SWEET osfmk/arm64/cpu.c. */
#ifndef CEPHEUS_GPU_IDLE_NATIVE_PRIVATE_H
#define CEPHEUS_GPU_IDLE_NATIVE_PRIVATE_H
#include "cepheus_gpu_idle.h"
#if HAS_RETENTION_STATE
#error "Scoped SWEET idle gate requires the non-reset WFI return path"
#endif
enum ceph_idle_phase {CE_ID_NEW,CE_ID_WAITING,CE_ID_HELD,CE_ID_RETAINED,
    CE_ID_RELEASED,CE_ID_FAILED};
static struct ceph_idle_gate ceph_gpu_idle_gate;
static unsigned ceph_gpu_idle_lock;
static enum ceph_idle_phase ceph_gpu_idle_phase;
int cepheus_gpu_idle_cpu_enter(unsigned cpu)
{
    return ceph_idle_enter(&ceph_gpu_idle_gate,cpu);
}
void cepheus_gpu_idle_cpu_leave(unsigned cpu)
{
    ceph_idle_leave(&ceph_gpu_idle_gate,cpu);
}
static int ceph_gpu_idle_context(void)
{
    return ml_get_interrupts_enabled() && !ml_at_interrupt_context() &&
        get_preemption_level()==0?0:-1;
}
static uint64_t ceph_gpu_idle_now(void)
{
    uint64_t ns;
    absolutetime_to_nanoseconds(mach_absolute_time(),&ns);
    return ns;
}
int cepheus_gpu_idle_hold(uint64_t *token)
{
    if(!token || ceph_gpu_idle_context() ||
        __atomic_exchange_n(&ceph_gpu_idle_lock,1,__ATOMIC_ACQUIRE))return -1;
    int e=-1;
    if(ceph_gpu_idle_phase!=CE_ID_NEW)goto done;
    ceph_gpu_idle_phase=CE_ID_FAILED;
    if(ceph_idle_block(&ceph_gpu_idle_gate))goto done;
    ceph_gpu_idle_phase=CE_ID_WAITING;
    uint64_t start=ceph_gpu_idle_now(),previous=start;
    /* Passive drainage avoids dereferencing a possibly retiring CPU record.
     * Already-idle cores must actually exit; a timer/IPI claim is not an ACK. */
    for(unsigned polls=0;polls<10000;polls++){
        uint64_t now=ceph_gpu_idle_now();
        if(now<previous || now<start || now-start>=10000000ULL)break;
        if(ceph_idle_drained(&ceph_gpu_idle_gate)){
            ceph_gpu_idle_phase=CE_ID_HELD;*token=1;e=0;goto done;
        }
        if(now==previous && polls)break;
        previous=now;delay(1);
    }
    ceph_gpu_idle_phase=CE_ID_FAILED;
    (void)ceph_idle_unblock(&ceph_gpu_idle_gate);
done:
    __atomic_store_n(&ceph_gpu_idle_lock,0,__ATOMIC_RELEASE);return e;
}
int cepheus_gpu_idle_check(uint64_t token)
{
    if(token!=1 || ceph_gpu_idle_context() ||
        __atomic_exchange_n(&ceph_gpu_idle_lock,1,__ATOMIC_ACQUIRE))return -1;
    int e=(ceph_gpu_idle_phase==CE_ID_HELD || ceph_gpu_idle_phase==CE_ID_RETAINED) &&
        ceph_idle_drained(&ceph_gpu_idle_gate)?0:-1;
    __atomic_store_n(&ceph_gpu_idle_lock,0,__ATOMIC_RELEASE);return e;
}
int cepheus_gpu_idle_finish(uint64_t token,int keep)
{
    if(token!=1 || (keep!=0 && keep!=1) || ceph_gpu_idle_context() ||
        __atomic_exchange_n(&ceph_gpu_idle_lock,1,__ATOMIC_ACQUIRE))return -1;
    int e=-1;
    if(ceph_gpu_idle_phase!=CE_ID_HELD && ceph_gpu_idle_phase!=CE_ID_RETAINED)goto done;
    if(keep || ceph_gpu_idle_phase==CE_ID_RETAINED ||
        !ceph_idle_drained(&ceph_gpu_idle_gate)){
        ceph_gpu_idle_phase=CE_ID_RETAINED;
        e=keep && ceph_idle_drained(&ceph_gpu_idle_gate)?0:-1;
    }else if(!ceph_idle_unblock(&ceph_gpu_idle_gate)){
        ceph_gpu_idle_phase=CE_ID_RELEASED;e=0;
    }
done:
    __atomic_store_n(&ceph_gpu_idle_lock,0,__ATOMIC_RELEASE);return e;
}
#endif
