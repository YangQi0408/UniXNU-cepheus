/* Private CP execution admission identity. It binds already-retained records;
 * it does not clear halt, write WPTR, submit a packet or prove global ownership. */
#ifndef CEPHEUS_CP_EXECUTION_ADMISSION_H
#define CEPHEUS_CP_EXECUTION_ADMISSION_H
#include "cepheus_cp_memory_api.h"
#include "cepheus_cp_ring_config.h"
#include "cepheus_cp_ring_native.h"
#include "cepheus_gx_smmu_plan.h"
#include "cepheus_zap_memory_private.h"

struct ceph_cp_execution_admission;
struct ceph_cp_execution_admission_owner {
    void *ctx;
    int (*validate)(void *); /* genuine GX/TBU/CP/CPU-device writer lease */
    /* Once after the one admission attempt, under SCM/GPLL0 serialization. The
     * record is a read-only copy owned by the admission boundary and must be
     * checked against the owner's own ring/CP/SMMU/ZAP ledger. keep=1 means
     * every attempted admission retains its external owner resources. */
    void (*finish)(void *,const struct ceph_cp_execution_admission *,int keep);
};

#define CE_CP_EXECUTION_ADMISSION_VERSION 1u

static inline int ceph_cp_execution_admission_overlap(const void *a,size_t an,
    const void *b,size_t bn)
{
    if(!a || !b || !an || !bn)return 0;
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x<=UINTPTR_MAX-an && y<=UINTPTR_MAX-bn &&
        x<y+bn && y<x+an;
}

static inline int ceph_cp_execution_admission_same_ring(const struct ceph_cp_ring_plan *a,
    const struct ceph_cp_ring_plan *b)
{
    if(!a || !b)return 0;
    for(unsigned n=0;n<7;++n)if(a->writes[n].word!=b->writes[n].word ||
        a->writes[n].value!=b->writes[n].value)return 0;
    return 1;
}
static inline int ceph_cp_execution_admission_same_cp(const struct ceph_cp_publication *a,
    const struct ceph_cp_publication *b)
{
    if(!a || !b)return 0;
    const struct ceph_gpu_publication *x=&a->memory,*y=&b->memory;
    if(a->table_used!=b->table_used || x->group!=y->group || x->ttbr0!=y->ttbr0 ||
        x->ttbr1!=y->ttbr1 || x->smr!=y->smr || x->cb!=y->cb || x->slot!=y->slot ||
        x->asid!=y->asid)return 0;
    for(unsigned n=0;n<CE_GPU_WORKSPACE_BUFFERS;++n)if(x->handles[n]!=y->handles[n])return 0;
    for(unsigned n=0;n<CE_CP_ROOT;++n) {
        const struct ceph_vm_region *p=&a->regions[n],*q=&b->regions[n];
        if(p->va!=q->va || p->pa!=q->pa || p->bytes!=q->bytes || p->permissions!=q->permissions)return 0;
    }
    for(unsigned n=0;n<32;++n)if(a->restore_sha256[n]!=b->restore_sha256[n])return 0;
    return 1;
}
static inline int ceph_cp_execution_admission_same_smmu(const struct ceph_gx_smmu_plan *a,
    const struct ceph_gx_smmu_plan *b)
{
    if(!a || !b)return 0;
    const struct ceph_smmu_plan *x=&a->context,*y=&b->context;
    return a->actlr==b->actlr && x->smr==y->smr && x->s2cr_fault==y->s2cr_fault &&
        x->s2cr_translate==y->s2cr_translate && x->cbar==y->cbar && x->cba2r==y->cba2r &&
        x->sctlr==y->sctlr && x->tcr2==y->tcr2 && x->ttbr0==y->ttbr0 && x->ttbr1==y->ttbr1 &&
        x->tcr==y->tcr && x->mair0==y->mair0 && x->mair1==y->mair1 && x->cb==y->cb &&
        x->slot==y->slot && x->asid==y->asid;
}

/* Standalone test translation units provide these before including this
 * header. The production SWEET unit already defines them in KERNEL context. */
struct ceph_cp_execution_admission {
    uint32_t version;
    uint32_t ring_writes;
    uint64_t admission_token;
    uint64_t zap_epoch;
    _Alignas(8) struct ceph_zap_claim zap;
    struct ceph_cp_publication cp;
    struct ceph_gx_smmu_plan smmu;
    struct ceph_cp_ring_plan ring;
    uint64_t rpmh_token;
};

/* Pure identity check over copied records. Live MMIO/owner/ZAP checks belong
 * to the private native admission wrapper, never to this helper. */
static inline int ceph_cp_execution_admission_plan(
    const struct ceph_cp_publication *cp,const struct ceph_gx_smmu_plan *smmu,
    const struct ceph_cp_ring_plan *actual_ring,const struct ceph_cp_ring_native_result *ring,
    uint64_t epoch,const struct ceph_zap_claim *zap,uint64_t rpmh_token,uint64_t token,
    struct ceph_cp_execution_admission *out)
{
    if(!cp || !smmu || !actual_ring || !ring || !zap || !out || !epoch || !rpmh_token || !token ||
        !zap->handle || !ring->configured || ring->writes!=7 ||
        !ceph_gx_smmu_plan_canonical(smmu) || !cp->memory.group || !cp->table_used ||
        cp->table_used>32 || cp->memory.ttbr0!=smmu->context.ttbr0 ||
        cp->memory.ttbr1!=smmu->context.ttbr1 || cp->memory.smr!=smmu->context.smr ||
        cp->memory.cb!=smmu->context.cb || cp->memory.slot!=smmu->context.slot ||
        cp->memory.asid!=smmu->context.asid)return -1;
    uintptr_t dst=(uintptr_t)out;
    if(!dst || (dst&7u) || dst>UINTPTR_MAX-sizeof(*out))return -1;
    const void *inputs[]={cp,smmu,actual_ring,ring,zap};
    const size_t bytes[]={sizeof(*cp),sizeof(*smmu),sizeof(*actual_ring),sizeof(*ring),sizeof(*zap)};
    for(unsigned n=0;n<5;++n)
        if(ceph_cp_execution_admission_overlap(out,sizeof(*out),inputs[n],bytes[n]))return -1;
    for(unsigned i=0;i<CE_CP_BUFFERS;++i) {
        if(!cp->memory.handles[i])return -1;
        for(unsigned j=0;j<i;++j)if(cp->memory.handles[i]==cp->memory.handles[j])return -1;
    }
    for(unsigned i=0;i<CE_CP_ROOT;++i) {
        const struct ceph_vm_region *r=&cp->regions[i];
        if(!r->va || !r->pa || !r->bytes || r->pa>=(1ULL<<40) || r->bytes>(1ULL<<40)-r->pa ||
            r->permissions!=ceph_cp_workspace_permissions(i))return -1;
    }
    const struct ceph_vm_region *r=cp->regions;
    const struct ceph_cp_ring_input input={r[CE_CP_RING].va,r[CE_CP_RPTR].va,r[CE_CP_SQE].va,
        r[CE_CP_RING].bytes,r[CE_CP_RPTR].bytes,r[CE_CP_SQE].bytes};
    struct ceph_cp_ring_plan expected;
    if(ceph_cp_ring_plan(&input,&expected) || !ceph_cp_execution_admission_same_ring(&expected,actual_ring))return -1;
    struct ceph_cp_execution_admission candidate={0};
    candidate.version=CE_CP_EXECUTION_ADMISSION_VERSION;candidate.ring_writes=ring->writes;
    candidate.admission_token=token;candidate.zap_epoch=epoch;candidate.rpmh_token=rpmh_token;
    candidate.zap=*zap;candidate.cp=*cp;candidate.smmu=*smmu;candidate.ring=*actual_ring;
    *out=candidate;return 0;
}
#endif
