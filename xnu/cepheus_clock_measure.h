/* Fixed SM8150 GPLL0-div debug counter; no caller-selected clock or MMIO.
 * Exclusive debug owner required. Samples are physical counter observations
 * referenced to nominal XO/4, not frequency admission or PLL programming.
 */
#ifndef CEPHEUS_CLOCK_MEASURE_H
#define CEPHEUS_CLOCK_MEASURE_H
#include "cepheus_gpll0.h"

enum ceph_measure_reg {CE_ME_MUX,CE_ME_DIV,CE_ME_DEBUG,CE_ME_XO,CE_ME_CTL,CE_ME_STATUS,CE_ME_NREG};
enum ceph_measure_phase {CE_ME_NEW,CE_ME_REFUSED,CE_ME_DONE,CE_ME_POISONED};
enum {CE_ME_IO=-1,CE_ME_BUSY=-2,CE_ME_TIMEOUT=-3,CE_ME_INVALID=-4};
#define CE_ME_ENABLE (1u<<20)
#define CE_ME_DONE_BIT (1u<<25)
#define CE_ME_COUNT_MASK 0x1ffffffu

struct ceph_measure_owner {
    void *ctx;
    /* Exclude all writers to GCC debug mux/div/CBCR/XO4/counter words, hold
     * real config-access bandwidth, XO and the parent's original ownership.
     * Also exclude deep idle; callbacks must not sleep or recurse.
     */
    int (*validate)(void *);
    /* Called once after any attempted store. keep=1 on uncertainty: leave
     * clocks/counter/mapping/leases intact. keep=0 only after exact owned-field
     * restoration; does NOT release the held GPLL0 parent's resources.
     */
    void (*finish)(void *,unsigned attempted_stores,int keep);
};
struct ceph_clock_sample {
    uint32_t short_count,full_count;
    uint64_t short_hz,full_hz,short_quantum_hz,full_quantum_hz;
    uint64_t short_ns,full_ns;
    /* Both rates are measured relative to nominal 19.2MHz XO. No calibrated
     * XO tolerance or automatic rounding-to-300MHz/admission is implied.
     */
};
struct ceph_clock_measure {
    struct ceph_gpll0 *parent;
    struct ceph_measure_owner owner;
    void *timer;
    int (*now_ns)(void *,uint64_t *);
    int (*pause_us)(void *,unsigned);
    enum ceph_measure_phase phase;
    unsigned attempted_stores;
    uint64_t previous,deadline;
    uint32_t initial[CE_ME_NREG],expected[CE_ME_NREG];
};
static inline uint32_t ceph_measure_offset(enum ceph_measure_reg reg)
{
    const uint32_t offsets[]={0x62000,0x62004,0x62008,0x43008,0x62038,0x6203c};
    return reg>=0 && reg<CE_ME_NREG?offsets[reg]:UINT32_MAX;
}
static inline uint32_t ceph_measure_stable(enum ceph_measure_reg reg)
{
    /* Branch hardware status may change when enabled; do not write or restore
     * those bits. All non-status fields, including unrelated votes, stay exact.
     */
    return reg==CE_ME_DEBUG || reg==CE_ME_XO?0x0fffffffu:UINT32_MAX;
}
static inline int ceph_measure_lease(struct ceph_clock_measure *s)
{
    return !s || !s->owner.validate || !s->owner.finish || !s->now_ns || !s->pause_us ||
        s->owner.validate(s->owner.ctx) || ceph_gpll0_lease(s->parent) ||
        s->parent->phase!=CE_GP_HELD?CE_ME_IO:0;
}
static inline int ceph_measure_now(struct ceph_clock_measure *s,uint64_t *out)
{
    uint64_t n;
    if(ceph_measure_lease(s) || s->now_ns(s->timer,&n) || n<s->previous)return CE_ME_IO;
    s->previous=n;
    if(n>=s->deadline)return CE_ME_TIMEOUT;
    *out=n;return 0;
}
static inline int ceph_measure_read(struct ceph_clock_measure *s,enum ceph_measure_reg reg,uint32_t *out)
{
    uint32_t off=ceph_measure_offset(reg);
    if(!out || off==UINT32_MAX || ceph_measure_lease(s))return CE_ME_IO;
    uint32_t v=*(volatile uint32_t *)(s->parent->gcc+off);
    ceph_gpll0_barrier();
    if(v==UINT32_MAX || ceph_measure_lease(s))return CE_ME_IO;
    *out=v;return 0;
}
static inline int ceph_measure_expected(struct ceph_clock_measure *s,enum ceph_measure_reg reg)
{
    uint32_t v;
    return ceph_measure_read(s,reg,&v) ||
        ((v^s->expected[reg])&ceph_measure_stable(reg))?CE_ME_IO:0;
}
static inline int ceph_measure_write(struct ceph_clock_measure *s,enum ceph_measure_reg reg,
    uint32_t mask,uint32_t wanted)
{
    uint32_t allowed;
    switch(reg) {
    case CE_ME_MUX:allowed=0x3ffu;break;
    case CE_ME_DIV:allowed=0xfu;break;
    case CE_ME_DEBUG:case CE_ME_XO:allowed=1u;break;
    case CE_ME_CTL:allowed=CE_ME_ENABLE|0x1ffffu;break;
    default:return CE_ME_INVALID;
    }
    if(!mask || (mask&~allowed) || (wanted&~mask))return CE_ME_INVALID;
    if(reg==CE_ME_CTL && mask!=allowed)return CE_ME_INVALID;
    if(reg==CE_ME_CTL && wanted!=0 && wanted!=0x1000 && wanted!=0x10000 &&
        wanted!=(CE_ME_ENABLE|0x1000) && wanted!=(CE_ME_ENABLE|0x10000))return CE_ME_INVALID;
    uint32_t v;
    if(ceph_measure_read(s,reg,&v) || ((v^s->expected[reg])&ceph_measure_stable(reg)))return CE_ME_IO;
    /* Never echo back volatile branch hardware-status bits in the store. */
    uint32_t next=((v&ceph_measure_stable(reg))&~mask)|wanted;
    if(reg!=CE_ME_CTL && !((next^v)&mask))return 0;
    uint64_t now;int e=ceph_measure_now(s,&now);if(e)return e;
    if(ceph_measure_lease(s))return CE_ME_IO;
    s->phase=CE_ME_POISONED;++s->attempted_stores;
    ceph_gpll0_barrier();
    *(volatile uint32_t *)(s->parent->gcc+ceph_measure_offset(reg))=next;
    ceph_gpll0_barrier();
    s->expected[reg]=next;
    return ceph_measure_expected(s,reg);
}
static inline int ceph_measure_pause(struct ceph_clock_measure *s)
{
    uint64_t n;int e=ceph_measure_now(s,&n);if(e)return e;
    return s->pause_us(s->timer,1) || ceph_measure_lease(s)?CE_ME_IO:0;
}
static inline int ceph_measure_poll(struct ceph_clock_measure *s,int done,uint32_t *status)
{
    /* The downstream loops are unbounded. Both monotonic time and iteration
     * limits bound ours, even if a broken timer stops advancing.
     */
    uint64_t start;int e=ceph_measure_now(s,&start);if(e)return e;
    for(unsigned n=0;n<25000;++n) {
        uint64_t now;e=ceph_measure_now(s,&now);if(e)return e;
        if(now-start>=25000000)return CE_ME_TIMEOUT;
        uint32_t v;
        if(ceph_measure_expected(s,CE_ME_CTL) || ceph_measure_read(s,CE_ME_STATUS,&v))return CE_ME_IO;
        if(!!(v&CE_ME_DONE_BIT)==done){*status=v;return 0;}
        e=ceph_measure_pause(s);if(e)return e;
    }
    return CE_ME_TIMEOUT;
}
static inline int ceph_measure_sample(struct ceph_clock_measure *s,uint32_t ticks,
    uint32_t *count,uint64_t *elapsed)
{
    uint32_t status;
    int e=ceph_measure_write(s,CE_ME_CTL,CE_ME_ENABLE|0x1ffffu,ticks);if(e)return e;
    e=ceph_measure_poll(s,0,&status);if(e)return e; /* require fresh DONE edge */
    uint64_t start,stop;
    e=ceph_measure_now(s,&start);if(e)return e;
    e=ceph_measure_write(s,CE_ME_CTL,CE_ME_ENABLE|0x1ffffu,CE_ME_ENABLE|ticks);if(e)return e;
    e=ceph_measure_poll(s,1,&status);if(e)return e;
    e=ceph_measure_now(s,&stop);if(e)return e;
    /* A DONE edge too early cannot correspond to this XO4 sampling window.
     * Allow one reference tick for counter terminal-count convention only.
     */
    uint64_t minimum=((uint64_t)(ticks-1u)*1000000000)/4800000;
    if(stop-start<minimum)return CE_ME_IO;
    uint32_t again;
    if(ceph_measure_read(s,CE_ME_STATUS,&again) || again!=status)return CE_ME_IO;
    e=ceph_measure_write(s,CE_ME_CTL,CE_ME_ENABLE|0x1ffffu,ticks);if(e)return e;
    *count=status&CE_ME_COUNT_MASK;*elapsed=stop-start;return 0;
}
static inline uint64_t ceph_measure_rate(uint32_t count,uint32_t ticks)
{return ((UINT64_C(10)*count+15)*4800000)/(UINT64_C(10)*ticks+35);}
static inline uint64_t ceph_measure_quantum(uint32_t ticks)
{
    uint64_t denominator=UINT64_C(10)*ticks+35;
    return (UINT64_C(48000000)+denominator-1)/denominator;
}
static inline int ceph_measure_counts(struct ceph_clock_sample *r)
{
    if(!r->short_count || r->full_count<=r->short_count || r->full_count>=CE_ME_COUNT_MASK)
        return CE_ME_INVALID;
    r->short_hz=ceph_measure_rate(r->short_count,0x1000);
    r->full_hz=ceph_measure_rate(r->full_count,0x10000);
    r->short_quantum_hz=ceph_measure_quantum(0x1000);
    r->full_quantum_hz=ceph_measure_quantum(0x10000);
    uint64_t difference=r->short_hz>r->full_hz?r->short_hz-r->full_hz:r->full_hz-r->short_hz;
    /* Added consistency gate, not a downstream admission criterion: use
     * counter quantization only, never reference-frequency calibration.
     */
    if(difference>r->short_quantum_hz+r->full_quantum_hz)return CE_ME_INVALID;
    return 0;
}
static inline int ceph_clock_measure_run(struct ceph_clock_measure *s,struct ceph_clock_sample *out)
{
    if(!s || !out || s->phase!=CE_ME_NEW || s->attempted_stores)return CE_ME_INVALID;
    s->phase=CE_ME_REFUSED;
    if(ceph_measure_lease(s))return CE_ME_IO;
    uint64_t start;
    if(s->now_ns(s->timer,&start) || start>UINT64_MAX-100000000)return CE_ME_IO;
    s->previous=start;s->deadline=start+100000000;
    struct ceph_gpll0_snapshot parent;
    if(ceph_gpll0_check(s->parent,&parent))return CE_ME_IO;
    for(unsigned r=0;r<CE_ME_NREG;++r) {
        if(ceph_measure_read(s,(enum ceph_measure_reg)r,&s->initial[r]))return CE_ME_IO;
        s->expected[r]=s->initial[r];
    }
    for(unsigned r=0;r<CE_ME_STATUS;++r)
        if(ceph_measure_expected(s,(enum ceph_measure_reg)r))return CE_ME_IO;
    /* Do not steal a running counter or write undocumented control fields. */
    uint32_t control=s->initial[CE_ME_CTL];
    if(control!=0 && control!=0x1000 && control!=0x10000)return CE_ME_BUSY;
    struct ceph_clock_sample result={0};int e;
    e=ceph_measure_write(s,CE_ME_MUX,0x3ff,0x167);if(e)goto failed;
    e=ceph_measure_write(s,CE_ME_DIV,0xf,0);if(e)goto failed;
    e=ceph_measure_write(s,CE_ME_DEBUG,1,1);if(e)goto failed;
    e=ceph_measure_write(s,CE_ME_XO,1,1);if(e)goto failed;
    e=ceph_measure_sample(s,0x1000,&result.short_count,&result.short_ns);if(e)goto failed;
    e=ceph_measure_sample(s,0x10000,&result.full_count,&result.full_ns);if(e)goto failed;
    for(unsigned r=0;r<CE_ME_STATUS;++r)
        if(ceph_measure_expected(s,(enum ceph_measure_reg)r)){e=CE_ME_IO;goto failed;}
    if(ceph_gpll0_check(s->parent,&parent)){e=CE_ME_IO;goto failed;}
    /* Restore owned control fields only after clean completion and unchanged
     * owner/parent. Never attempt recovery after an uncertain MMIO result.
     */
    e=ceph_measure_write(s,CE_ME_CTL,CE_ME_ENABLE|0x1ffffu,control);if(e)goto failed;
    e=ceph_measure_write(s,CE_ME_XO,1,s->initial[CE_ME_XO]&1u);if(e)goto failed;
    e=ceph_measure_write(s,CE_ME_DEBUG,1,s->initial[CE_ME_DEBUG]&1u);if(e)goto failed;
    e=ceph_measure_write(s,CE_ME_DIV,0xf,s->initial[CE_ME_DIV]&0xfu);if(e)goto failed;
    e=ceph_measure_write(s,CE_ME_MUX,0x3ff,s->initial[CE_ME_MUX]&0x3ffu);if(e)goto failed;
    for(unsigned r=0;r<CE_ME_STATUS;++r)
        if(ceph_measure_expected(s,(enum ceph_measure_reg)r)){e=CE_ME_IO;goto failed;}
    if(ceph_gpll0_check(s->parent,&parent)){e=CE_ME_IO;goto failed;}
    uint64_t completed;e=ceph_measure_now(s,&completed);if(e)goto failed;
    e=ceph_measure_counts(&result);
    s->phase=e?CE_ME_REFUSED:CE_ME_DONE;
    s->owner.finish(s->owner.ctx,s->attempted_stores,0);
    if(!e)*out=result;
    return e;
failed:
    if(s->attempted_stores)s->owner.finish(s->owner.ctx,s->attempted_stores,1);
    return e;
}
/* One native attempt under the same serialization as GPLL0 hold/check. */
int cepheus_gpu_gpll0_measure(const struct ceph_measure_owner *,struct ceph_clock_sample *);
#endif
