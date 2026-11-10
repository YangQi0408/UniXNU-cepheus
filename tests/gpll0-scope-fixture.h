/* Synthetic idle/mappings with the actual production RPMh scope lifecycle. */
#ifndef CEPHEUS_GPLL0_SCOPE_FIXTURE_H
#define CEPHEUS_GPLL0_SCOPE_FIXTURE_H
#include <stdbool.h>
#include <errno.h>
static uintptr_t rh_rsc,rh_tcs,rh_db;
static uint32_t rh_db_size;
static unsigned rh_lock;
static bool rh_poisoned;
static unsigned scope_checks,scope_expire,scope_retains;
static int scope_idle_held,scope_idle_retained,scope_context,scope_idle_fault;
static void (*scope_revoke)(void);
int ml_get_interrupts_enabled(void){return scope_context!=1;}
int ml_at_interrupt_context(void){return scope_context==2;}
int get_preemption_level(void){return scope_context==3;}
int cepheus_gpu_idle_check(uint64_t token)
{
    ++scope_checks;
    if(scope_expire && scope_checks>=scope_expire && scope_revoke)scope_revoke();
    return token==1 && scope_idle_held && !scope_idle_fault &&
        (!scope_expire || scope_checks<scope_expire)?0:-1;
}
int cepheus_gpu_idle_finish(uint64_t token,int keep)
{
    if(cepheus_gpu_idle_check(token))return -1;
    if(keep){scope_idle_retained=1;++scope_retains;return 0;}
    if(scope_idle_retained)return -1;
    scope_idle_held=0;return 0;
}
#include "../xnu/cepheus_rpmh_scope_private.h"
static void gpll0_scope_reset(void)
{
    (void)ceph_rpmh_scope_effect;
    (void)ceph_rpmh_scope_legacy;
    rh_rsc=rh_tcs=rh_db=rh_db_size=1;rh_lock=0;rh_poisoned=false;
    scope_checks=scope_expire=scope_retains=0;
    scope_idle_held=1;scope_idle_retained=scope_context=scope_idle_fault=0;
    scope_revoke=NULL;
    ceph_rpmh_scope_phase=CE_RS_OPEN;
    ceph_rpmh_scope_effects=ceph_rpmh_scope_attempted=ceph_rpmh_scope_failed=0;
    ceph_rpmh_scope_idle=0;
    uint64_t token=0;
    assert(!cepheus_gpu_rpmh_hold(1,&token) && token==1);
    scope_checks=0;
}
#endif
