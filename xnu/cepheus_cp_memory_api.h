/* Kernel-private pool bridge; no crypto implementation or hardware API. */
#ifndef CEPHEUS_CP_MEMORY_API_H
#define CEPHEUS_CP_MEMORY_API_H
#include "cepheus_cp_workspace.h"
struct ceph_cp_memory_view {
    struct ceph_cp_publication publication;
    struct ceph_gpu_memory_view views[CE_CP_BUFFERS];
};
static inline int ceph_cp_memory_output(const struct ceph_gpu_memory_view views[CE_CP_BUFFERS],
    const void *out,size_t bytes,const void *identity,size_t identity_bytes)
{
    uintptr_t dst=(uintptr_t)out,self=(uintptr_t)identity;
    if(!dst || (dst&7u) || !bytes || bytes>UINTPTR_MAX-dst || identity_bytes>UINTPTR_MAX-self ||
        (dst<self+identity_bytes && self<dst+bytes))return -1;
    for(unsigned i=0;i<CE_CP_BUFFERS;++i) {
        uintptr_t base=(uintptr_t)views[i].cpu;
        if(views[i].bytes>UINTPTR_MAX-base ||
            (dst<base+views[i].bytes && base<dst+bytes))return -1;
    }
    return 0;
}
#ifdef __cplusplus
extern "C" {
#endif
int ceph_memory_cp_seal(const uint64_t[CE_CP_BUFFERS],const struct ceph_gpu_memory_view[CE_CP_BUFFERS],
    const struct ceph_vm_region[CE_CP_ROOT],unsigned,const struct ceph_gx_smmu_plan *,struct ceph_cp_publication *);
/* REFUSED only before exposure; uncertain backing stays retained. */
int ceph_memory_cp_acquire(const struct ceph_cp_publication *,struct ceph_cp_memory_view *);
int ceph_memory_cp_validate(const struct ceph_cp_publication *);
int ceph_memory_cp_publish(const struct ceph_cp_publication *);
enum {CE_MEMORY_CP_REFUSED,CE_MEMORY_CP_INSTALLED,CE_MEMORY_CP_UNCERTAIN};
int ceph_memory_cp_finish(const struct ceph_cp_publication *,int);
/* Exclusive CPU preparation pin; blocks ordinary get/put/release/share until
 * native capture either seals this exact workspace or retains uncertainty. */
int ceph_memory_cp_build_acquire(const struct ceph_cp_workspace *);
int ceph_memory_cp_build_validate(const struct ceph_cp_workspace *);
int ceph_memory_cp_build_finish(const struct ceph_cp_workspace *,int uncertain);
int ceph_memory_cp_build_seal(const struct ceph_cp_workspace *,const struct ceph_gx_smmu_plan *,
    struct ceph_cp_publication *);
/* Reject native result storage in any live pool backing, not only CP. */
int ceph_memory_cp_output_safe(const void *,size_t);
/* Installed actual group only; no SMMU ownership or hardware claim. */
int ceph_memory_cp_installed(const struct ceph_cp_publication *);
/* Verify exact installed layout and zero ring/RPTR backing, then barrier. */
int ceph_memory_cp_ring_publish(const struct ceph_cp_publication *);
#ifdef __cplusplus
}
#endif
#endif
