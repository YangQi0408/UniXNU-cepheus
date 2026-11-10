#define _DEFAULT_SOURCE 1
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "cepheus_clock_measure.h"

static uint32_t *regs;
static unsigned checks,expire,finished,kept,effects,model_stores,counters,context_bad;
static unsigned inject_store;
static int fault,applied,timer_fault;
static unsigned early_ticks;
static uint64_t now,due,now_calls,time_expire;
static uint32_t counter_value;
static int ceph_scm_info_context(void *p){(void)p;return (int)context_bad;}
static uintptr_t ml_io_map(uint64_t pa,size_t bytes)
{assert(pa==CE_GPLL0_PA && bytes==CE_GPLL0_BYTES);return (uintptr_t)regs;}
static void delay(unsigned us){assert(us && us<=500);if(timer_fault!=1)now+=(uint64_t)us*1000;}
static uint64_t mach_absolute_time(void)
{
    ++now_calls;
    if(timer_fault==2 && now_calls>=time_expire)now=0;
    else if(timer_fault!=1)now+=1000000;
    return now;
}
static void absolutetime_to_nanoseconds(uint64_t value,uint64_t *out){*out=value;}
#include "cepheus_gpll0_private.h"
#include "cepheus_clock_measure_private.h"

static uint32_t *reg(enum ceph_measure_reg r){return regs+ceph_measure_offset(r)/4;}
static void hardware(void)
{
    struct ceph_clock_measure *s=&ceph_measure_native;
    if(s->attempted_stores!=model_stores) {
        model_stores=s->attempted_stores;
        /* Only emulate the actual counter control store, not a hidden engine. */
        uint32_t control=*reg(CE_ME_CTL);
        if(control&CE_ME_ENABLE) {
            if(!due) {
                ++counters;
                uint32_t ticks=control&0x1ffffu;
                uint64_t numerator=UINT64_C(300000000)*(UINT64_C(10)*ticks+35);
                counter_value=(uint32_t)((numerator/4800000-15+5)/10);
                due=now+(UINT64_C(1000000000)*ticks+4799999)/4800000;
                if(early_ticks)due=now+early_ticks;
            }
        } else {
            due=0;
            if(fault!=1)*reg(CE_ME_STATUS)&=~CE_ME_DONE_BIT;
        }
        if(inject_store==model_stores && !applied) {
            applied=1;
            if(fault==5)*reg(CE_ME_MUX)^=1u<<12;
            if(fault==6)*reg(CE_ME_CTL)^=1u<<20;
            if(fault==7)regs[0]^=1u<<31;
            if(fault==8)*reg(CE_ME_MUX)^=1;
        }
    }
    if(due && (now>=due || fault==10) && fault!=2) {
        uint32_t count=counter_value;
        if(fault==3)count=1234;
        if(fault==4 && counters==2)count+=10000;
        if(fault==9)count=CE_ME_COUNT_MASK;
        *reg(CE_ME_STATUS)=CE_ME_DONE_BIT|count;
    }
}
static int parent_owner(void *p)
{assert(p==regs);return 0;}
static void parent_retained(void *p,unsigned n,int ready)
{assert(p==regs && n==0 && ready);}
static int measure_owner(void *p)
{
    assert(p==regs);++checks;
    if(expire && checks>=expire){assert(!mprotect(regs,CE_GPLL0_BYTES,PROT_NONE));return -1;}
    hardware();return 0;
}
static void finish(void *p,unsigned n,int keep)
{assert(p==regs);++finished;effects=n;kept=(unsigned)keep;}
static void reset(void)
{
    assert(!mprotect(regs,CE_GPLL0_BYTES,PROT_READ|PROT_WRITE));
    memset(regs,0,CE_GPLL0_BYTES);
    ceph_gpll0_native=(struct ceph_gpll0){0};ceph_measure_native=(struct ceph_clock_measure){0};
    ceph_gpll0_lock=0;
    checks=expire=finished=kept=effects=model_stores=counters=context_bad=inject_store=0;
    fault=applied=timer_fault=0;early_ticks=0;now=1000000;due=now_calls=time_expire=0;
    regs[0]=(1u<<20)|(1u<<30)|(1u<<31)|7;
    regs[1]=31;regs[0x40/4]=0x4000;regs[0xc/4]=0x107;
    regs[0x30/4]=1u<<26;regs[0x38/4]=1;
    regs[0x52000/4]=0x401;regs[0x52004/4]=0x18080;
    struct ceph_gpll0_owner o={regs,parent_owner,parent_retained};struct ceph_gpll0_snapshot out;
    assert(!cepheus_gpu_gpll0_hold(&o,&out));
    *reg(CE_ME_MUX)=0x1045;*reg(CE_ME_DIV)=0x103;
    *reg(CE_ME_DEBUG)=0x80000020;*reg(CE_ME_XO)=0x80000040;
    *reg(CE_ME_CTL)=0x10000;*reg(CE_ME_STATUS)=CE_ME_DONE_BIT|99;
}
static int run(struct ceph_clock_sample *out)
{struct ceph_measure_owner o={regs,measure_owner,finish};return cepheus_gpu_gpll0_measure(&o,out);}
int main(void)
{
    regs=mmap(NULL,CE_GPLL0_BYTES,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(regs!=MAP_FAILED);reset();
    uint32_t before[CE_GPLL0_BYTES/4];memcpy(before,regs,sizeof(before));
    struct ceph_clock_sample out={0};assert(!run(&out));
    unsigned total=checks,stores=effects;
    assert(finished==1 && !kept && counters==2 && ceph_measure_native.phase==CE_ME_DONE);
    assert(out.short_count!=out.full_count && out.full_hz>299999900 && out.full_hz<300000100);
    assert(out.short_quantum_hz==1171 && out.full_quantum_hz==74);
    assert(out.short_ns && out.full_ns>out.short_ns);
    for(unsigned n=0;n<CE_GPLL0_BYTES/4;++n) {
        if(n==0x6203c/4)continue; /* read-only counter retains last data */
        uint32_t mask=n==0x62008/4 || n==0x43008/4?0x0fffffffu:UINT32_MAX;
        assert((regs[n]&mask)==(before[n]&mask));
    }
    assert(run(&out)==CE_ME_INVALID && finished==1);
    for(unsigned n=1;n<=total;++n) {
        reset();expire=n;memset(&out,0xa5,sizeof(out));
        assert(run(&out));
        assert(out.full_hz==UINT64_C(0xa5a5a5a5a5a5a5a5));
        assert(finished==(ceph_measure_native.attempted_stores!=0));
        if(finished)assert(kept && ceph_measure_native.phase==CE_ME_POISONED);
        assert(run(&out)==CE_ME_INVALID);
    }
    for(int n=1;n<=4;++n) {
        reset();fault=n;assert(run(&out));
        assert(finished==1 && kept==(unsigned)(n<=2));
        assert(ceph_measure_native.phase==(n<=2?CE_ME_POISONED:CE_ME_REFUSED));
    }
    for(unsigned n=1;n<=stores;++n) {
        reset();fault=5;inject_store=n;assert(run(&out) && applied && kept && finished==1);
    }
    reset();fault=6;inject_store=5;assert(run(&out) && kept);
    reset();fault=7;inject_store=5;assert(run(&out) && kept && ceph_gpll0_native.phase==CE_GP_POISONED);
    reset();fault=8;inject_store=1;assert(run(&out) && kept);
    reset();fault=9;assert(run(&out) && finished && !kept);
    reset();fault=10;assert(run(&out)==CE_ME_IO && finished && kept);
    reset();timer_fault=1;early_ticks=1;assert(run(&out)==CE_ME_TIMEOUT && finished && kept);
    reset();timer_fault=1;fault=2;assert(run(&out)==CE_ME_TIMEOUT && kept && finished);
    reset();timer_fault=2;time_expire=3;assert(run(&out) && finished && kept);
    for(unsigned n=0;n<CE_ME_NREG;++n) {
        reset();*reg((enum ceph_measure_reg)n)=UINT32_MAX;assert(run(&out) && !finished);
    }
    reset();*reg(CE_ME_CTL)=CE_ME_ENABLE|0x1000;assert(run(&out)==CE_ME_BUSY && !finished);
    reset();*reg(CE_ME_CTL)=0x40000000;assert(run(&out)==CE_ME_BUSY && !finished);
    reset();*reg(CE_ME_DEBUG)|=1;*reg(CE_ME_XO)|=1;
    assert(!run(&out) && (*reg(CE_ME_DEBUG)&1) && (*reg(CE_ME_XO)&1) && !kept);
    reset();*reg(CE_ME_CTL)=0;assert(!run(&out) && !*reg(CE_ME_CTL) && !kept);
    reset();*reg(CE_ME_CTL)=0x1000;assert(!run(&out) && *reg(CE_ME_CTL)==0x1000 && !kept);
    reset();*reg(CE_ME_MUX)=0x1167;*reg(CE_ME_DIV)=0x100;
    assert(!run(&out) && effects==stores-4 && !kept);
    reset();now=UINT64_MAX-99999999;timer_fault=1;assert(run(&out)==CE_ME_IO && !finished);
    reset();context_bad=1;assert(run(&out)==CE_ME_INVALID && !checks);
    reset();ceph_gpll0_lock=1;assert(run(&out)==CE_ME_INVALID && !checks);
    reset();ceph_gpll0_native.phase=CE_GP_REJECTED;assert(run(&out) && !finished);
    reset();struct ceph_measure_owner o={regs,NULL,finish};
    assert(cepheus_gpu_gpll0_measure(&o,&out)==CE_ME_INVALID && !checks);
    o=(struct ceph_measure_owner){regs,measure_owner,NULL};
    assert(cepheus_gpu_gpll0_measure(&o,&out)==CE_ME_INVALID && !checks);
    assert(run(NULL)==CE_ME_INVALID && !checks);
    reset();uint32_t v=123;
    assert(ceph_measure_read(&ceph_measure_native,CE_ME_NREG,&v)==CE_ME_IO && v==123);
    assert(ceph_measure_write(&ceph_measure_native,CE_ME_STATUS,1,1)==CE_ME_INVALID);
    assert(ceph_measure_write(&ceph_measure_native,CE_ME_MUX,0x800,0)==CE_ME_INVALID);
    assert(ceph_measure_write(&ceph_measure_native,CE_ME_CTL,CE_ME_ENABLE,CE_ME_ENABLE)==CE_ME_INVALID);
    struct ceph_clock_sample r={.short_count=0,.full_count=1};assert(ceph_measure_counts(&r));
    r=(struct ceph_clock_sample){.short_count=10,.full_count=10};assert(ceph_measure_counts(&r));
    r=(struct ceph_clock_sample){.short_count=CE_ME_COUNT_MASK,.full_count=1};assert(ceph_measure_counts(&r));
    assert(!munmap(regs,CE_GPLL0_BYTES));
    printf("Clock counter native adapter: %u protected-aperture lease faults/%u store mutations, short/full counts, fresh DONE, bounded deadlines, restoration/retention PASS (host hardware model, not phone measurement)\n",total,stores);
}
