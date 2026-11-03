/* Read-only TSENS0 TM GPU sensor12/13 sampler. Caller owns the actual mapping
 * and excludes TSENS reinitialization; no threshold/calibration/recovery writes.
 * Both valid bits are required: unlike downstream fallback, repeated invalid
 * readings are NOT accepted just because their numeric codes agree.
 */
#ifndef CEPHEUS_GPU_TSENS_H
#define CEPHEUS_GPU_TSENS_H
#include <stddef.h>
#include "cepheus_gpu_thermal.h"
struct ceph_gpu_tsens {
    volatile uint32_t *tm;size_t bytes;uint64_t pa;
    void *owner;int (*lease)(void *);int (*now_ns)(void *,uint64_t *);
};
static inline void ceph_gpu_tsens_order(void)
{
#if defined(__aarch64__) || defined(__arm64__)
    __asm__ volatile("dmb oshld" ::: "memory");
#elif !defined(KERNEL)
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
#else
#error "TSENS sampler requires arm64"
#endif
}
static inline int ceph_gpu_tsens_decode(uint32_t word,int *mc)
{
    if(!mc || word==UINT32_MAX || !(word&(1u<<21)))return -1;
    int code=(int)(word&0xfff);if(code&0x800)code-=4096;
    int temperature=code*100;if(temperature < -40000 || temperature>125000)return -1;
    *mc=temperature;return 0;
}
static inline int ceph_gpu_tsens_sample(void *ctx,struct ceph_gpu_temperature *out)
{
    if(!out)return -1;
    *out=(struct ceph_gpu_temperature){0};struct ceph_gpu_tsens *m=ctx;
    if(!m || !m->tm || ((uintptr_t)m->tm&3u) || m->pa!=0xc263000 || m->bytes!=4096 ||
        !m->lease || !m->now_ns || m->lease(m->owner))return -1;
    uint64_t start,end;if(m->now_ns(m->owner,&start))return -1;
    uint32_t ready=m->tm[0xe4/4];ceph_gpu_tsens_order();
    if(ready==UINT32_MAX || !(ready&8))return -1;
    struct ceph_gpu_temperature s={0};s.sampled_ns=start;
    for(unsigned i=0;i<2;++i) {
        int value=0,valid=0;
        for(unsigned attempt=0;attempt<3 && !valid;++attempt) {
            if(m->lease(m->owner))return -1;
            uint32_t word=m->tm[0xa0/4+12+i];ceph_gpu_tsens_order();
            valid=!ceph_gpu_tsens_decode(word,&value);
        }
        if(!valid)return -1;
        if(i)s.sensor13_mc=value;else s.sensor12_mc=value;
        s.valid_mask|=1u<<i;
    }
    ready=m->tm[0xe4/4];ceph_gpu_tsens_order();
    if(ready==UINT32_MAX || !(ready&8) || m->lease(m->owner) || m->now_ns(m->owner,&end) ||
        end<start || end-start>100000000ULL)return -1;
    *out=s;return 0;
}
#endif
