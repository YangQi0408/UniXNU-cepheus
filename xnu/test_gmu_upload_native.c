#define _DEFAULT_SOURCE 1
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "cepheus_gpu_power_mmio.h"
#include "cepheus_gmu_upload_native.h"
#include "cepheus_gpu_memory_private.h"
static volatile uint8_t *windows[3];
static const size_t sizes[]={0x90000,0x9000,0x30000};
static uint8_t cache[8192],file[80];
static unsigned ceph_gpll0_lock,checks,expire,maps,fail_map,begins,finished,kept,context;
static unsigned begin_error,pin_error,pin_invalid,pins,unpins,published,publish_error,finish_error,pin_result;
static unsigned mutate,reenter,revoke_write,snapshot_error;
static struct ceph_gmu_upload_native_owner owner;
static struct ceph_gpu_publication publication;
static int effects(void);
static int ceph_scm_info_context(void *unused){(void)unused;return context?-1:0;}
static volatile uint32_t *word(unsigned w,unsigned off)
{assert(w<3 && windows[w] && off<sizes[w]);return (volatile uint32_t *)(windows[w]+off);}
static volatile uint32_t *reg(enum ceph_gmu_reg r)
{uint32_t off;assert(!ceph_gmu_register_offset(r,&off));return word(2,off);}
static int validate(void *ctx)
{
    assert(ctx==windows);++checks;
    if(checks==expire || (revoke_write && effects())) {
        for(unsigned i=0;i<3;++i)assert(!mprotect((void *)windows[i],sizes[i],PROT_NONE));
        return -1;
    }
    if(mutate) {
        mutate=0;owner.begin=NULL;owner.validate=NULL;owner.finish=NULL;publication.handles[0]=0;
    }
    if(reenter) {
        reenter=0;struct ceph_gmu_upload_native_result out={123,456,789};
        assert(cepheus_gmu_upload_native(&owner,257,&publication,&out)<0 && out.writes==123 && out.error==456);
    }
    return 0;
}
static uintptr_t ml_io_map(uint64_t pa,size_t bytes)
{
    static const uint64_t expected[]={0x100000,0x2c90000,0x2c6a000};
    assert(maps<3 && pa==expected[maps] && bytes==sizes[maps]);++maps;
    return maps==fail_map?0:(uintptr_t)windows[maps-1];
}
#if !defined(CEPHEUS_UPLOAD_NATIVE_POOL_TEST)
int ceph_memory_upload_acquire(const struct ceph_gpu_publication *p,
    struct ceph_gmu_start_config *config,struct ceph_gmu_mapping *m)
{
    assert(p->handles[0]==1);++pins;if(pin_error)return -1;
    *config=(struct ceph_gmu_start_config){0x06040001,0x60100000,20480,0x60200000,4096};
    m[0]=(struct ceph_gmu_mapping){0x4000,4096,cache,CE_GMU_ICACHE};
    m[1]=(struct ceph_gmu_mapping){0x44000,4096,cache+4096,CE_GMU_DCACHE};return 0;
}
int ceph_memory_upload_validate(const struct ceph_gpu_publication *p)
{assert(p->handles[0]==1);return pin_invalid?-1:0;}
int ceph_memory_upload_publish(const struct ceph_gpu_publication *p)
{assert(p->handles[0]==1);++published;return publish_error?-1:0;}
int ceph_memory_upload_finish(const struct ceph_gpu_publication *p,enum ceph_memory_upload_result result)
{assert(p->handles[0]==1);++unpins;pin_result=(unsigned)result;return finish_error?-1:0;}
int cepheus_gpu_upload_snapshot(struct ceph_gmu_upload *u,uint64_t handle,struct ceph_gmu_block *plan,unsigned capacity)
{
    assert(handle==257);if(snapshot_error)return -1;
    return ceph_gmu_upload_file(u,file,sizeof(file),plan,capacity);
}
#else
extern int cepheus_gpu_upload_snapshot(struct ceph_gmu_upload *,uint64_t,struct ceph_gmu_block *,unsigned);
#endif
#include "cepheus_gmu_upload_native_private.h"
static int effects(void){return ceph_upload_native.upload.writes!=0;}
static int begin(void *ctx,const struct ceph_gpu_publication *p,uint64_t snapshot)
{assert(ctx==windows && ceph_gpll0_lock && p->handles[0] && snapshot);++begins;return begin_error?-7:0;}
static void finish(void *ctx,int keep)
{assert(ctx==windows && ceph_gpll0_lock && ceph_upload_native.finished);++finished;kept=(unsigned)keep;}
static void le32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(8*i));}
static void reset(void)
{
    for(unsigned i=0;i<3;++i) {
        assert(!mprotect((void *)windows[i],sizes[i],PROT_READ|PROT_WRITE));memset((void *)windows[i],0,sizes[i]);
    }
    memset(&ceph_upload_native,0,sizeof(ceph_upload_native));memset(cache,0,sizeof(cache));
    ceph_gpll0_lock=checks=expire=maps=fail_map=begins=finished=kept=context=0;
    begin_error=pin_error=pin_invalid=pins=unpins=published=publish_error=finish_error=pin_result=0;
    mutate=reenter=revoke_write=snapshot_error=0;
    owner=(struct ceph_gmu_upload_native_owner){windows,begin,validate,finish};
    publication=(struct ceph_gpu_publication){.group=257,.handles={1,2,3,4,5,6},.smr=0x84000005};
    *word(0,0x71004)=1;*word(1,0x106c)=0x222000;*word(1,0x1540)=1u<<31;*word(1,0x100c)=0x222001;
    *word(0,CE_GPU_GCC_MEMNOC)=*word(0,CE_GPU_GCC_DVM)=1;
    *word(1,0x1098)=*word(1,0x109c)=*word(1,0x1078)=1;
    *reg(CE_GMU_SYSRESET)=*reg(CE_GMU_TCM_RETENTION)=1;
    const unsigned addresses[]={0,0x40000,0x4000,0x44000};memset(file,0,sizeof(file));
    for(unsigned i=0;i<4;++i){le32(file+20*i,addresses[i]);le32(file+20*i+4,4);le32(file+20*i+16,0x12345678+i);}
}
static void allocate_windows(void)
{
    for(unsigned i=0;i<3;++i) {
        windows[i]=mmap(NULL,sizes[i],PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        assert(windows[i]!=MAP_FAILED);
    }
}
#if defined(CEPHEUS_UPLOAD_NATIVE_POOL_TEST)
int test_upload_native_pool(uint64_t snapshot,const struct ceph_gpu_publication *pub,unsigned mode)
{
    assert(mode<=5 && pub);allocate_windows();reset();publication=*pub;
    if(mode==1)expire=5;
    if(mode==2)revoke_write=1;
    if(mode==3)fail_map=2;
    if(mode==4)*reg(CE_GMU_SYSRESET)=0;
    struct ceph_gmu_upload_native_result out;int e=cepheus_gmu_upload_native(&owner,snapshot,&publication,&out);
    assert((e==0)==(mode==0));assert(finished==1 && !ceph_gpll0_lock);
    if(!mode)assert(out.writes && !out.quarantined && kept && !ceph_upload_native.pinned);
    if(mode==2)assert(out.writes && out.quarantined && kept);
    if(mode!=0 && mode!=2)assert(!out.writes && !out.quarantined && !kept);
    for(unsigned i=0;i<3;++i)assert(!munmap((void *)windows[i],sizes[i]));
    return e;
}
#else
static int run(struct ceph_gmu_upload_native_result *out)
{return cepheus_gmu_upload_native(&owner,257,&publication,out);}
int main(void)
{
    allocate_windows();reset();struct ceph_gmu_upload_native_result out;
    assert(!run(&out) && out.writes==10 && !out.error && !out.quarantined && maps==3);
    assert(finished==1 && kept && pins==1 && unpins==1 && published==2 && pin_result==CE_MEMORY_UPLOAD_COPIED);
    assert(*word(2,0x3000)==0x12345678 && *word(2,0x7000)==0x12345679 && cache[0]==0x7a);
    assert(*reg(CE_GMU_SYSRESET)==1 && !ceph_gpll0_lock);
    unsigned boundaries=checks,before=checks;out=(struct ceph_gmu_upload_native_result){123,456,789};
    assert(run(&out)<0 && out.writes==123 && checks==before);
    for(unsigned n=1;n<=boundaries;++n) {
        reset();expire=n;assert(run(&out)<0 && checks==n && finished==1 && !ceph_gpll0_lock);
        assert(kept==(out.writes!=0) && out.quarantined==(out.writes!=0));
        if(unpins)assert(pin_result==(out.writes?CE_MEMORY_UPLOAD_UNCERTAIN:CE_MEMORY_UPLOAD_REFUSED));
        before=checks;assert(run(&out)<0 && checks==before && finished==1);
    }
    for(unsigned n=1;n<=3;++n) {
        reset();fail_map=n;assert(run(&out)<0 && maps==n && finished==1 && !kept && unpins==1);
        unsigned postcheck=checks;
        reset();fail_map=n;expire=postcheck;assert(run(&out)<0 && checks==expire && !kept && unpins==1);
    }
    reset();begin_error=1;assert(run(&out)==-7 && !finished && !pins && !maps);
    reset();pin_error=1;assert(run(&out)<0 && finished==1 && pins==1 && !unpins && !maps);
    reset();pin_invalid=1;assert(run(&out)<0 && finished==1 && unpins==1 && !kept);
    reset();publish_error=1;assert(run(&out)<0 && out.writes==10 && out.quarantined && kept);
    reset();snapshot_error=1;assert(run(&out)<0 && !out.writes && !kept);
    reset();finish_error=1;assert(run(&out)<0 && out.quarantined && kept && ceph_upload_native.pinned);
    reset();finish_error=1;fail_map=1;assert(run(&out)<0 && !out.writes && out.quarantined && kept && ceph_upload_native.pinned);
    reset();mutate=1;assert(!run(&out) && out.writes==10 && kept);
    reset();reenter=1;assert(!run(&out) && out.writes==10 && kept);
    reset();*reg(CE_GMU_SYSRESET)=0;assert(run(&out)<0 && !out.writes && !kept);
    reset();*reg(CE_GMU_TCM_RETENTION)=0;assert(run(&out)<0 && !out.writes && !kept);
    reset();*word(0,CE_GPU_GCC_MEMNOC)=1u<<31;
    assert(!mprotect((void *)windows[2],sizes[2],PROT_NONE));assert(run(&out)<0 && !out.writes && !kept);
    reset();context=1;out=(struct ceph_gmu_upload_native_result){123,456,789};
    assert(run(&out)<0 && !begins && !maps && out.writes==123);
    for(unsigned i=0;i<3;++i)assert(!munmap((void *)windows[i],sizes[i]));
    printf("Native GMU upload: %u PROT_NONE access boundaries, fixed maps, copied owner/publication, reset/clocks and uncertain cleanup PASS (emulated pool/hardware)\n",boundaries);
}
#endif
