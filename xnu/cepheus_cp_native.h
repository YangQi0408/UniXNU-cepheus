/* Native protected restore preparation only; no ring stores or CP execution. */
#ifndef CEPHEUS_CP_NATIVE_H
#define CEPHEUS_CP_NATIVE_H
#include "cepheus_cp_memory_api.h"
#include "cepheus_cp_init.h"
struct ceph_cp_native_owner {
    void *ctx;
    /* Real GX/protection/CPU/device writer exclusion, acquired without hidden
     * stores. Copied callbacks must remain valid and non-recursive for boot. */
    int (*begin)(void *,const struct ceph_cp_workspace *,const struct ceph_gx_smmu_plan *);
    int (*validate)(void *);
    void (*finish)(void *,unsigned writes,int keep);
    uint64_t rpmh_token;
};
struct ceph_cp_native_result {
    struct ceph_cp_publication publication;
    uint32_t packet[12];
    unsigned writes,reads;
};
/* One attempt after actual native perf setup with OOB0 held. Copies exact
 * TABLED workspace, pins real backing, writes45 fixed protection fields,
 * captures66 registers, then seals under the pool lock. Failure leaves out
 * unchanged; any attempted store uncertainty retains backing for boot. */
int cepheus_gpu_cp_prepare_native(const struct ceph_cp_native_owner *,const struct ceph_cp_workspace *,
    const struct ceph_gx_smmu_plan *,struct ceph_cp_native_result *);
#endif
