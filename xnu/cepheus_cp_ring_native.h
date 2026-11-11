/* Native cold ring/SQE addresses while CP stays halted; no submission. */
#ifndef CEPHEUS_CP_RING_NATIVE_H
#define CEPHEUS_CP_RING_NATIVE_H
#include "cepheus_cp_memory_api.h"
struct ceph_cp_ring_native_owner {
    void *ctx;
    /* Genuine GX/SMMU/TBU and CPU/device writer exclusion. Retain on finish;
     * copied callbacks must stay non-recursive and valid throughout boot. */
    int (*begin)(void *,const struct ceph_cp_publication *);
    int (*validate)(void *);
    void (*finish)(void *,unsigned writes,int keep);
    uint64_t rpmh_token;
};
struct ceph_cp_ring_native_result {unsigned writes,configured;};
/* Derives all addresses from the actual enabled native GX SMMU/CP records.
 * No caller MMIO, fake context, WPTR, halt clear, PAS or trust control. */
int cepheus_gpu_cp_ring_native(const struct ceph_cp_ring_native_owner *,struct ceph_cp_ring_native_result *);
#endif
