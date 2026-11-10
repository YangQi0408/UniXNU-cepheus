/* One fixed A640 firmware-version query, never arbitrary commands/startup. */
#ifndef CEPHEUS_GMU_HFI_NATIVE_H
#define CEPHEUS_GMU_HFI_NATIVE_H
#include "cepheus_gpu_retire.h"
#include "cepheus_rpmh_scope.h"
struct ceph_hfi_native_owner {
    void *ctx;
    /* Acquire actual workspace, GMU/SMMU/power/thermal and global-writer
     * exclusion. Native stage records are not that resource ownership. */
    int (*begin)(void *,const struct ceph_gpu_publication *);
    int (*validate)(void *);
    void (*finish)(void *,unsigned packet_words,unsigned doorbells,int keep);
    uint64_t rpmh_token;
};
struct ceph_hfi_native_result {
    uint32_t hfi_version,firmware_version;
    unsigned packet_words,doorbells; /* padded packet size, attempted IRQ stores */
    int error,retained;
};
/* One terminal attempt per boot. Requires actual native SMMU/upload/CM3
 * success for the exact pool publication. No mapping/remap/CP/reset/SMC.
 * Any admitted queue access permanently seals the six-buffer pin. */
int cepheus_gmu_hfi_version_native(const struct ceph_hfi_native_owner *,
    const struct ceph_gpu_publication *,struct ceph_hfi_native_result *);
#endif
