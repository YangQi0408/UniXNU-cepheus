/* SM8150 power transaction register adapter. The platform supplies mapped
 * windows and a live resource lease; this layer never invents ownership,
 * changes ARC votes, releases reset, or accesses GX registers.
 */
#ifndef CEPHEUS_GPU_POWER_MMIO_H
#define CEPHEUS_GPU_POWER_MMIO_H
#include <stddef.h>
#include "cepheus_gpu_power_txn.h"

struct ceph_power_window {
    volatile uint8_t *base;
    uint64_t pa;
    size_t bytes;
};
struct ceph_power_mmio {
    struct ceph_power_window gcc,cc,gmu;
    void *owner;
    int (*lease)(void *); /* cold ownership, parents, shared rails and lifetime */
};
static inline void ceph_power_mmio_barrier(void)
{
#if defined(__aarch64__) || defined(__arm64__)
    __asm__ volatile("dsb osh" ::: "memory");
#elif !defined(KERNEL)
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
#else
#error "Power MMIO requires arm64"
#endif
}
static inline int ceph_power_window_valid(const struct ceph_power_window *w,uint64_t pa,size_t bytes)
{
    return w && w->base && !((uintptr_t)w->base&3u) && w->pa==pa && w->bytes==bytes &&
        (uintptr_t)w->base<=UINTPTR_MAX-bytes;
}
static inline int ceph_power_mmio_valid(struct ceph_power_mmio *m)
{
    return !m || !ceph_power_window_valid(&m->gcc,0x100000,0x90000) ||
        !ceph_power_window_valid(&m->cc,0x2c90000,0x9000) ||
        !ceph_power_window_valid(&m->gmu,0x2c6a000,0x30000) ||
        !m->lease || m->lease(m->owner)?-1:0;
}
static inline uint32_t ceph_power_mmio_load(const struct ceph_power_window *w,uint32_t offset)
{
    uint32_t value=*(volatile uint32_t *)(w->base+offset);
    ceph_power_mmio_barrier();return value;
}
/* Named registers only; offsets are bytes within three fixed apertures. */
static inline int ceph_power_mmio_register(struct ceph_power_mmio *m,enum ceph_power_reg reg,
    const struct ceph_power_window **window,uint32_t *offset)
{
    *window=&m->cc;
    switch(reg) {
    case CE_PWR_CX:*offset=CE_GPU_CX_GDSCR;break;
    case CE_PWR_CX_STATUS:*offset=CE_GPU_CX_HW_CTRL;break;
    case CE_PWR_GX:*offset=CE_GPU_GX_GDSCR;break;
    case CE_PWR_RCG:*offset=CE_GPU_GMU_CMD;break;
    case CE_PWR_RCG_CFG:*offset=CE_GPU_GMU_CFG;break;
    case CE_PWR_GMU:*offset=CE_GPU_GMU_CBCR;break;
    case CE_PWR_CXO:*offset=CE_GPU_CXO_CBCR;break;
    case CE_PWR_AHB:*offset=CE_GPU_AHB_CBCR;break;
    case CE_PWR_CFG_AHB:*window=&m->gcc;*offset=CE_GPU_GCC_CFG;break;
    case CE_PWR_DDR:*window=&m->gcc;*offset=CE_GPU_GCC_DDR;break;
    case CE_PWR_MEMNOC:*window=&m->gcc;*offset=CE_GPU_GCC_MEMNOC;break;
    case CE_PWR_DVM:*window=&m->gcc;*offset=CE_GPU_GCC_DVM;break;
    case CE_PWR_CM3_RESET:*window=&m->gmu;*offset=0x1f800u*4-0x6a000u;break;
    default:return -1;
    }
    return 0;
}
static inline int ceph_power_mmio_gmu_ready(struct ceph_power_mmio *m)
{
    /* Only GCC/GPUCC are touched until all access clocks and CX are live. */
    if(!ceph_gpu_branch_active(ceph_power_mmio_load(&m->gcc,CE_GPU_GCC_CFG)))return -1;
    struct ceph_gpu_power_snapshot s={0};
    s.cfg=ceph_power_mmio_load(&m->gcc,CE_GPU_GCC_CFG);
    s.cx_gdscr=ceph_power_mmio_load(&m->cc,CE_GPU_CX_GDSCR);
    s.cx_hw=ceph_power_mmio_load(&m->cc,CE_GPU_CX_HW_CTRL);
    s.gx_gdscr=ceph_power_mmio_load(&m->cc,CE_GPU_GX_GDSCR);
    s.ahb=ceph_power_mmio_load(&m->cc,CE_GPU_AHB_CBCR);
    s.gmu=ceph_power_mmio_load(&m->cc,CE_GPU_GMU_CBCR);
    s.cxo=ceph_power_mmio_load(&m->cc,CE_GPU_CXO_CBCR);
    s.rcg=ceph_power_mmio_load(&m->cc,CE_GPU_GMU_CMD);
    return ceph_gpu_gmu_blockers(&s)?-1:0;
}
static inline int ceph_power_mmio_read(void *ctx,enum ceph_power_reg reg,uint32_t *value)
{
    struct ceph_power_mmio *m=ctx;
    const struct ceph_power_window *window;uint32_t offset;
    if(!value || ceph_power_mmio_valid(m) || ceph_power_mmio_register(m,reg,&window,&offset))return -1;
    if(window!=&m->gcc && !ceph_gpu_branch_active(ceph_power_mmio_load(&m->gcc,CE_GPU_GCC_CFG)))return -1;
    if(window==&m->gmu && ceph_power_mmio_gmu_ready(m))return -1;
    if(m->lease(m->owner))return -1;
    uint32_t v=ceph_power_mmio_load(window,offset);
    if(v==UINT32_MAX)return -1;
    *value=v;return 0;
}
static inline int ceph_power_mmio_write(void *ctx,enum ceph_power_reg reg,uint32_t value)
{
    struct ceph_power_mmio *m=ctx;
    const struct ceph_power_window *window;uint32_t offset,before,mask;
    switch(reg) {
    case CE_PWR_CX:mask=0x00fff001u;break;
    case CE_PWR_RCG:mask=3;break;
    case CE_PWR_RCG_CFG:mask=0x0010371fu;break;
    case CE_PWR_GMU:case CE_PWR_CXO:case CE_PWR_AHB:
    case CE_PWR_DDR:case CE_PWR_MEMNOC:case CE_PWR_DVM:mask=1;break;
    default:return -1; /* no status, reset, GX, PLL or CFG_AHB stores */
    }
    if(ceph_power_mmio_read(m,reg,&before) || ((before^value)&~mask))return -1;
    if(reg==CE_PWR_CX) {
        uint32_t cfg=value&0x00fff000u;
        if(cfg!=0x00288000u || ((value&1u) && !(before&1u)))return -1;
    } else {
        uint32_t cx;
        if(ceph_power_mmio_read(m,CE_PWR_CX_STATUS,&cx) || !(cx&(1u<<31)))return -1;
        if(reg==CE_PWR_RCG_CFG) {
            uint32_t cmd;
            if((value&mask)!=0x602 || ceph_power_mmio_read(m,CE_PWR_RCG,&cmd) ||
                !(cmd&2u) || (cmd&((1u<<31)|1u)))return -1;
        } else if(reg==CE_PWR_RCG) {
            if((before&1u) || ((value&1u) && !(value&2u)))return -1;
        } else if(!(value&1u))return -1; /* branch votes may only be enabled */
    }
    if(ceph_power_mmio_register(m,reg,&window,&offset) || m->lease(m->owner))return -1;
    ceph_power_mmio_barrier();
    *(volatile uint32_t *)(window->base+offset)=value;
    ceph_power_mmio_barrier();return 0;
}
#endif
