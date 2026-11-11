#define _DEFAULT_SOURCE 1
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "cepheus_cp_ring_native.h"
#include "cepheus_gx_core_native.h"
#include "cepheus_smmu_mmio.h"
/* Actual ring backend, synthetic installed upstream/pool. The mixed C/C++
 * integration also exercises actual CP preparation and SMMU installation. */
static struct {volatile uint32_t *gpu;} ceph_gx_native;
static struct {
    struct {volatile uint8_t *base;} gmu,smmu;
    struct {struct {volatile uint8_t *base;} gcc,cc;} power;
    struct ceph_gpu_publication publication;
} ceph_hfi_native;
static struct {unsigned sealed;} ceph_cp_native;
static struct {
    unsigned acquired,finished,failed,uncertain,pinned;
    struct {uint64_t rpmh_token;} owner;
    struct ceph_smmu_install txn;
    struct ceph_gx_smmu_plan plan;
    struct ceph_cp_publication publication;
    struct ceph_smmu_mmio mmio;
} ceph_gx_smmu_native;
static unsigned ceph_gpll0_lock,checks,expire,ends,begins,quarantine,context,upstream_loss;
static unsigned installed,publishes,publish_fail,late_output,final_loss,mutate,reenter,halt_loss;
static unsigned fault_loss,readback_loss,protection_loss,context_loss;
static struct ceph_cp_ring_native_owner owner;
static int ceph_scm_info_context(void *p){(void)p;return context?-1:0;}
static int cepheus_gpu_rpmh_check(uint64_t token){return token==1?0:-1;}
static int ceph_cp_native_provenance(const struct ceph_cp_publication *p)
{return ceph_cp_native.sealed && p->memory.group==257 && !upstream_loss?0:-1;}
int ceph_memory_cp_installed(const struct ceph_cp_publication *p)
{return p->memory.group==257 && installed==4?0:-1;}
int ceph_memory_cp_ring_publish(const struct ceph_cp_publication *p)
{++publishes;return ceph_memory_cp_installed(p) || publish_fail?-1:0;}
int ceph_memory_cp_output_safe(const void *out,size_t bytes)
{(void)out;(void)bytes;return late_output && ends?-1:0;}
int ceph_memory_cp_finish(const struct ceph_cp_publication *p,int result)
{assert(p->memory.group==257 && result==CE_MEMORY_CP_UNCERTAIN);++quarantine;installed=5;return 0;}
#define CE_MEMORY_HFI_UNCERTAIN 2
static int ceph_memory_hfi_finish(const struct ceph_gpu_publication *p,int result)
{(void)p;assert(result==CE_MEMORY_HFI_UNCERTAIN);return 0;}
static int ceph_gx_smmu_native_access(void *p)
{
    (void)p;
    return upstream_loss || !ceph_gx_smmu_native.acquired || ceph_gx_smmu_native.failed ||
        !(ceph_gx_native.gpu[0x808]&2u) || ceph_gx_native.gpu[0x823]?-1:0;
}
#include "cepheus_cp_ring_native_private.h"
static int begin(void *ctx,const struct ceph_cp_publication *p)
{assert(ctx==(void *)ceph_gx_native.gpu && p->memory.group==257 && ceph_gpll0_lock);++begins;return 0;}
static int validate(void *ctx)
{
    assert(ctx==(void *)ceph_gx_native.gpu && ceph_gpll0_lock);++checks;
    if(expire && checks>=expire) {
        assert(!mprotect((void *)ceph_gx_native.gpu,0x40000,PROT_NONE));
        assert(!mprotect((void *)ceph_gx_smmu_native.mmio.base,0x10000,PROT_NONE));return -1;
    }
    if(mutate){mutate=0;owner.validate=NULL;}
    if(reenter) {
        reenter=0;_Alignas(8) struct ceph_cp_ring_native_result r={123,456};
        assert(cepheus_gpu_cp_ring_native(&owner,&r)<0 && r.writes==123);
    }
    if(ceph_cp_ring_native.result.writes) {
        if(halt_loss)ceph_gx_native.gpu[0x808]=0;
        if(fault_loss)ceph_gx_native.gpu[0x823]=1;
        if(readback_loss)ceph_gx_native.gpu[0x804]=0;
        if(protection_loss)ceph_gx_native.gpu[0x842]=0;
        if(context_loss)*(volatile uint64_t *)(ceph_gx_smmu_native.mmio.base+0x9020)=0;
    }
    return 0;
}
static void finish(void *ctx,unsigned writes,int keep)
{
    assert(ctx==(void *)ceph_gx_native.gpu && ceph_gpll0_lock && keep && writes==ceph_cp_ring_native.result.writes);
    ++ends;if(final_loss)expire=checks+1;
}
static void reset(void)
{
    assert(!mprotect((void *)ceph_gx_native.gpu,0x40000,PROT_READ|PROT_WRITE));
    assert(!mprotect((void *)ceph_gx_smmu_native.mmio.base,0x10000,PROT_READ|PROT_WRITE));
    memset((void *)ceph_gx_native.gpu,0,0x40000);memset((void *)ceph_gx_smmu_native.mmio.base,0,0x10000);
    memset(&ceph_cp_ring_native,0,sizeof(ceph_cp_ring_native));
    ceph_gpll0_lock=checks=expire=ends=begins=quarantine=context=upstream_loss=0;
    publishes=publish_fail=late_output=final_loss=mutate=reenter=halt_loss=0;
    fault_loss=readback_loss=protection_loss=context_loss=0;installed=4;
    ceph_gx_native.gpu[0x808]=3;ceph_cp_native.sealed=1;
    ceph_gx_smmu_native.acquired=ceph_gx_smmu_native.finished=1;
    ceph_gx_smmu_native.failed=ceph_gx_smmu_native.uncertain=ceph_gx_smmu_native.pinned=0;
    ceph_gx_smmu_native.owner.rpmh_token=1;
    ceph_gx_smmu_native.txn=(struct ceph_smmu_install){CE_SM_ENABLED,15,0};
    struct ceph_smmu_geometry g;
    assert(!ceph_smmu_geometry((1u<<30)|(1u<<27)|(11u<<9)|16,(2u<<28)|8,0x1222,0x10000,&g));
    struct ceph_smmu_stream streams[16]={0};for(unsigned n=0;n<16;++n)streams[n].s2cr=2u<<16;
    assert(!ceph_gx_smmu_plan(streams,16,&g,0,1,2,0x110000000ULL,0,&ceph_gx_smmu_native.plan));
    ceph_gx_smmu_native.mmio.geometry=g;ceph_gx_smmu_native.mmio.mapped_bytes=0x10000;
    ceph_gx_smmu_native.mmio.plan=ceph_gx_smmu_native.plan.context;
    ceph_gx_smmu_native.mmio.actlr=0x303;ceph_gx_smmu_native.mmio.lease=ceph_gx_smmu_native_access;
    const struct ceph_smmu_plan *p=&ceph_gx_smmu_native.plan.context;
    ceph_gx_smmu_native.publication=(struct ceph_cp_publication){.memory={.group=257},
        .regions={{0xfc000000,0,32768,0},{0xfc010000,0,4096,0},{0xfc020000,0,32768,0}}};
    const enum ceph_smmu_reg regs[]={CE_SM_SMR,CE_SM_S2CR,CE_SM_CBAR,CE_SM_CBA2R,CE_SM_SCTLR,
        CE_SM_ACTLR,CE_SM_TCR2,CE_SM_TTBR0,CE_SM_TTBR1,CE_SM_TCR,CE_SM_MAIR0,CE_SM_MAIR1};
    const uint64_t values[]={p->smr,p->s2cr_translate,p->cbar,p->cba2r,p->sctlr,
        0x303,p->tcr2,p->ttbr0,p->ttbr1,p->tcr,p->mair0,p->mair1};
    for(unsigned n=0;n<12;++n) {
        uint32_t off;unsigned width;assert(!ceph_smmu_mmio_offset_mode(&ceph_gx_smmu_native.mmio,regs[n],&off,&width,1));
        if(width==8)*(volatile uint64_t *)(ceph_gx_smmu_native.mmio.base+off)=values[n];
        else *(volatile uint32_t *)(ceph_gx_smmu_native.mmio.base+off)=(uint32_t)values[n];
    }
    for(unsigned n=0;n<45;++n) {
        uint32_t word,value;assert(!ceph_cp_protection_word(n,&word,&value));ceph_gx_native.gpu[word]=value;
    }
    owner=(struct ceph_cp_ring_native_owner){(void *)ceph_gx_native.gpu,begin,validate,finish,1};
}
static int run(struct ceph_cp_ring_native_result *out){return cepheus_gpu_cp_ring_native(&owner,out);}
int main(void)
{
    ceph_gx_native.gpu=mmap(NULL,0x40000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    ceph_gx_smmu_native.mmio.base=mmap(NULL,0x10000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(ceph_gx_native.gpu!=MAP_FAILED && ceph_gx_smmu_native.mmio.base!=MAP_FAILED);
    _Alignas(8) struct ceph_cp_ring_native_result out;
    reset();assert(!run(&out) && out.writes==7 && out.configured && ends==1 && publishes==2 && !quarantine);
    assert(ceph_gx_native.gpu[0x808]==3 && !ceph_gx_native.gpu[0x807] && !ceph_gx_native.gpu[0xf400]);
    unsigned boundaries=checks;
    for(unsigned n=1;n<=boundaries;++n) {
        reset();expire=n;out=(struct ceph_cp_ring_native_result){123,456};
        assert(run(&out)<0 && checks==n && ends==1 && quarantine && out.writes==123 && !ceph_gpll0_lock);
        unsigned before=checks;assert(run(&out)<0 && checks==before);
    }
    reset();mutate=reenter=1;assert(!run(&out) && !owner.validate && out.writes==7);
    reset();final_loss=1;assert(run(&out)<0 && quarantine && ceph_cp_ring_native.result.writes==7);
    reset();late_output=1;out.writes=123;assert(run(&out)<0 && quarantine && out.writes==123);
    reset();publish_fail=1;assert(run(&out)<0 && quarantine && !ceph_cp_ring_native.result.writes);
    reset();halt_loss=1;assert(run(&out)<0 && quarantine);
    reset();fault_loss=1;assert(run(&out)<0 && quarantine);
    reset();readback_loss=1;assert(run(&out)<0 && quarantine);
    reset();protection_loss=1;assert(run(&out)<0 && quarantine);
    reset();context_loss=1;assert(run(&out)<0 && quarantine);
    const uint32_t empty[]={0x800,0x801,0x804,0x805,0x806,0x807,0x830,0x831};
    for(unsigned n=0;n<8;++n){reset();ceph_gx_native.gpu[empty[n]]=1;assert(run(&out)<0 && !ceph_cp_ring_native.result.writes);}
    reset();installed=3;assert(run(&out)<0 && !begins);
    reset();ceph_gx_smmu_native.txn.state=CE_SM_NEW;assert(run(&out)<0 && !begins);
    reset();owner.rpmh_token=0;assert(run(&out)<0 && !begins);
    reset();context=1;assert(run(&out)<0 && !begins);
    reset();assert(run((struct ceph_cp_ring_native_result *)ceph_gx_native.gpu)<0 && !begins);
    reset();assert(run((struct ceph_cp_ring_native_result *)&owner)<0 && !begins);
    reset();assert(run((struct ceph_cp_ring_native_result *)&ceph_gx_smmu_native)<0 && !begins);
    reset();assert(!munmap((void *)ceph_gx_native.gpu,0x40000));
    assert(!munmap((void *)ceph_gx_smmu_native.mmio.base,0x10000));
    printf("Native halted ring: %u PROT_NONE boundaries,7 fixed stores, context/protection/empty inheritance/output retention PASS (synthetic upstream/pool)\n",boundaries);
}
