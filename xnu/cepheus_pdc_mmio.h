/* Ordered A640v2 PDC/RSC MMIO adapter. Holds no runtime mapping/ownership
 * discovery. The actual platform must lease all three windows and freeze
 * firmware/GX/admission; every operation rechecks it. No general PDC writes.
 */
#ifndef CEPHEUS_PDC_MMIO_H
#define CEPHEUS_PDC_MMIO_H
#include "cepheus_gpu_pdc.h"
#include "cepheus_gmu_mmio.h"
struct ceph_pdc_mmio {
    struct ceph_gmu_mmio *gmu;
    volatile uint32_t *cfg,*seq;
    uint64_t cfg_pa,seq_pa;
    size_t cfg_bytes,seq_bytes;
    void *owner;
    int (*lease)(void *);
    void (*finish)(void *,int keep);
    struct ceph_pdc_plan plan;
    unsigned cursor,active,terminal,failed;
};
static inline int ceph_pdc_mmio_ready(struct ceph_pdc_mmio *m)
{
    if(!m || m->failed || !m->cfg || !m->seq || (((uintptr_t)m->cfg|(uintptr_t)m->seq)&3u) ||
        m->cfg_pa!=0xb290000 || m->seq_pa!=0xb490000 || m->cfg_bytes!=0x10000 || m->seq_bytes!=0x10000 ||
        (uintptr_t)m->cfg>UINTPTR_MAX-m->cfg_bytes || (uintptr_t)m->seq>UINTPTR_MAX-m->seq_bytes ||
        !m->lease || !m->finish)return -1;
    if(m->lease(m->owner) || ceph_gmu_mmio_ready(m->gmu))goto failed;
    uint32_t reset,gx;
    if(ceph_gmu_mmio_load(m->gmu,0x1f800,&reset) || reset!=1 ||
        ceph_gmu_mmio_load(m->gmu,0x1f8d0,&gx) || (gx&0xc0)!=0xc0)goto failed;
    if(m->lease(m->owner))goto failed;
    return 0;
failed:
    m->failed=1;return -1;
}
static inline volatile uint32_t *ceph_pdc_mmio_window(struct ceph_pdc_mmio *m,enum ceph_pdc_window w)
{return w==CE_PDC_RSC?(volatile uint32_t *)m->gmu->base:w==CE_PDC_CFG?m->cfg:w==CE_PDC_SEQ?m->seq:NULL;}
static inline int ceph_pdc_mmio_load(struct ceph_pdc_mmio *m,enum ceph_pdc_window w,
    uint32_t off,uint32_t *out)
{
    if(!out || (off&3u) || ceph_pdc_mmio_ready(m))return -1;
    volatile uint32_t *base=ceph_pdc_mmio_window(m,w);
    if(!base || off>(w==CE_PDC_RSC?0x30000u:0x10000u)-4u)return -1;
    uint32_t value=base[off/4];ceph_gmu_mmio_barrier();
    if(ceph_pdc_mmio_ready(m) || value==UINT32_MAX){m->failed=1;return -1;}
    *out=value;return 0;
}
static inline int ceph_pdc_mmio_begin(void *ctx,const struct ceph_pdc_input *in)
{
    struct ceph_pdc_mmio *m=ctx;struct ceph_pdc_plan plan;
    if(!m || m->active || m->terminal || m->cursor || ceph_pdc_plan_a640v2(in,&plan) ||
        ceph_pdc_mmio_ready(m))return -1;
    /* A populated live PDC is NOT converted to cold merely because the caller
     * gave us empty structs. Never disable or overwrite inherited enable bits. */
    const uint32_t cold[]={0x1140*4,0x1573*4,0x15d7*4};
    for(unsigned i=0;i<3;++i) {
        uint32_t value;if(ceph_pdc_mmio_load(m,CE_PDC_CFG,cold[i],&value) || value)return -1;
    }
    m->plan=plan;m->active=1;return 0;
}
static inline int ceph_pdc_mmio_validate(void *ctx)
{
    struct ceph_pdc_mmio *m=ctx;
    return !m || !m->active || m->terminal || m->plan.count!=48 || ceph_pdc_mmio_ready(m)?-1:0;
}
static inline int ceph_pdc_mmio_read(void *ctx,enum ceph_pdc_window w,uint32_t off,uint32_t *v)
{
    struct ceph_pdc_mmio *m=ctx;
    if(!v || (off&3) || ceph_pdc_mmio_validate(m))return -1;
    unsigned allowed=0;
    for(unsigned i=0;i<m->plan.count;++i)
        if(m->plan.writes[i].window==w && m->plan.writes[i].offset==off)allowed=1;
    return allowed?ceph_pdc_mmio_load(m,w,off,v):-1;
}
static inline int ceph_pdc_mmio_write(void *ctx,enum ceph_pdc_window w,uint32_t off,uint32_t value)
{
    struct ceph_pdc_mmio *m=ctx;
    if(ceph_pdc_mmio_validate(m) || m->cursor>=m->plan.count)return -1;
    const struct ceph_pdc_write *step=&m->plan.writes[m->cursor];
    if(step->window!=w || step->offset!=off || step->value!=value)return -1;
    volatile uint32_t *base=ceph_pdc_mmio_window(m,w);if(!base)return -1;
    ++m->cursor;ceph_gmu_mmio_barrier();base[off/4]=value;ceph_gmu_mmio_barrier();
    return ceph_pdc_mmio_ready(m);
}
static inline int ceph_pdc_mmio_barrier(void *ctx)
{if(ceph_pdc_mmio_validate(ctx))return -1;ceph_gmu_mmio_barrier();return ceph_pdc_mmio_validate(ctx);}
static inline void ceph_pdc_mmio_end(void *ctx,int keep)
{
    struct ceph_pdc_mmio *m=ctx;if(!m || !m->active || m->terminal)return;
    m->active=0;m->terminal=1;m->finish(m->owner,keep || m->cursor!=0);
}
static inline int ceph_pdc_mmio_ops(struct ceph_pdc_mmio *m,struct ceph_pdc_ops *out)
{
    if(!m || !out || m->active || m->terminal || m->cursor || ceph_pdc_mmio_ready(m))return -1;
    *out=(struct ceph_pdc_ops){m,ceph_pdc_mmio_begin,ceph_pdc_mmio_validate,ceph_pdc_mmio_read,
        ceph_pdc_mmio_write,ceph_pdc_mmio_barrier,ceph_pdc_mmio_end};return 0;
}
#endif
