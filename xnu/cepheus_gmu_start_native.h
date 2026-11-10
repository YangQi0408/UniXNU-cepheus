/* Native CM3 cold-start transport. No owner grant, firmware upload or CP start. */
#ifndef CEPHEUS_GMU_START_NATIVE_H
#define CEPHEUS_GMU_START_NATIVE_H
#include "cepheus_gmu_start.h"
#include "cepheus_gpu_config_native.h"
struct ceph_gmu_start_native_owner {
    void *ctx;
    /* Acquire actual cold power/PDC/SMMU/image-publication prerequisites and
     * thermal supervision. Failure unwinds partial acquisition without writes.
     * No serialized parent/power API calls while the GPLL0 lock is held. */
    int (*begin)(void *,const struct ceph_gmu_start_config *);
    /* Valid across reset release; cold reset is checked separately. Protect
     * firmware/workspace identity, mappings, foreign writers and ongoing
     * power/clock/SMMU/PDC/thermal state, not caller-provided ready booleans. */
    int (*validate)(void *);
    /* Non-sleeping boot-lifetime callbacks. keep=1 retains every prerequisite
     * after attempted stores, including uncertain release; never reset/free. */
    void (*finish)(void *,int keep);
};
/* Boot-lifetime one-shot. Requires an exact native retained config copy;
 * bin1 requires QMP ACD consumption before admission, not after release.
 * Fixed GCC/GPUCC/GMU maps and actual time/delay.
 * No startup caller. A result is CM3/HFI readiness only, never GPU execution. */
int cepheus_gmu_start_cold_native(const struct ceph_gmu_start_native_owner *,
    const struct ceph_gmu_start_config *,const struct ceph_gpu_config *,struct ceph_gmu_start *);
#endif
