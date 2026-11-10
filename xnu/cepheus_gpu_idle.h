/* Scoped XNU cpu_idle exclusion. Not an inherited-resource or firmware lease. */
#ifndef CEPHEUS_GPU_IDLE_H
#define CEPHEUS_GPU_IDLE_H
#include <stdint.h>
#define CE_IDLE_CPUS 8u
#define CE_IDLE_READERS 0xffu
#define CE_IDLE_BLOCKED (1u << 31)
#define CE_IDLE_FAULT (1u << 30)
struct ceph_idle_gate { unsigned state; };
static inline unsigned ceph_idle_state(const struct ceph_idle_gate *s)
{
    return __atomic_load_n(&s->state,__ATOMIC_SEQ_CST);
}
static inline void ceph_idle_fault(struct ceph_idle_gate *s)
{
    __atomic_fetch_or(&s->state,CE_IDLE_FAULT|CE_IDLE_BLOCKED,__ATOMIC_SEQ_CST);
}
static inline int ceph_idle_enter(struct ceph_idle_gate *s,unsigned cpu)
{
    if(cpu>=CE_IDLE_CPUS){ceph_idle_fault(s);return 0;}
    unsigned bit=1u<<cpu,old=ceph_idle_state(s);
    for(;;){
        if(old&(CE_IDLE_BLOCKED|CE_IDLE_FAULT))return 0;
        if(old&bit){ceph_idle_fault(s);return 0;}
        if(__atomic_compare_exchange_n(&s->state,&old,old|bit,0,
            __ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST))return 1;
    }
}
static inline void ceph_idle_leave(struct ceph_idle_gate *s,unsigned cpu)
{
    if(cpu>=CE_IDLE_CPUS){ceph_idle_fault(s);return;}
    unsigned bit=1u<<cpu,old=ceph_idle_state(s);
    for(;;){
        if(!(old&bit)){ceph_idle_fault(s);return;}
        if(__atomic_compare_exchange_n(&s->state,&old,old&~bit,0,
            __ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST))return;
    }
}
static inline int ceph_idle_block(struct ceph_idle_gate *s)
{
    unsigned old=__atomic_fetch_or(&s->state,CE_IDLE_BLOCKED,__ATOMIC_SEQ_CST);
    return old&(CE_IDLE_BLOCKED|CE_IDLE_FAULT)?-1:0;
}
static inline int ceph_idle_drained(const struct ceph_idle_gate *s)
{
    return ceph_idle_state(s)==CE_IDLE_BLOCKED;
}
static inline int ceph_idle_unblock(struct ceph_idle_gate *s)
{
    unsigned old=ceph_idle_state(s);
    for(;;){
        if(!(old&CE_IDLE_BLOCKED) || (old&CE_IDLE_FAULT))return -1;
        if(__atomic_compare_exchange_n(&s->state,&old,old&~CE_IDLE_BLOCKED,0,
            __ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST))return 0;
    }
}
/* One-shot, thread context, default dormant. Success drains only the patched
 * cpu_idle path; no hotplug/suspend/debugger/secure/EL2 or RSC coverage.
 * No GPU/resource MMIO, boot args or global idle_enable/wfi changes.
 * keep=1 permanently retains this hold after uncertain hardware effects.
 * A timeout releases the gate only because hold itself has no GPU effects. */
int cepheus_gpu_idle_hold(uint64_t *);
int cepheus_gpu_idle_check(uint64_t);
int cepheus_gpu_idle_finish(uint64_t,int keep);
int cepheus_gpu_idle_cpu_enter(unsigned);
void cepheus_gpu_idle_cpu_leave(unsigned);
#endif
