#define _DEFAULT_SOURCE 1
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "cepheus_pdc_mmio.h"
#include "cepheus_gpu_power_mmio.h"
#include "cepheus_gpu_pdc_native.h"
static volatile uint8_t *windows[5];
static const size_t sizes[]={0x90000,0x9000,0x30000,0x10000,0x10000};
static unsigned ceph_gpll0_lock,checks,expire,maps,fail_map,begins,finished,kept,context;
static unsigned begin_error,mutate,reenter;
static struct ceph_pdc_native_owner owner;
static struct ceph_pdc_input input;
static struct ceph_gmu_start_config config;
static int ceph_scm_info_context(void *unused){(void)unused;return context?-1:0;}
static volatile uint32_t *word(unsigned w,unsigned off)
{assert(w<5 && windows[w] && off<sizes[w]);return (volatile uint32_t *)(windows[w]+off);}
static volatile uint32_t *reg(enum ceph_gmu_reg r)
{uint32_t off;assert(!ceph_gmu_register_offset(r,&off));return word(2,off);}
static int validate(void *ctx)
{
    assert(ctx==windows);++checks;
    if(checks==expire) {
        for(unsigned i=0;i<5;++i)assert(!mprotect((void *)windows[i],sizes[i],PROT_NONE));
        return -1;
    }
    if(mutate) {
        mutate=0;owner.begin=NULL;owner.validate=NULL;owner.finish=NULL;
        input.resources[0].address=0;config.chipid=0;
    }
    if(reenter) {
        reenter=0;struct ceph_pdc_install out={123,456,789};
        assert(cepheus_gpu_pdc_install_native(&owner,&input,&config,&out)<0);
        assert(out.attempted==123 && out.terminal==456 && ceph_gpll0_lock);
    }
    return 0;
}
static uintptr_t ml_io_map(uint64_t pa,size_t bytes)
{
    static const uint64_t expected[]={0x100000,0x2c90000,0x2c6a000,0xb290000,0xb490000};
    assert(maps<5 && pa==expected[maps] && bytes==sizes[maps]);++maps;
    return maps==fail_map?0:(uintptr_t)windows[maps-1];
}
#include "cepheus_gpu_pdc_native_private.h"
static int begin(void *ctx,const struct ceph_pdc_input *in,const struct ceph_gmu_start_config *c)
{
    assert(ctx==windows && ceph_gpll0_lock && c->chipid==0x06040001);
    assert(in->resources[0].address==0x30010);++begins;return begin_error?-7:0;
}
static void finish(void *ctx,int keep)
{assert(ctx==windows && ceph_gpll0_lock && ceph_pdc_native.finished);++finished;kept=(unsigned)keep;}
static void reset(void)
{
    for(unsigned i=0;i<5;++i) {
        assert(!mprotect((void *)windows[i],sizes[i],PROT_READ|PROT_WRITE));
        memset((void *)windows[i],0,sizes[i]);
    }
    memset(&ceph_pdc_native,0,sizeof(ceph_pdc_native));
    ceph_gpll0_lock=checks=expire=maps=fail_map=begins=finished=kept=context=0;
    begin_error=mutate=reenter=0;
    owner=(struct ceph_pdc_native_owner){windows,begin,validate,finish};
    config=(struct ceph_gmu_start_config){0x06040001,0x60100000,20480,0x60200000,4096};
    input=(struct ceph_pdc_input){.chipid=0x06040001,.cfg_pa=0xb290000,.seq_pa=0xb490000};
    for(unsigned i=0;i<3;++i)
        input.resources[i]=(struct ceph_arc_resource){.address=(uint32_t[]){0x30010,0x30000,0x30090}[i],
            .levels={0,16,48,64},.count=4};
    *word(0,0x71004)=1;*word(1,0x106c)=0x222000;*word(1,0x1540)=1u<<31;*word(1,0x100c)=0x222001;
    *word(1,0x1098)=*word(1,0x109c)=*word(1,0x1078)=1;
    *reg(CE_GMU_SYSRESET)=1;*reg(CE_GMU_TCM_RETENTION)=1;*word(2,0x1f8d0u*4-0x6a000)=0xc0;
}
static int run(struct ceph_pdc_install *out)
{return cepheus_gpu_pdc_install_native(&owner,&input,&config,out);}
int main(void)
{
    for(unsigned i=0;i<5;++i) {
        windows[i]=mmap(NULL,sizes[i],PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        assert(windows[i]!=MAP_FAILED);
    }
    struct ceph_pdc_install out;
    reset();assert(!run(&out) && out.attempted==48 && out.terminal && !out.quarantined);
    assert(maps==5 && finished==1 && kept && !ceph_gpll0_lock && ceph_pdc_native.mmio.cursor==48);
    assert(*word(3,0x1140*4)==0x80000001 && *word(4,0)==0xfebea1e1 && *reg(CE_GMU_SYSRESET)==1);
    unsigned boundaries=checks,before=checks;
    out=(struct ceph_pdc_install){123,456,789};assert(run(&out)<0 && checks==before && out.attempted==123);
    for(unsigned n=1;n<=boundaries;++n) {
        reset();expire=n;assert(run(&out)<0 && checks==n && finished==1 && !ceph_gpll0_lock);
        assert(kept==(out.attempted!=0) && out.quarantined==(out.attempted!=0));
        before=checks;assert(run(&out)<0 && checks==before && finished==1);
    }
    for(unsigned n=1;n<=5;++n) {
        reset();fail_map=n;assert(run(&out)<0 && maps==n && checks==2*n && finished==1 && !kept);
        reset();fail_map=n;expire=2*n;assert(run(&out)<0 && maps==n && checks==expire && !kept);
    }
    reset();begin_error=1;assert(run(&out)==-7 && begins==1 && !finished && !maps);
    reset();context=1;assert(run(&out)<0 && !begins && !ceph_pdc_native.attempted);
    reset();ceph_gpll0_lock=1;assert(run(&out)<0 && !begins && ceph_gpll0_lock);
    reset();owner.finish=NULL;assert(run(&out)<0 && !begins);
    reset();assert(cepheus_gpu_pdc_install_native(NULL,&input,&config,&out)<0);
    assert(cepheus_gpu_pdc_install_native(&owner,NULL,&config,&out)<0);
    assert(cepheus_gpu_pdc_install_native(&owner,&input,NULL,&out)<0);
    assert(cepheus_gpu_pdc_install_native(&owner,&input,&config,NULL)<0);
    reset();input.resources[2].count=3;assert(run(&out)<0 && !begins);
    reset();mutate=1;assert(!run(&out) && finished==1 && !owner.finish && !config.chipid);
    reset();reenter=1;assert(!run(&out) && finished==1);
    reset();*reg(CE_GMU_TCM_RETENTION)=0;assert(run(&out)<0 && !kept && !out.attempted);
    reset();*reg(CE_GMU_SYSRESET)=0;assert(run(&out)<0 && !kept && !out.attempted);
    reset();*word(2,0x1f8d0u*4-0x6a000)=0;assert(run(&out)<0 && !kept && !out.attempted);
    for(unsigned i=0;i<3;++i) {
        reset();*word(3,(uint32_t[]){0x1140*4,0x1573*4,0x15d7*4}[i])=1;
        assert(run(&out)<0 && !kept && !out.attempted);
    }
    reset();*word(0,0x71004)=0x80000000;
    assert(!mprotect((void *)windows[1],sizes[1],PROT_NONE));
    assert(!mprotect((void *)windows[2],sizes[2],PROT_NONE));assert(run(&out)<0 && !kept);
    reset();*word(1,0x1540)=0;
    assert(!mprotect((void *)windows[2],sizes[2],PROT_NONE));assert(run(&out)<0 && !kept);
    for(unsigned i=0;i<5;++i)assert(!munmap((void *)windows[i],sizes[i]));
    printf("Native PDC/RSC: %u PROT_NONE boundaries, 48 ordered effects, fixed maps/copied inputs, cold/access gates and uncertain enable retention PASS (host model)\n",boundaries);
}
