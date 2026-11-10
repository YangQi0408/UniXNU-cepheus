/* Native immutable-snapshot upload. No reset release, PAS or CP start. */
#ifndef CEPHEUS_GMU_UPLOAD_NATIVE_H
#define CEPHEUS_GMU_UPLOAD_NATIVE_H
#include "cepheus_gmu_upload_mmio.h"
#include "cepheus_gpu_retire.h"
struct ceph_gmu_upload_native_owner {
    void *ctx;
    /* Acquire cold retained CM3, actual CX/clocks/SMMU/TBU and thermal
     * supervision, plus immutable snapshot/workspace writer exclusion.
     * No hidden prerequisite stores; failures unwind without device effects. */
    int (*begin)(void *,const struct ceph_gpu_publication *,uint64_t snapshot);
    /* Cover all fixed mapping footprints, installed SID5/root identity,
     * actual rails/clocks and every resource-affecting writer/idle transition. */
    int (*validate)(void *);
    /* Non-sleeping callbacks under the GPLL0 lock; no recursive native calls.
     * keep=1 retains hardware prerequisites after any attempted destination
     * store or failed cleanup. Never reset/detach/free on uncertainty. */
    void (*finish)(void *,int keep);
};
struct ceph_gmu_upload_native_result {unsigned writes;int error,quarantined;};
/* Boot-lifetime one-shot in kernel thread context. Real sealed A640 snapshot
 * handle and real shared publication only. Success is compared image bytes,
 * not running firmware, PAS authentication or permission to unhalt CP. */
int cepheus_gmu_upload_native(const struct ceph_gmu_upload_native_owner *,uint64_t,
    const struct ceph_gpu_publication *,struct ceph_gmu_upload_native_result *);
#endif
