/* Native GX SID0 installer; CP remains halted, no security fallback. */
#ifndef CEPHEUS_GX_SMMU_NATIVE_H
#define CEPHEUS_GX_SMMU_NATIVE_H
#include "cepheus_gx_smmu_install.h"
#include "cepheus_cp_memory_api.h"
struct ceph_gx_smmu_native_owner {
    void *ctx;
    /* Reserve both TBUs, exact CB/slot/ASID and CPU/device writer exclusion.
     * Acquire without hidden stores/reset; failure unwinds its own attempt. */
    int (*begin)(void *,const struct ceph_gx_smmu_plan *,const struct ceph_cp_publication *);
    int (*validate)(void *);
    /* Boot-lifetime, non-sleeping/non-recursive under GPLL0 serialization.
     * Existing HFI/OOB/GX prerequisites are retained even before exposure. */
    void (*finish)(void *,unsigned effects,int keep);
    uint64_t rpmh_token;
};
/* One-shot after actual native GX setup; derives CP views from the real pool.
 * Installed translation is not authentication or command execution. */
int cepheus_gpu_gx_smmu_install_native(const struct ceph_gx_smmu_native_owner *,
    const struct ceph_gx_smmu_plan *,const struct ceph_cp_publication *,struct ceph_smmu_install *);
#endif
