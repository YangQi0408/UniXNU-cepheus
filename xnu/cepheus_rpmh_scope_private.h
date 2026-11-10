/* Included after rh_lock/rh_poisoned in the actual sweet_rpmh.c. */
#ifndef CEPHEUS_RPMH_SCOPE_PRIVATE_H
#define CEPHEUS_RPMH_SCOPE_PRIVATE_H
#include "cepheus_rpmh_scope.h"
extern int get_preemption_level(void);
extern int cepheus_gpu_idle_check(uint64_t);
extern int cepheus_gpu_idle_finish(uint64_t,int);
enum ceph_rpmh_scope_phase {CE_RS_OPEN,CE_RS_HELD,CE_RS_RETAINED,CE_RS_RELEASED,CE_RS_FAILED};
static unsigned ceph_rpmh_scope_phase,ceph_rpmh_scope_effects;
static unsigned ceph_rpmh_scope_attempted;
static unsigned ceph_rpmh_scope_failed;
static uint64_t ceph_rpmh_scope_idle;
static int ceph_rpmh_scope_context(void)
{
    return ml_get_interrupts_enabled() && !ml_at_interrupt_context() &&
        !get_preemption_level()?0:EBUSY;
}
/* Called under rh_lock before any legacy writer mapping or hardware access. */
static int ceph_rpmh_scope_legacy(void)
{
    unsigned phase=__atomic_load_n(&ceph_rpmh_scope_phase,__ATOMIC_ACQUIRE);
    return phase==CE_RS_OPEN || phase==CE_RS_RELEASED?0:EBUSY;
}
int cepheus_gpu_rpmh_check(uint64_t token)
{
    if(token!=1 || ceph_rpmh_scope_context())return EBUSY;
    unsigned phase=__atomic_load_n(&ceph_rpmh_scope_phase,__ATOMIC_ACQUIRE);
    if((phase!=CE_RS_HELD && phase!=CE_RS_RETAINED) ||
        __atomic_load_n(&ceph_rpmh_scope_failed,__ATOMIC_ACQUIRE))return EBUSY;
    if(cepheus_gpu_idle_check(ceph_rpmh_scope_idle)) {
        __atomic_store_n(&ceph_rpmh_scope_failed,1,__ATOMIC_RELEASE);
        __atomic_store_n(&ceph_rpmh_scope_phase,CE_RS_FAILED,__ATOMIC_RELEASE);
        return EBUSY;
    }
    return 0;
}
int cepheus_gpu_rpmh_hold(uint64_t idle_token,uint64_t *token)
{
    if(!token || ceph_rpmh_scope_context() ||
        __atomic_exchange_n(&rh_lock,1,__ATOMIC_ACQUIRE))return EBUSY;
    int e=EBUSY;
    if(ceph_rpmh_scope_attempted)goto done;
    ceph_rpmh_scope_attempted=1;
    if(__atomic_load_n(&ceph_rpmh_scope_phase,__ATOMIC_ACQUIRE)!=CE_RS_OPEN)goto done;
    /* Admission refusal before publication leaves legacy writers unblocked. */
    if(rh_poisoned || !rh_rsc || !rh_tcs || !rh_db || !rh_db_size ||
        cepheus_gpu_idle_check(idle_token))goto done;
    ceph_rpmh_scope_idle=idle_token;
    __atomic_store_n(&ceph_rpmh_scope_phase,CE_RS_HELD,__ATOMIC_RELEASE);
    *token=1;e=0;
done:
    __atomic_store_n(&rh_lock,0,__ATOMIC_RELEASE);return e;
}
/* Under rh_lock, after the complete lease guard and immediately before store. */
static int ceph_rpmh_scope_effect(void)
{
    if(cepheus_gpu_rpmh_check(1))return EBUSY;
    if(cepheus_gpu_idle_finish(ceph_rpmh_scope_idle,1)) {
        __atomic_store_n(&ceph_rpmh_scope_failed,1,__ATOMIC_RELEASE);return EBUSY;
    }
    ceph_rpmh_scope_effects=1;
    __atomic_store_n(&ceph_rpmh_scope_phase,CE_RS_RETAINED,__ATOMIC_RELEASE);
    return 0;
}
int cepheus_gpu_rpmh_finish(uint64_t token,int keep)
{
    if(token!=1 || (keep!=0 && keep!=1) || ceph_rpmh_scope_context() ||
        __atomic_exchange_n(&rh_lock,1,__ATOMIC_ACQUIRE))return EBUSY;
    int e=EBUSY;
    unsigned phase=__atomic_load_n(&ceph_rpmh_scope_phase,__ATOMIC_ACQUIRE);
    if(phase!=CE_RS_HELD && phase!=CE_RS_RETAINED && phase!=CE_RS_FAILED)goto done;
    if(keep || ceph_rpmh_scope_effects || phase!=CE_RS_HELD || rh_poisoned ||
        __atomic_load_n(&ceph_rpmh_scope_failed,__ATOMIC_ACQUIRE) ||
        cepheus_gpu_idle_check(ceph_rpmh_scope_idle)) {
        __atomic_store_n(&ceph_rpmh_scope_phase,CE_RS_RETAINED,__ATOMIC_RELEASE);
        int retained=cepheus_gpu_idle_finish(ceph_rpmh_scope_idle,1);
        e=keep && !retained && !__atomic_load_n(&ceph_rpmh_scope_failed,__ATOMIC_ACQUIRE)?0:EBUSY;
    }else {
        __atomic_store_n(&ceph_rpmh_scope_phase,CE_RS_RELEASED,__ATOMIC_RELEASE);e=0;
    }
done:
    __atomic_store_n(&rh_lock,0,__ATOMIC_RELEASE);return e;
}
#endif
