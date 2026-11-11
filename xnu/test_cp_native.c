#define _DEFAULT_SOURCE 1
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "cepheus_cp_native.h"
#include "cepheus_gx_core_native.h"
#include "cepheus_gmu_mmio.h"
/* Fast fault fixture: real native CP implementation, synthetic prerequisites
 * and pool. Separate C/C++ test uses the actual pinned pool and native chain. */
static struct {
    unsigned acquired,failed,sp_clock_off;
    struct ceph_gx_native_result result;
    struct {uint64_t rpmh_token;} owner;
    volatile uint32_t *gpu;
} ceph_gx_native;
static struct {
    struct {volatile uint8_t *base;} gmu,smmu;
    struct {struct {volatile uint8_t *base;} gcc,cc;} power;
    struct ceph_gpu_publication publication;
} ceph_hfi_native;
static unsigned ceph_gpll0_lock,checks,expire,begins,ends,context,upstream_loss;
static unsigned build_state,build_fail,seal_fail,corrupt,final_loss,mutate,reenter;
static unsigned halt_loss,fault_loss,mismatch;
static unsigned output_late;
static struct ceph_cp_workspace workspace;
static struct ceph_gx_smmu_plan plan;
static struct ceph_cp_native_owner owner;
static int ceph_scm_info_context(void *p){(void)p;return context?-1:0;}
static int ceph_gx_native_record(void){return upstream_loss?-1:0;}
static int ceph_gx_native_window(void)
{
    return upstream_loss || !(ceph_gx_native.gpu[0x808]&2u) || ceph_gx_native.gpu[0x823]?-1:0;
}
static int cepheus_gpu_rpmh_check(uint64_t token){return token==1?0:-1;}
int ceph_memory_cp_output_safe(const void *out,size_t bytes)
{(void)out;(void)bytes;return output_late && build_state==2?-1:0;}
int ceph_memory_cp_build_acquire(const struct ceph_cp_workspace *w)
{assert(w->table.pa==plan.context.ttbr0% (1ULL<<48));if(build_fail || build_state)return -1;build_state=1;return 0;}
int ceph_memory_cp_build_validate(const struct ceph_cp_workspace *w)
{(void)w;return build_state==1 && !corrupt?0:-1;}
int ceph_memory_cp_build_finish(const struct ceph_cp_workspace *w,int uncertain)
{(void)w;assert(build_state==1);build_state=uncertain?3:0;return 0;}
int ceph_memory_cp_build_seal(const struct ceph_cp_workspace *w,const struct ceph_gx_smmu_plan *p,
    struct ceph_cp_publication *out)
{
    assert(build_state==1);if(seal_fail || corrupt)return -1;
    *out=(struct ceph_cp_publication){.memory={.group=257,.ttbr0=p->context.ttbr0,.ttbr1=p->context.ttbr1,
        .smr=p->context.smr,.cb=p->context.cb,.slot=p->context.slot,.asid=p->context.asid},.table_used=w->table.used};
    for(unsigned i=0;i<CE_CP_BUFFERS;++i)out->memory.handles[i]=w->handles[i];
    for(unsigned i=0;i<CE_CP_ROOT;++i)out->regions[i]=w->regions[i];
    build_state=2;return 0;
}
int ceph_memory_cp_validate(const struct ceph_cp_publication *p)
{return p->memory.group==257 && build_state==2 && !corrupt?0:-1;}
int ceph_memory_cp_finish(const struct ceph_cp_publication *p,int result)
{assert(p->memory.group==257 && result==CE_MEMORY_CP_UNCERTAIN);build_state=3;return 0;}
static int ceph_memory_hfi_finish(const struct ceph_gpu_publication *p,int result)
{(void)p;(void)result;return 0;}
#define CE_MEMORY_HFI_UNCERTAIN 2
#include "cepheus_cp_native_private.h"
static int begin(void *ctx,const struct ceph_cp_workspace *w,const struct ceph_gx_smmu_plan *p)
{assert(ctx==(void *)ceph_gx_native.gpu && ceph_gpll0_lock && w->table.pa==(p->context.ttbr0&0xffffffffffULL));++begins;return 0;}
static int validate(void *ctx)
{
    assert(ctx==(void *)ceph_gx_native.gpu && ceph_gpll0_lock);++checks;
    if(expire && checks>=expire){assert(!mprotect((void *)ceph_gx_native.gpu,0x40000,PROT_NONE));return -1;}
    if(halt_loss && ceph_cp_native.result.writes)ceph_gx_native.gpu[0x808]=0;
    if(fault_loss && ceph_cp_native.result.writes)ceph_gx_native.gpu[0x823]=1;
    if(mismatch && ceph_cp_native.result.writes)ceph_gx_native.gpu[0x842]=0;
    if(mutate){mutate=0;owner.validate=NULL;workspace.regions[0].va+=4096;plan.context.smr=0;}
    if(reenter) {
        reenter=0;struct ceph_cp_native_result out={0};out.writes=123;
        assert(cepheus_gpu_cp_prepare_native(&owner,&workspace,&plan,&out)<0 && out.writes==123);
    }
    return 0;
}
static void finish(void *ctx,unsigned writes,int keep)
{assert(ctx==(void *)ceph_gx_native.gpu && ceph_gpll0_lock && writes==ceph_cp_native.result.writes && keep);++ends;if(final_loss)expire=checks+1;}
static void reset(void)
{
    assert(!mprotect((void *)ceph_gx_native.gpu,0x40000,PROT_READ|PROT_WRITE));
    memset((void *)ceph_gx_native.gpu,0,0x40000);
    memset(&ceph_cp_native,0,sizeof(ceph_cp_native));
    ceph_gpll0_lock=checks=expire=begins=ends=context=upstream_loss=0;
    build_state=build_fail=seal_fail=corrupt=final_loss=mutate=reenter=0;
    halt_loss=fault_loss=mismatch=output_late=0;
    ceph_gx_native.acquired=1;ceph_gx_native.failed=ceph_gx_native.sp_clock_off=0;
    ceph_gx_native.result=(struct ceph_gx_native_result){.configured=1,.setup=1,.perfcounter=1};
    ceph_gx_native.owner.rpmh_token=1;ceph_gx_native.gpu[0x808]=3;
    for(unsigned i=0;i<CE_CP_BUFFERS;++i)memset(workspace.views[i].cpu,0,workspace.views[i].bytes);
    workspace.state=CE_CP_WS_TABLED;workspace.table=(struct ceph_vm_pool){workspace.views[CE_CP_ROOT].cpu,0x120000000,32,5};
    struct ceph_smmu_geometry g;assert(!ceph_smmu_geometry((1u<<30)|(1u<<27)|(11u<<9)|16,(2u<<28)|8,0x1222,0x10000,&g));
    struct ceph_smmu_stream streams[16]={0};for(unsigned i=0;i<16;++i)streams[i].s2cr=2u<<16;
    assert(!ceph_gx_smmu_plan(streams,16,&g,0,1,2,workspace.table.pa,0,&plan));
    owner=(struct ceph_cp_native_owner){(void *)ceph_gx_native.gpu,begin,validate,finish,1};
}
static int run(struct ceph_cp_native_result *out)
{return cepheus_gpu_cp_prepare_native(&owner,&workspace,&plan,out);}
int main(void)
{
    ceph_gx_native.gpu=mmap(NULL,0x40000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(ceph_gx_native.gpu!=MAP_FAILED);
    for(unsigned i=0;i<CE_CP_BUFFERS;++i) {
        void *cpu=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(cpu!=MAP_FAILED);
        workspace.views[i]=(struct ceph_gpu_memory_view){cpu,0x110000000ULL+0x20000*i,4096};workspace.handles[i]=i+1;
        if(i<CE_CP_ROOT)workspace.regions[i]=(struct ceph_vm_region){0xfc000000ULL+0x10000*i,workspace.views[i].pa,4096,1};
    }
    struct ceph_cp_native_result out;
    reset();assert(!run(&out) && out.writes==45 && out.reads==66 && build_state==2 && ends==1);
    unsigned boundaries=checks;
    ceph_gpll0_lock=1;assert(!ceph_cp_native_provenance(&out.publication));ceph_gpll0_lock=0;
    assert(!ceph_gx_native.gpu[0x807] && ceph_gx_native.gpu[0x808]==3);
    for(unsigned n=1;n<=boundaries;++n) {
        reset();expire=n;memset(&out,0xa5,sizeof(out));
        assert(run(&out)<0 && checks==n && ends==1 && out.writes==0xa5a5a5a5 && !ceph_gpll0_lock);
        if(ceph_cp_native.result.writes)assert(build_state==3);
        unsigned before=checks;assert(run(&out)<0 && checks==before);
    }
    reset();mutate=reenter=1;assert(!run(&out) && !owner.validate && out.writes==45);
    reset();final_loss=1;assert(run(&out)<0 && build_state==3);
    reset();build_fail=1;assert(run(&out)<0 && !ceph_cp_native.result.writes && !build_state);
    reset();seal_fail=1;assert(run(&out)<0 && build_state==3);
    reset();output_late=1;memset(&out,0xa5,sizeof(out));
    assert(run(&out)<0 && build_state==3 && out.writes==0xa5a5a5a5);
    reset();halt_loss=1;assert(run(&out)<0 && build_state==3);
    reset();fault_loss=1;assert(run(&out)<0 && build_state==3);
    reset();mismatch=1;assert(run(&out)<0 && build_state==3);
    reset();corrupt=1;assert(run(&out)<0 && !ceph_cp_native.result.writes);
    reset();assert(run((struct ceph_cp_native_result *)workspace.views[0].cpu)<0 && !begins);
    reset();assert(run((struct ceph_cp_native_result *)ceph_gx_native.gpu)<0 && !begins);
    reset();assert(run((struct ceph_cp_native_result *)&workspace)<0 && !begins);
    reset();upstream_loss=1;assert(run(&out)<0 && !begins);
    reset();context=1;assert(run(&out)<0 && !begins);
    reset();owner.rpmh_token=0;assert(run(&out)<0 && !begins);
    reset();assert(!run(&out));out.publication.regions[0].va+=4096;
    ceph_gpll0_lock=1;
    assert(ceph_cp_native_provenance(&out.publication)<0);
    out=ceph_cp_native.result;((uint32_t *)workspace.views[CE_CP_RESTORE].cpu)[5]^=1;
    assert(ceph_cp_native_provenance(&out.publication)<0);
    ceph_gpll0_lock=0;
    for(unsigned i=0;i<CE_CP_BUFFERS;++i)assert(!munmap(workspace.views[i].cpu,4096));
    assert(!munmap((void *)ceph_gx_native.gpu,0x40000));
    printf("Native CP: %u PROT_NONE owner boundaries,45 fixed writes/66 reads, immutable copy/provenance/atomic output PASS (synthetic pool)\n",boundaries);
}
