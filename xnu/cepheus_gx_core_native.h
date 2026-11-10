/* Fixed A640v2 core subset only. No CP release, security write or startup hook. */
#ifndef CEPHEUS_GX_CORE_NATIVE_H
#define CEPHEUS_GX_CORE_NATIVE_H
#include "cepheus_gx_core.h"
#include "cepheus_gx_hwcg.h"
#include "cepheus_gx_gbif.h"
#include "cepheus_gx_perfcounter.h"
#include "cepheus_gpu_retire.h"
struct ceph_gx_native_owner {
    void *ctx;
    /* Protect real GX/SPTP/fence/thermal/global writers and reserve the
     * profile's GMEM range. Prior native records do not grant ownership. */
    int (*begin)(void *,const struct ceph_gpu_publication *,const struct ceph_gx_core_input *);
    int (*validate)(void *);
    void (*finish)(void *,unsigned writes,int keep);
    uint64_t rpmh_token;
};
struct ceph_gx_native_result {
    unsigned writes; /* attempted stores, not completed work */
    int configured,retained,error;
    unsigned hwcg_writes,gbif_writes,perf_writes;
    int hwcg_skipped,setup,perfcounter;
};
/* One attempt after actual native query/boot/DCVS/START with OOB0 HELD.
 * Fixed map/register plan, no caller VA/register/value/ready flag.
 * Requires a real external owner; there is no production caller yet. */
int cepheus_gpu_gx_core_native(const struct ceph_gx_native_owner *,struct ceph_gx_native_result *);
/* Same singleton/owner/map, fixed vendor order HWCG -> GBIF -> core.
 * Still no perfcounter/LLCC/security/CP initialization or execution. */
int cepheus_gpu_gx_setup_native(const struct ceph_gx_native_owner *,struct ceph_gx_native_result *);
/* Adds busy/performance/IFPC counters at their vendor positions within setup.
 * Still no LLCC, GX SMMU, security, CP publication or execution. */
int cepheus_gpu_gx_setup_perf_native(const struct ceph_gx_native_owner *,struct ceph_gx_native_result *);
#endif
