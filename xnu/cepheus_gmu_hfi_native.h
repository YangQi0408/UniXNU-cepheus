/* One fixed A640 firmware-version query, never arbitrary commands/startup. */
#ifndef CEPHEUS_GMU_HFI_NATIVE_H
#define CEPHEUS_GMU_HFI_NATIVE_H
#include "cepheus_gpu_retire.h"
#include "cepheus_rpmh_scope.h"
#include "cepheus_gpu_config_native.h"
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
struct ceph_hfi_boot_native_result {
    uint32_t hfi_version,firmware_version;
    unsigned acked,doorbells;
    int error,retained;
};
/* Continue the SAME successfully queried native session with fixed tables,
 * features and CORE_FW_START. Requires the private native config record;
 * bin1 also requires actual QMP ACD consumption before native CM3 startup.
 * This entry does not send DCVS or START and does not access GX/CP. */
int cepheus_gmu_hfi_boot_native(const struct ceph_gpu_config *,struct ceph_hfi_boot_native_result *);
struct ceph_hfi_start_native_result {
    unsigned dcvs_acked,start_acked,doorbells;
    int error,retained;
};
/* Fixed default GX level1/BW INVALID, then START only with the actual native
 * OOB0 held. No arbitrary DCVS arguments, GX access, CP unhalt or submission. */
int cepheus_gmu_hfi_dcvs_native(struct ceph_hfi_start_native_result *);
int cepheus_gmu_hfi_start_native(struct ceph_hfi_start_native_result *);
#endif
