/* Internal C/C++ bridge, not a public driver interface. Only retirement
 * orchestrator may thaw/release a publication after actual protocol success.
 * Kernel code remains trusted; this is lifetime discipline, not a security
 * boundary against arbitrary privileged callers. No IOUserClient exposes it.
 */
#ifndef CEPHEUS_GPU_MEMORY_PRIVATE_H
#define CEPHEUS_GPU_MEMORY_PRIVATE_H
#include "cepheus_gpu_retire.h"
#include "cepheus_gpu_memory.h"
#ifdef __cplusplus
extern "C" {
#endif
int ceph_memory_share_group(const uint64_t[CE_GPU_WORKSPACE_BUFFERS],const struct ceph_gpu_publication *,struct ceph_gpu_publication *);
int ceph_memory_validate_workspace(const struct ceph_gpu_memory_view[CE_GPU_WORKSPACE_BUFFERS],const struct ceph_gpu_publication *);
int ceph_memory_retire_begin(const struct ceph_gpu_publication *);
/* success means the orchestrator finished all protocol operations, not a
 * caller assertion. False permanently quarantines the entire group. */
int ceph_memory_retire_finish(const struct ceph_gpu_publication *,int success);
struct ceph_gmu_start_config;
/* Native installer only. Pin the actual shared six-buffer group against
 * retirement/abandonment, derive its config, and order its Normal-NC tables.
 * The platform must separately exclude CPU/device/foreign writers. */
int ceph_memory_smmu_acquire(const struct ceph_gpu_publication *,struct ceph_gmu_start_config *);
int ceph_memory_smmu_validate(const struct ceph_gpu_publication *);
int ceph_memory_smmu_publish(const struct ceph_gpu_publication *);
enum ceph_memory_smmu_result {CE_MEMORY_SMMU_REFUSED,CE_MEMORY_SMMU_INSTALLED,CE_MEMORY_SMMU_UNCERTAIN};
/* Actual installer result: uncertainty permanently retains/quarantines the
 * real group. Installed ends this temporary pin, not the hardware lease. */
int ceph_memory_smmu_finish(const struct ceph_gpu_publication *,enum ceph_memory_smmu_result);
struct ceph_gmu_mapping;
/* Actual cache destinations, never caller-selected CPU mappings. Separate
 * temporary upload pin excludes installation, retirement and abandonment.
 * The platform still owns hardware access and excludes all foreign writers. */
int ceph_memory_upload_acquire(const struct ceph_gpu_publication *,struct ceph_gmu_start_config *,struct ceph_gmu_mapping *);
int ceph_memory_upload_validate(const struct ceph_gpu_publication *);
int ceph_memory_upload_publish(const struct ceph_gpu_publication *);
enum ceph_memory_upload_result {CE_MEMORY_UPLOAD_REFUSED,CE_MEMORY_UPLOAD_COPIED,CE_MEMORY_UPLOAD_UNCERTAIN};
int ceph_memory_upload_finish(const struct ceph_gpu_publication *,enum ceph_memory_upload_result);
#ifdef __cplusplus
}
#endif
#endif
