/* Retain an already running SM8150 GPLL0 for the GMU divided parent.
 * Not a frequency measurement, ownership acquisition or PLL configuration.
 */
#ifndef CEPHEUS_GPLL0_H
#define CEPHEUS_GPLL0_H
#include <stddef.h>
#include <stdint.h>

#define CE_GPLL0_PA UINT64_C(0x100000)
#define CE_GPLL0_BYTES 0x90000u
#define CE_GPLL0_VOTE 0x52000u
#define CE_GPLL0_GATE 0x52004u
#define CE_GPLL0_PLL_BIT 1u
#define CE_GPLL0_DIV_BIT (1u<<16)

struct ceph_gpll0_owner {
    void *ctx;
    /* Must exclude other writers to BOTH shared vote words and PLL config;
     * hold the live CXO/rails, mapped aperture and inherited PLL throughout.
     * No idle-register-based ownership, recursive calls or sleeping callbacks.
     */
    int (*validate)(void *);
    /* Keep the resources for this boot, even on uncertain writes. Called once
     * on successful adoption or any attempted store; no release API exists.
     */
    void (*retained)(void *,unsigned attempted_stores,int ready);
};
struct ceph_gpll0_snapshot {
    uint32_t mode,l,alpha,user,status,opmode,pll_vote,gate_vote;
    uint64_t configured_div_hz; /* calculated with nominal XO, NOT measured */
};
enum ceph_gpll0_phase {CE_GP_NEW,CE_GP_REJECTED,CE_GP_HELD,CE_GP_POISONED};
struct ceph_gpll0 {
    volatile uint8_t *gcc;
    uint64_t pa;
    size_t bytes;
    struct ceph_gpll0_owner owner;
    void *timer;
    int (*delay_us)(void *,unsigned);
    enum ceph_gpll0_phase phase;
    unsigned attempted_stores;
    struct ceph_gpll0_snapshot held;
};
static inline void ceph_gpll0_barrier(void)
{
#if defined(__aarch64__) || defined(__arm64__)
    __asm__ volatile("dsb osh" ::: "memory");
#elif !defined(KERNEL)
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
#else
#error "GPLL0 MMIO requires arm64"
#endif
}
static inline int ceph_gpll0_lease(struct ceph_gpll0 *s)
{
    return !s || !s->gcc || ((uintptr_t)s->gcc&3u) || s->pa!=CE_GPLL0_PA ||
        s->bytes!=CE_GPLL0_BYTES || (uintptr_t)s->gcc>UINTPTR_MAX-s->bytes ||
        !s->owner.validate || !s->owner.retained || !s->delay_us ||
        s->owner.validate(s->owner.ctx)?-1:0;
}
static inline int ceph_gpll0_read(struct ceph_gpll0 *s,uint32_t off,uint32_t *v)
{
    if(!v || ceph_gpll0_lease(s))return -1;
    switch(off) {
    case 0:case 4:case 0xc:case 0x30:case 0x38:case 0x40:
    case CE_GPLL0_VOTE:case CE_GPLL0_GATE:break;
    default:return -1;
    }
    uint32_t value=*(volatile uint32_t *)(s->gcc+off);
    ceph_gpll0_barrier();
    if(value==UINT32_MAX || ceph_gpll0_lease(s))return -1;
    *v=value;return 0;
}
static inline int ceph_gpll0_snapshot_read(struct ceph_gpll0 *s,struct ceph_gpll0_snapshot *out)
{
    struct ceph_gpll0_snapshot v={0};
    if(ceph_gpll0_read(s,0,&v.mode) || ceph_gpll0_read(s,4,&v.l) ||
        ceph_gpll0_read(s,0x40,&v.alpha) || ceph_gpll0_read(s,0xc,&v.user) ||
        ceph_gpll0_read(s,0x30,&v.status) || ceph_gpll0_read(s,0x38,&v.opmode) ||
        ceph_gpll0_read(s,CE_GPLL0_VOTE,&v.pll_vote) ||
        ceph_gpll0_read(s,CE_GPLL0_GATE,&v.gate_vote))return -1;
    /* Restrict to already calibrated, FSM-enabled, active/locked RUN PLL.
     * No calibration, UPDATE, reset, L/ALPHA or output-divider stores.
     */
    const uint32_t live=(1u<<20)|(1u<<30)|(1u<<31)|1u;
    if((v.mode&live)!=live || (v.mode&((1u<<21)|(1u<<22)|(1u<<7))) ||
        !(v.status&(1u<<26)) || v.opmode!=1 || (v.user&7u)!=7u ||
        !v.l || v.l>104 || v.alpha>0xffffu)return -1;
    unsigned div;
    switch((v.user>>8)&15u) {
    case 0:div=1;break;case 1:div=2;break;case 3:div=4;break;case 7:div=8;break;
    default:return -1;
    }
    uint64_t pll=UINT64_C(19200000)*v.l+((UINT64_C(19200000)*v.alpha)>>16);
    if(pll<249600000 || pll>2000000000)return -1;
    v.configured_div_hz=pll/div;
    if(v.configured_div_hz!=300000000)return -1;
    *out=v;return 0;
}
static inline int ceph_gpll0_core_equal(const struct ceph_gpll0_snapshot *a,
    const struct ceph_gpll0_snapshot *b)
{
    return a->mode==b->mode && a->l==b->l && a->alpha==b->alpha &&
        a->user==b->user && a->status==b->status && a->opmode==b->opmode &&
        a->configured_div_hz==b->configured_div_hz;
}
static inline int ceph_gpll0_pause(struct ceph_gpll0 *s,unsigned us)
{
    return ceph_gpll0_lease(s) || s->delay_us(s->timer,us) || ceph_gpll0_lease(s)?-1:0;
}
static inline int ceph_gpll0_vote_set(struct ceph_gpll0 *s,uint32_t off)
{
    uint32_t bit,before;
    if(off==CE_GPLL0_VOTE)bit=CE_GPLL0_PLL_BIT;
    else if(off==CE_GPLL0_GATE)bit=CE_GPLL0_DIV_BIT;
    else return -1;
    if(ceph_gpll0_read(s,off,&before))return -1;
    if(before&bit)return 0;
    if(ceph_gpll0_lease(s))return -1;
    s->phase=CE_GP_POISONED;++s->attempted_stores;
    ceph_gpll0_barrier();
    *(volatile uint32_t *)(s->gcc+off)=before|bit;
    ceph_gpll0_barrier();
    for(unsigned us=0;us<100;++us) {
        uint32_t after;
        if(ceph_gpll0_read(s,off,&after))return -1;
        /* Exclusion is required for RMW. Do not repair a changed shared word. */
        if(after== (before|bit))return 0;
        if((after&~bit)!=(before&~bit) || ceph_gpll0_pause(s,1))return -1;
    }
    return -1;
}
static inline int ceph_gpll0_hold(struct ceph_gpll0 *s,struct ceph_gpll0_snapshot *out)
{
    if(!s || !out || s->phase!=CE_GP_NEW || s->attempted_stores)return -1;
    s->phase=CE_GP_REJECTED;
    struct ceph_gpll0_snapshot before,again,after;
    if(ceph_gpll0_snapshot_read(s,&before) || ceph_gpll0_snapshot_read(s,&again) ||
        !ceph_gpll0_core_equal(&before,&again) || before.pll_vote!=again.pll_vote ||
        before.gate_vote!=again.gate_vote)return -1;
    int e=-1;
    if(ceph_gpll0_vote_set(s,CE_GPLL0_VOTE) || ceph_gpll0_snapshot_read(s,&again) ||
        !ceph_gpll0_core_equal(&before,&again) || again.pll_vote!=(before.pll_vote|1u) ||
        again.gate_vote!=before.gate_vote ||
        ceph_gpll0_vote_set(s,CE_GPLL0_GATE) || ceph_gpll0_pause(s,500) ||
        ceph_gpll0_snapshot_read(s,&after) || ceph_gpll0_snapshot_read(s,&again) ||
        !ceph_gpll0_core_equal(&before,&after) || !ceph_gpll0_core_equal(&after,&again) ||
        after.pll_vote!=again.pll_vote || after.gate_vote!=again.gate_vote ||
        after.pll_vote!=(before.pll_vote|1u) ||
        after.gate_vote!=(before.gate_vote|CE_GPLL0_DIV_BIT))goto done;
    s->held=after;s->phase=CE_GP_HELD;*out=after;e=0;
done:
    if(s->attempted_stores || !e)s->owner.retained(s->owner.ctx,s->attempted_stores,!e);
    return e;
}
static inline int ceph_gpll0_check(struct ceph_gpll0 *s,struct ceph_gpll0_snapshot *out)
{
    if(!s || !out || s->phase!=CE_GP_HELD)return -1;
    struct ceph_gpll0_snapshot a,b;
    if(ceph_gpll0_snapshot_read(s,&a) || ceph_gpll0_snapshot_read(s,&b) ||
        !ceph_gpll0_core_equal(&s->held,&a) || !ceph_gpll0_core_equal(&a,&b) ||
        a.pll_vote!=b.pll_vote || a.gate_vote!=b.gate_vote ||
        !(a.pll_vote&1u) || !(a.gate_vote&CE_GPLL0_DIV_BIT)) {
        s->phase=CE_GP_POISONED;return -1;
    }
    *out=a;return 0;
}
/* Boot-lifetime singleton, serialized native mapping/delay; no startup caller. */
int cepheus_gpu_gpll0_hold(const struct ceph_gpll0_owner *,struct ceph_gpll0_snapshot *);
int cepheus_gpu_gpll0_check(struct ceph_gpll0_snapshot *);
#endif
