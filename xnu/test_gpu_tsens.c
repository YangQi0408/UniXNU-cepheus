#define _DEFAULT_SOURCE 1
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "cepheus_gpu_tsens.h"
static volatile uint32_t *ram[2],*srot[2];
struct host {unsigned calls,fail,time_calls,time_fail,change_at,change_kind;uint64_t now,step;int backward;};
static void protect(int flags)
{
    for(unsigned i=0;i<2;++i) {
        assert(!mprotect((void *)ram[i],4096,flags));
        assert(!mprotect((void *)srot[i],4096,flags));
    }
}
static int lease(void *p)
{
    struct host *h=p;
    if(++h->calls==h->fail){protect(PROT_NONE);return -1;}
    if(h->calls==h->change_at) {
        if(h->change_kind==0)srot[1][1]=1;
        else if(h->change_kind==1)ram[1][0xe4/4]=0;
        else ram[1][0x14/4]=1u<<11;
    }
    return 0;
}
static int now(void *p,uint64_t *v)
{
    struct host *h=p;++h->time_calls;
    if(h->time_calls==h->time_fail)return -1;
    if(h->backward && h->time_calls==2)h->now-=h->step;
    else h->now+=h->step;
    *v=h->now;return 0;
}
int main(void)
{
    for(unsigned i=0;i<2;++i) {
        ram[i]=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        srot[i]=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        assert(ram[i]!=MAP_FAILED && srot[i]!=MAP_FAILED);
        memset((void *)ram[i],0,4096);memset((void *)srot[i],0,4096);
    }
    struct host h={.step=1000};struct ceph_gpu_tsens m={{ram[0],ram[1]},{4096,4096},{0xc263000,0xc265000},&h,lease,now,
        {srot[0],srot[1]},{4096,4096},{0xc222000,0xc223000}};
    srot[0][1]=1|(1u<<18);srot[1][1]=1|(1u<<14);
    struct ceph_gpu_temperature s;ram[0][0xe4/4]=ram[1][0xe4/4]=8;
    ram[0][0xdc/4]=(1u<<21)|400;ram[1][0xcc/4]=(1u<<21)|420;
    assert(!ceph_gpu_tsens_sample(&m,&s) && s.valid_mask==3 && s.gpuss0_mc==40000 && s.gpuss1_mc==42000 && s.sampled_ns==1000);
    unsigned calls=h.calls;
    srot[1][1]=1;assert(ceph_gpu_tsens_sample(&m,&s)<0 && !s.valid_mask);srot[1][1]=1|(1u<<14);
    srot[0][1]=1u<<18;assert(ceph_gpu_tsens_sample(&m,&s)<0 && !s.valid_mask);srot[0][1]=1|(1u<<18);
    ram[0][0x14/4]=1u<<31;assert(ceph_gpu_tsens_sample(&m,&s)<0 && !s.valid_mask && ram[0][0x14/4]==(1u<<31));ram[0][0x14/4]=0;
    ram[1][0x14/4]=1u<<11;assert(ceph_gpu_tsens_sample(&m,&s)<0 && !s.valid_mask);ram[1][0x14/4]=0;
    for(unsigned fail=1;fail<=calls;++fail) {
        protect(PROT_READ|PROT_WRITE);h=(struct host){.step=1000,.fail=fail};
        assert(ceph_gpu_tsens_sample(&m,&s)<0 && !s.valid_mask && h.calls==fail);
    }
    protect(PROT_READ|PROT_WRITE);
    h=(struct host){.step=1000};ram[0][0xdc/4]=400;assert(ceph_gpu_tsens_sample(&m,&s)<0 && !s.valid_mask);
    ram[0][0xdc/4]=(1u<<21)|0xf9c;assert(!ceph_gpu_tsens_sample(&m,&s) && s.gpuss0_mc==-10000);
    ram[0][0xdc/4]=(1u<<21)|600;assert(!ceph_gpu_tsens_sample(&m,&s));
    struct ceph_gpu_thermal t={0};assert(!ceph_gpu_thermal_start(&t,h.now,30000000000ULL));
    assert(ceph_gpu_thermal_check(&t,&s,h.now)==CE_THERMAL_HOT);
    ram[1][0xe4/4]=0;assert(ceph_gpu_tsens_sample(&m,&s)<0);
    ram[1][0xe4/4]=UINT32_MAX;assert(ceph_gpu_tsens_sample(&m,&s)<0);ram[1][0xe4/4]=8;
    ram[1][0xcc/4]=UINT32_MAX;assert(ceph_gpu_tsens_sample(&m,&s)<0);ram[1][0xcc/4]=(1u<<21)|420;
    h=(struct host){.step=100000001};assert(ceph_gpu_tsens_sample(&m,&s)<0);
    h=(struct host){0};assert(ceph_gpu_tsens_sample(&m,&s)<0);
    h=(struct host){.step=1000,.backward=1};assert(ceph_gpu_tsens_sample(&m,&s)<0 && !s.valid_mask);
    for(unsigned i=1;i<=2;++i) {
        h=(struct host){.step=1000,.time_fail=i};assert(ceph_gpu_tsens_sample(&m,&s)<0 && !s.valid_mask);
    }
    for(unsigned i=0;i<3;++i) {
        srot[1][1]=1|(1u<<14);ram[1][0xe4/4]=8;ram[1][0x14/4]=0;
        h=(struct host){.step=1000,.change_at=20,.change_kind=i};
        assert(ceph_gpu_tsens_sample(&m,&s)<0 && !s.valid_mask && h.calls>=20);
    }
    srot[1][1]=1|(1u<<14);ram[1][0xe4/4]=8;ram[1][0x14/4]=0;
    h=(struct host){.step=1000};protect(PROT_READ);
    assert(!ceph_gpu_tsens_sample(&m,&s));protect(PROT_READ|PROT_WRITE);
    struct ceph_gpu_tsens bad=m;bad.pa[1]++;assert(ceph_gpu_tsens_sample(&bad,&s)<0);
    bad=m;bad.tm[1]=bad.tm[0];assert(ceph_gpu_tsens_sample(&bad,&s)<0);
    bad=m;bad.srot[1]=bad.tm[0];assert(ceph_gpu_tsens_sample(&bad,&s)<0);
    bad=m;bad.srot[1]=(volatile uint32_t *)((uintptr_t)bad.tm[0]+4);assert(ceph_gpu_tsens_sample(&bad,&s)<0);
    bad=m;bad.tm[1]=(volatile uint32_t *)(UINTPTR_MAX-3u);assert(ceph_gpu_tsens_sample(&bad,&s)<0);
    bad=m;bad.bytes[1]--;assert(ceph_gpu_tsens_sample(&bad,&s)<0);
    bad=m;bad.lease=NULL;assert(ceph_gpu_tsens_sample(&bad,&s)<0);
    assert(ceph_gpu_tsens_sample(NULL,&s)<0 && ceph_gpu_tsens_sample(&m,NULL)<0);
    uint32_t value=0x12345678;srot[0][1]=UINT32_MAX;h.calls=0;
    assert(ceph_gpu_tsens_load(&m,0,1,4,&value)<0 && h.calls==2 && value==0x12345678);
    assert(ceph_gpu_tsens_load(&m,2,0,4,&value)<0);
    assert(ceph_gpu_tsens_load(&m,0,2,4,&value)<0);
    assert(ceph_gpu_tsens_load(&m,0,0,4096,&value)<0);
    assert(ceph_gpu_tsens_load(&m,0,0,1,&value)<0);
    int mc;assert(ceph_gpu_tsens_decode((1u<<21)|1251,&mc)<0 && ceph_gpu_tsens_decode((1u<<21)|0x800,&mc)<0);
    for(unsigned i=0;i<2;++i){assert(!munmap((void *)ram[i],4096));assert(!munmap((void *)srot[i],4096));}
    printf("TSENS GPU sampler: %u PROT_NONE owner boundaries, per-load/time guards, fixed sensors/read-only maps, stopped time refusal PASS (host model)\n",calls);
}
