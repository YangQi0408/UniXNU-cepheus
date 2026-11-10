#define _DEFAULT_SOURCE 1
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "cepheus_gpu_tsens_native.h"
static volatile uint32_t *windows[4];
static unsigned checks,expire,maps,fail_map,context,mutate,reenter,overlap,time_calls;
static uint64_t ticks,step;
static struct ceph_gpu_tsens_native_owner owner;
static int ceph_scm_info_context(void *unused){(void)unused;return context?-1:0;}
static int validate(void *ctx)
{
    assert(ctx==windows);++checks;
    if(checks==expire) {
        for(unsigned i=0;i<4;++i)assert(!mprotect((void *)windows[i],4096,PROT_NONE));
        return -1;
    }
    if(mutate){mutate=0;owner.validate=NULL;}
    if(reenter) {
        reenter=0;uint64_t unchanged=0x12345678;
        assert(cepheus_gpu_tsens_bind_native(&owner)<0);
        assert(cepheus_gpu_thermal_now_native(&unchanged)<0 && unchanged==0x12345678);
        struct ceph_gpu_temperature empty;
        assert(cepheus_gpu_temperature_sample_native(&empty)<0 && !empty.valid_mask);
    }
    return 0;
}
static uintptr_t ml_io_map(uint64_t pa,size_t bytes)
{
    static const uint64_t expected[]={0xc263000,0xc265000,0xc222000,0xc223000};
    assert(maps<4 && pa==expected[maps] && bytes==4096);++maps;
    return maps==fail_map?0:(uintptr_t)windows[overlap?0:maps-1];
}
static uint64_t mach_absolute_time(void){++time_calls;ticks+=step;return ticks;}
static void absolutetime_to_nanoseconds(uint64_t t,uint64_t *out){*out=t;}
#include "cepheus_gpu_tsens_native_private.h"
static void reset(void)
{
    for(unsigned i=0;i<4;++i) {
        assert(!mprotect((void *)windows[i],4096,PROT_READ|PROT_WRITE));
        memset((void *)windows[i],0,4096);
    }
    memset(&ceph_tsens_native,0,sizeof(ceph_tsens_native));ceph_tsens_native_lock=0;
    checks=expire=maps=fail_map=context=mutate=reenter=overlap=time_calls=0;ticks=0;step=1000;
    owner=(struct ceph_gpu_tsens_native_owner){windows,validate};
    windows[0][0xe4/4]=windows[1][0xe4/4]=8;
    windows[0][0xdc/4]=(1u<<21)|400;windows[1][0xcc/4]=(1u<<21)|420;
    windows[2][1]=1|(1u<<18);windows[3][1]=1|(1u<<14);
    for(unsigned i=0;i<4;++i)assert(!mprotect((void *)windows[i],4096,PROT_READ));
}
static int run(void)
{
    struct ceph_gpu_temperature s;uint64_t now;
    if(cepheus_gpu_tsens_bind_native(&owner))return -1;
    if(cepheus_gpu_temperature_sample_native(&s))return -1;
    assert(s.valid_mask==3 && s.gpuss0_mc==40000 && s.gpuss1_mc==42000 && s.sampled_ns==1000);
    return cepheus_gpu_thermal_now_native(&now);
}
int main(void)
{
    for(unsigned i=0;i<4;++i) {
        windows[i]=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        assert(windows[i]!=MAP_FAILED);
    }
    reset();assert(!run() && maps==4 && time_calls==3 && !ceph_tsens_native_lock);
    unsigned boundaries=checks;struct ceph_gpu_temperature s;
    assert(!cepheus_gpu_temperature_sample_native(&s) && s.sampled_ns==4000);
    unsigned before=checks;assert(cepheus_gpu_tsens_bind_native(&owner)<0 && checks==before);
    for(unsigned n=1;n<=boundaries;++n) {
        reset();expire=n;assert(run()<0 && checks==n && ceph_tsens_native.failed && !ceph_tsens_native_lock);
        before=checks;uint64_t unchanged=0x12345678;
        assert(cepheus_gpu_temperature_sample_native(&s)<0 && !s.valid_mask);
        assert(cepheus_gpu_thermal_now_native(&unchanged)<0 && unchanged==0x12345678);
        assert(cepheus_gpu_tsens_bind_native(&owner)<0 && checks==before);
    }
    for(unsigned n=1;n<=4;++n) {
        reset();fail_map=n;assert(run()<0 && maps==n && checks==2*n && !time_calls && ceph_tsens_native.failed);
        reset();fail_map=n;expire=2*n;assert(run()<0 && maps==n && checks==expire);
    }
    reset();mutate=1;assert(!run() && !owner.validate);
    reset();reenter=1;assert(!run() && !ceph_tsens_native_lock);
    reset();context=1;assert(run()<0 && !maps && !checks && !ceph_tsens_native.attempted);
    reset();ceph_tsens_native_lock=1;assert(run()<0 && !maps && !checks && ceph_tsens_native_lock);
    reset();owner.validate=NULL;assert(run()<0 && !ceph_tsens_native.attempted);
    reset();assert(cepheus_gpu_tsens_bind_native(NULL)<0);
    assert(cepheus_gpu_temperature_sample_native(NULL)<0 && cepheus_gpu_thermal_now_native(NULL)<0);
    assert(cepheus_gpu_temperature_sample_native(&s)<0 && !s.valid_mask && !checks);
    reset();overlap=1;assert(run()<0 && maps==4 && ceph_tsens_native.failed);
    reset();step=0;assert(run()<0 && ceph_tsens_native.failed);
    reset();step=100000001;assert(run()<0 && ceph_tsens_native.failed);
    reset();assert(!run());ticks=0;
    assert(cepheus_gpu_temperature_sample_native(&s)<0 && !s.valid_mask && ceph_tsens_native.failed);
    reset();assert(!cepheus_gpu_tsens_bind_native(&owner));
    assert(!mprotect((void *)windows[0],4096,PROT_READ|PROT_WRITE));
    windows[0][0xe4/4]=0;assert(!mprotect((void *)windows[0],4096,PROT_READ));
    assert(cepheus_gpu_temperature_sample_native(&s)<0 && !s.valid_mask && ceph_tsens_native.failed);
    for(unsigned i=0;i<4;++i)assert(!munmap((void *)windows[i],4096));
    printf("Native TSENS: %u PROT_NONE owner boundaries, actual adapter/fixed maps/time/copied callbacks, read-only/clock/map-failure terminal refusal PASS (host model)\n",boundaries);
}
