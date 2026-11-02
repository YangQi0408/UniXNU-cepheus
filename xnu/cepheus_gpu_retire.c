#include "cepheus_gpu_memory_private.h"
#include "cepheus_smmu_detach.h"
#include "cepheus_gmu_slumber.h"
#include "cepheus_gmu_park.h"

int cepheus_gpu_memory_share_workspace(const uint64_t handles[CE_GPU_WORKSPACE_BUFFERS],const struct ceph_smmu_plan *p,struct ceph_gpu_publication *out)
{
    if(!ceph_smmu_plan_canonical(p) || !out)return -1;
    struct ceph_gpu_publication identity={0};
    identity.ttbr0=p->ttbr0;identity.ttbr1=p->ttbr1;identity.smr=p->smr;
    identity.cb=p->cb;identity.slot=p->slot;identity.asid=p->asid;
    return ceph_memory_share_group(handles,&identity,out);
}

int cepheus_gpu_memory_retire_workspace(const struct ceph_gpu_publication *p,struct ceph_gpu_retire_ops *b)
{
    if(!p || !b || !b->slumber || !b->park || !b->detach || !b->hfi || !ceph_smmu_plan_canonical(b->plan) ||
        p->ttbr0!=b->plan->ttbr0 || p->ttbr1!=b->plan->ttbr1 || p->smr!=b->plan->smr ||
        p->cb!=b->plan->cb || p->slot!=b->plan->slot || p->asid!=b->plan->asid)return -1;
    if(ceph_memory_retire_begin(p))return -1;
    struct ceph_gmu_slumber slumber={0};struct ceph_gmu_park park={0};struct ceph_smmu_detach detach={0};
    int e=ceph_gmu_slumber_request(&slumber,b->slumber,b->hfi,b->bw,b->freq,b->bw_levels,b->gx_levels);
    if(!e)e=ceph_gmu_park_slumbered(&park,b->park);
    if(!e)e=ceph_smmu_detach_context(&detach,b->detach,b->plan);
    /* These engines check ACK AND hardware state, scoped TLB sync and context
     * identity. The trusted backend keeps admission/park held until return. */
    int released=ceph_memory_retire_finish(p,e==0);
    return e?e:released;
}
