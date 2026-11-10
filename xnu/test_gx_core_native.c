#define _DEFAULT_SOURCE 1
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "cepheus_gx_core_native.h"
#include "cepheus_gmu_hfi_native.h"
#include "cepheus_gmu_hfi_link.h"
#include "cepheus_gpu_memory_private.h"
#include "../tests/gpll0-scope-fixture.h"
/* Fast exhaustive access fixture: upstream native records/HFI access are
 * synthetic here. The separate actual-pool chain runs their real backends. */
static struct {
    unsigned sealed,pinned,failed,boot_ready,dcvs_attempted,start_attempted;
    struct ceph_hfi_native_result result;
    struct ceph_hfi_boot_native_result boot;
    struct ceph_hfi_start_native_result start;
    struct ceph_gmu_hfi_link link;
    struct ceph_hfi_native_owner owner;
    struct ceph_gpu_publication publication;
    struct ceph_gmu_mmio gmu;
} ceph_hfi_native;
static struct {
    unsigned acquired,failed,release_attempted;
    struct ceph_gmu_oob txn;
    struct {uint64_t rpmh_token;} owner;
} ceph_oob_native;
static volatile uint8_t *gpu,*gmu;
static unsigned ceph_gpll0_lock,checks,expire,maps,begins,ends,stores,kept,uncertain;
static unsigned context,map_fail,map_revoke,final_loss,mutation,reentry,access_fail,corrupt;
static unsigned halt_loss,fault_loss,token_loss;
static struct ceph_gx_native_owner owner;
static int ceph_scm_info_context(void *unused){(void)unused;return context?-1:0;}
static void revoke(void)
{
    assert(!mprotect((void *)gpu,0x40000,PROT_NONE));
    assert(!mprotect((void *)gmu,0x30000,PROT_NONE));
}
static int ceph_hfi_native_access(void *unused)
{
    (void)unused;
    return access_fail || ceph_hfi_native.failed || cepheus_gpu_rpmh_check(1)?-1:0;
}
int ceph_memory_hfi_finish(const struct ceph_gpu_publication *p,int result)
{
    assert(p==&ceph_hfi_native.publication && result==CE_MEMORY_HFI_UNCERTAIN);
    ++uncertain;return 0;
}
static uintptr_t ml_io_map(uint64_t pa,size_t bytes)
{
    assert(pa==0x2c00000 && bytes==0x40000 && ceph_gpll0_lock);++maps;
    if(map_revoke)expire=checks+1;
    return map_fail?0:(uintptr_t)gpu;
}
#include "cepheus_gx_core_native_private.h"
static int begin(void *ctx,const struct ceph_gpu_publication *p,const struct ceph_gx_core_input *i)
{
    assert(ctx==(void *)gpu && p==&ceph_hfi_native.publication && i->chipid==0x06040001 &&
        i->gmem_base==0x100000 && i->gmem_bytes==0x100000 && ceph_gpll0_lock);++begins;return 0;
}
static int validate(void *ctx)
{
    assert(ctx==(void *)gpu && ceph_gpll0_lock);++checks;
    if(expire && checks>=expire){revoke();return -1;}
    if(mutation){mutation=0;owner.validate=NULL;owner.rpmh_token=0;}
    if(reentry) {
        reentry=0;struct ceph_gx_native_result out={.error=123};
        assert(cepheus_gpu_gx_core_native(&owner,&out)<0 && out.error==123 && ceph_gpll0_lock);
    }
    if(ceph_gx_native.result.writes) {
        if(corrupt)((volatile uint32_t *)gpu)[0xe05]=0;
        if(halt_loss)((volatile uint32_t *)gpu)[0x808]=0;
        if(fault_loss)((volatile uint32_t *)gpu)[0x823]=1;
        if(token_loss)ceph_oob_native.owner.rpmh_token=0;
    }
    return 0;
}
static void finish(void *ctx,unsigned writes,int keep)
{
    assert(ctx==(void *)gpu && ceph_gpll0_lock);++ends;stores=writes;kept=keep;
    if(final_loss)expire=checks+1;
}
static void reset(void)
{
    assert(!mprotect((void *)gpu,0x40000,PROT_READ|PROT_WRITE));
    assert(!mprotect((void *)gmu,0x30000,PROT_READ|PROT_WRITE));
    memset((void *)gpu,0,0x40000);memset((void *)gmu,0,0x30000);
    memset(&ceph_gx_native,0,sizeof(ceph_gx_native));
    memset(&ceph_hfi_native,0,sizeof(ceph_hfi_native));
    memset(&ceph_oob_native,0,sizeof(ceph_oob_native));
    ceph_gpll0_lock=checks=expire=maps=begins=ends=stores=kept=uncertain=0;
    context=map_fail=map_revoke=final_loss=mutation=reentry=access_fail=corrupt=0;
    halt_loss=fault_loss=token_loss=0;
    gpll0_scope_reset();scope_revoke=revoke;
    owner=(struct ceph_gx_native_owner){(void *)gpu,begin,validate,finish,1};
    ceph_hfi_native.sealed=ceph_hfi_native.pinned=ceph_hfi_native.boot_ready=1;
    ceph_hfi_native.dcvs_attempted=ceph_hfi_native.start_attempted=1;
    ceph_hfi_native.start.dcvs_acked=ceph_hfi_native.start.start_acked=1;
    ceph_hfi_native.result.firmware_version=0x20000042;
    ceph_hfi_native.link.bound=1;ceph_hfi_native.owner.rpmh_token=1;
    ceph_hfi_native.gmu=(struct ceph_gmu_mmio){gmu,0x30000,
        {0x06040001,0x60000000,0x5000,0x60010000,0x1000},0x20000001,(void *)gpu,validate};
    ceph_oob_native.acquired=1;ceph_oob_native.txn.state=CE_OOB_HELD;
    ceph_oob_native.owner.rpmh_token=1;((volatile uint32_t *)gpu)[0x808]=2;
}
int main(void)
{
    gpu=mmap(NULL,0x40000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    gmu=mmap(NULL,0x30000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(gpu!=MAP_FAILED && gmu!=MAP_FAILED);
    struct ceph_gx_native_result out;
    reset();assert(!cepheus_gpu_gx_core_native(&owner,&out));
    assert(out.configured && out.retained && out.writes==23 && maps==1 && begins==1 && ends==1 && kept);
    unsigned owner_boundaries=checks,scope_boundaries=scope_checks;
    for(unsigned n=0;n<23;++n)assert(((volatile uint32_t *)gpu)[ceph_gx_native.plan.writes[n].word]==ceph_gx_native.plan.writes[n].value);
    for(unsigned n=1;n<=owner_boundaries;++n) {
        reset();expire=n;
        assert(cepheus_gpu_gx_core_native(&owner,&out)<0 && checks==n && ceph_gx_native.failed && uncertain==1 && ends==1 && kept);
        unsigned before=checks;out.error=123;
        assert(cepheus_gpu_gx_core_native(&owner,&out)<0 && out.error==123 && checks==before);
    }
    for(unsigned n=3;n<=scope_boundaries;++n) {
        reset();scope_expire=n;
        assert(cepheus_gpu_gx_core_native(&owner,&out)<0 && scope_checks==n && ceph_gx_native.failed && uncertain==1 && ends==1 && kept);
    }
    reset();mutation=reentry=1;assert(!cepheus_gpu_gx_core_native(&owner,&out) && !owner.validate);
    reset();map_fail=1;assert(cepheus_gpu_gx_core_native(&owner,&out)<0 && !out.writes && kept && uncertain);
    reset();map_revoke=1;assert(cepheus_gpu_gx_core_native(&owner,&out)<0 && maps==1 && checks==expire && !out.writes);
    reset();final_loss=1;assert(cepheus_gpu_gx_core_native(&owner,&out)<0 && out.writes==23 && !out.configured && checks==expire);
    for(unsigned n=0;n<4;++n) {
        reset();if(n==0)corrupt=1;if(n==1)halt_loss=1;if(n==2)fault_loss=1;if(n==3)token_loss=1;
        assert(cepheus_gpu_gx_core_native(&owner,&out)<0 && out.writes==1 && uncertain);
    }
    reset();context=1;out.error=123;assert(cepheus_gpu_gx_core_native(&owner,&out)<0 && out.error==123 && !begins);
    reset();ceph_gpll0_lock=1;out.error=123;assert(cepheus_gpu_gx_core_native(&owner,&out)<0 && out.error==123 && !begins);
    reset();ceph_hfi_native.start.start_acked=0;out.error=123;
    assert(cepheus_gpu_gx_core_native(&owner,&out)<0 && out.error==123 && !ceph_gx_native.attempted && !begins);
    assert(!munmap((void *)gpu,0x40000) && !munmap((void *)gmu,0x30000));
    printf("Actual GX backend: %u owner/%u scope PROT_NONE boundaries, fixed23 writes/readback/CP halt/sticky failure PASS (synthetic upstream records)\n",owner_boundaries,scope_boundaries-2);
}
