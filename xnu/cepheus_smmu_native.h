/* Native GMU SMMUv2 installer. No global reset, ownership grant or CP start. */
#ifndef CEPHEUS_SMMU_NATIVE_H
#define CEPHEUS_SMMU_NATIVE_H
#include "cepheus_smmu_install.h"
#include "cepheus_gpu_retire.h"
struct ceph_smmu_native_owner {
    void *ctx;
    /* Acquire real powered SMMU/TBU/GMU access, skip-init/secure constraints,
     * selected SMR/CB/ASID reservation, published workspace exclusion and
     * thermal supervision. Failure unwinds without hidden device stores. */
    int (*begin)(void *,const struct ceph_smmu_plan *,const struct ceph_gpu_publication *);
    /* Protect all fixed mapping footprints, root/PTE identity, cold CM3,
     * shared rails/clocks/TBU and every relevant writer/idle transition. */
    int (*validate)(void *);
    /* Non-sleeping boot-lifetime callbacks under the GPLL0 lock; no recursive
     * serialized native calls. keep=1 retains every hardware prerequisite
     * after publication/stores; never detach/reset or free on uncertainty. */
    void (*finish)(void *,int keep);
};
/* One-shot, kernel thread context. Takes a real shared pool publication, not
 * arbitrary table pointers/publish callbacks. Success is installed SID5 only,
 * not GMU execution, PAS authentication or permission to unhalt CP. */
int cepheus_gpu_smmu_install_native(const struct ceph_smmu_native_owner *,
    const struct ceph_smmu_plan *,const struct ceph_gpu_publication *,struct ceph_smmu_install *);
#endif
