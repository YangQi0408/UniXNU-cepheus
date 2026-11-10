/* Read-only gpuss-0/1 sampler: saved FDT thermal zones resolve to TSENS0 sensor15
 * and TSENS1 sensor11 (NOT legacy tsens_tz_sensor12 or TSENS0 sensors12/13).
 * Caller owns both actual mappings
 * and excludes TSENS reinitialization; no threshold/calibration/recovery writes.
 * Both valid bits are required: unlike downstream fallback, repeated invalid
 * readings are NOT accepted just because their numeric codes agree.
 * sampled_ns timestamps CPU observation, not a hardware conversion counter.
 * lease must guarantee ongoing conversion/clock health; valid alone can't.
 */
#ifndef CEPHEUS_GPU_TSENS_H
#define CEPHEUS_GPU_TSENS_H
#include <stddef.h>
#include "cepheus_gpu_thermal.h"
struct ceph_gpu_tsens {
    volatile uint32_t *tm[2];size_t bytes[2];uint64_t pa[2];
    void *owner;int (*lease)(void *);int (*now_ns)(void *,uint64_t *);
    volatile uint32_t *srot[2];size_t srot_bytes[2];uint64_t srot_pa[2];
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
static inline int ceph_gpu_tsens_valid(struct ceph_gpu_tsens *m)
{
    if(!m || !m->lease || !m->now_ns)return -1;
    uintptr_t bases[4];
    for(unsigned i=0;i<2;++i) {
        bases[i]=(uintptr_t)m->tm[i];bases[i+2]=(uintptr_t)m->srot[i];
        if(!bases[i] || !bases[i+2] || ((bases[i]|bases[i+2])&3u) ||
            bases[i]>UINTPTR_MAX-4096u || bases[i+2]>UINTPTR_MAX-4096u ||
            m->pa[i]!=(i?0xc265000u:0xc263000u) || m->bytes[i]!=4096 ||
            m->srot_pa[i]!=(i?0xc223000u:0xc222000u) || m->srot_bytes[i]!=4096)return -1;
    }
    for(unsigned i=0;i<4;++i)for(unsigned j=i+1;j<4;++j)
        if(bases[i]<bases[j]+4096u && bases[j]<bases[i]+4096u)return -1;
    return m->lease(m->owner)?-1:0;
}
static inline int ceph_gpu_tsens_load(struct ceph_gpu_tsens *m,unsigned controller,
    unsigned srot,uint32_t offset,uint32_t *out)
{
    if(!out || controller>1 || srot>1 || (offset&3u) || offset>4092u ||
        ceph_gpu_tsens_valid(m))return -1;
    volatile uint32_t *base=srot?m->srot[controller]:m->tm[controller];
    uint32_t value=base[offset/4];ceph_gpu_tsens_order();
    if(ceph_gpu_tsens_valid(m) || value==UINT32_MAX)return -1;
    *out=value;return 0;
}
static inline int ceph_gpu_tsens_health(struct ceph_gpu_tsens *m,unsigned controller)
{
    unsigned sensor=controller?11:15;uint32_t enabled,ready,faults;
    /* SROT+4 requires ownership of its containing page, not the four-byte
     * nominal FDT resource. Every status read has independent lease guards. */
    if(controller>1 || ceph_gpu_tsens_load(m,controller,1,4,&enabled) ||
        !(enabled&1u) || !(enabled&(1u<<(sensor+3))) ||
        ceph_gpu_tsens_load(m,controller,0,0xe4,&ready) || !(ready&8u) ||
        ceph_gpu_tsens_load(m,controller,0,0x14,&faults) ||
        (faults&((1u<<31)|(1u<<sensor))))return -1;
    return 0;
}
static inline int ceph_gpu_tsens_now(struct ceph_gpu_tsens *m,uint64_t *out)
{
    if(!out || ceph_gpu_tsens_valid(m))return -1;
    uint64_t value;int e=m->now_ns(m->owner,&value);
    if(ceph_gpu_tsens_valid(m) || e)return -1;
    *out=value;return 0;
}
static inline int ceph_gpu_tsens_sample(void *ctx,struct ceph_gpu_temperature *out)
{
    if(!out)return -1;
    *out=(struct ceph_gpu_temperature){0};struct ceph_gpu_tsens *m=ctx;
    if(ceph_gpu_tsens_valid(m))return -1;
    uint64_t start,end;if(ceph_gpu_tsens_now(m,&start))return -1;
    struct ceph_gpu_temperature s={0};s.sampled_ns=start;
    for(unsigned i=0;i<2;++i) {
        if(ceph_gpu_tsens_health(m,i))return -1;
        int value=0,valid=0;
        for(unsigned attempt=0;attempt<3 && !valid;++attempt) {
            uint32_t word;
            if(ceph_gpu_tsens_load(m,i,0,0xa0+4*(i?11:15),&word))return -1;
            valid=!ceph_gpu_tsens_decode(word,&value);
        }
        if(!valid)return -1;
        if(i)s.gpuss1_mc=value;else s.gpuss0_mc=value;
        s.valid_mask|=1u<<i;
    }
    for(unsigned i=0;i<2;++i)if(ceph_gpu_tsens_health(m,i))return -1;
    if(ceph_gpu_tsens_now(m,&end) || end<=start || end-start>100000000ULL)return -1;
    *out=s;return 0;
}
#endif
