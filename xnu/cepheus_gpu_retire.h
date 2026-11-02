/* Kernel-only publication group and retirement API. Source callbacks are
 * trusted platform implementations, not user-controlled function pointers.
 * The production orchestrator executes slumber/park/detach before pool release;
 * caller-supplied ready booleans are not accepted. No default hardware backend.
 */
#ifndef CEPHEUS_GPU_RETIRE_H
#define CEPHEUS_GPU_RETIRE_H
#include <stdint.h>
struct ceph_smmu_plan;
struct ceph_gmu_slumber_ops;
struct ceph_gmu_park_ops;
struct ceph_smmu_detach_ops;
struct ceph_hfi_session;
struct ceph_gpu_publication {
    uint64_t group;
    uint64_t handles[5];
    /* Matches owned root/context identity at publication, not a pointer to
     * mutable caller storage. Caller must not map this memory elsewhere. */
    uint64_t ttbr0,ttbr1;
    uint32_t smr,cb,slot,asid;
};
struct ceph_gpu_retire_ops {
    struct ceph_gmu_slumber_ops *slumber;
    struct ceph_gmu_park_ops *park;
    struct ceph_smmu_detach_ops *detach;
    struct ceph_hfi_session *hfi;
    const struct ceph_smmu_plan *plan;
    unsigned bw,freq,bw_levels,gx_levels;
};
#ifdef __cplusplus
extern "C" {
#endif
/* Atomic five-buffer transition CPU+one view -> SHARED+same view. No MMIO.
 * Buffer index4 is root pool. Publish BEFORE exposing addresses to hardware.
 * Geometry/materialized plan must be validated by higher-level workspace. */
int cepheus_gpu_memory_share_workspace(const uint64_t handles[5],const struct ceph_smmu_plan *,struct ceph_gpu_publication *);
int cepheus_gpu_memory_retire_workspace(const struct ceph_gpu_publication *,struct ceph_gpu_retire_ops *);
#ifdef __cplusplus
}
#endif
#endif
