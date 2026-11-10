#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include "cepheus_gpu_idle.h"
static struct ceph_idle_gate gate;
static unsigned stop,inside;
static void *reader(void *arg)
{
    unsigned cpu=(unsigned)(uintptr_t)arg;
    while(!__atomic_load_n(&stop,__ATOMIC_SEQ_CST)){
        if(!ceph_idle_enter(&gate,cpu))continue;
        __atomic_fetch_add(&inside,1,__ATOMIC_SEQ_CST);
        __atomic_fetch_sub(&inside,1,__ATOMIC_SEQ_CST);
        ceph_idle_leave(&gate,cpu);
    }
    return NULL;
}
int main(void)
{
    assert(!ceph_idle_drained(&gate));
    for(unsigned i=0;i<CE_IDLE_CPUS;i++)assert(ceph_idle_enter(&gate,i));
    assert(!ceph_idle_block(&gate) && !ceph_idle_drained(&gate));
    for(unsigned i=0;i<CE_IDLE_CPUS;i++)assert(!ceph_idle_enter(&gate,i));
    for(unsigned i=0;i<CE_IDLE_CPUS;i++){
        ceph_idle_leave(&gate,i);
        assert(ceph_idle_drained(&gate)==(i+1==CE_IDLE_CPUS));
    }
    assert(ceph_idle_block(&gate) && !ceph_idle_unblock(&gate));
    assert(!ceph_idle_state(&gate));
    /* Timeout can unblock while an old reader is still draining. */
    assert(ceph_idle_enter(&gate,0) && !ceph_idle_block(&gate));
    assert(!ceph_idle_unblock(&gate));ceph_idle_leave(&gate,0);
    pthread_t threads[CE_IDLE_CPUS];
    for(unsigned i=0;i<CE_IDLE_CPUS;i++)
        assert(!pthread_create(&threads[i],NULL,reader,(void *)(uintptr_t)i));
    for(unsigned n=0;n<10000;n++){
        assert(!ceph_idle_block(&gate));
        while(!ceph_idle_drained(&gate))assert(!(ceph_idle_state(&gate)&CE_IDLE_FAULT));
        assert(!__atomic_load_n(&inside,__ATOMIC_SEQ_CST));
        assert(!ceph_idle_unblock(&gate));
    }
    __atomic_store_n(&stop,1,__ATOMIC_SEQ_CST);
    for(unsigned i=0;i<CE_IDLE_CPUS;i++)assert(!pthread_join(threads[i],NULL));
    assert(!ceph_idle_state(&gate));
    assert(ceph_idle_enter(&gate,0));assert(!ceph_idle_enter(&gate,0));
    ceph_idle_leave(&gate,0);
    assert(!ceph_idle_enter(&gate,1) && ceph_idle_unblock(&gate));
    struct ceph_idle_gate bad={0};ceph_idle_leave(&bad,1);
    assert(ceph_idle_state(&bad)==(CE_IDLE_BLOCKED|CE_IDLE_FAULT));
    bad=(struct ceph_idle_gate){0};assert(!ceph_idle_enter(&bad,CE_IDLE_CPUS));
    assert(!ceph_idle_drained(&bad) && ceph_idle_unblock(&bad));
    puts("GPU idle reader/block races 10000 PASS; no hardware ownership");
    return 0;
}
