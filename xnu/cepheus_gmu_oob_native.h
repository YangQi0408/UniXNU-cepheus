/* Fixed GPU OOB0, bound to this boot's actual native CM3 record. */
#ifndef CEPHEUS_GMU_OOB_NATIVE_H
#define CEPHEUS_GMU_OOB_NATIVE_H
#include "cepheus_gmu_oob.h"
#include "cepheus_rpmh_scope.h"
struct ceph_oob_native_owner {
    void *ctx;
    /* Acquire actual HFI/workspace/thermal/OOB0 and foreign-writer lifetime.
     * Failure has no effects. A native CM3 record is not this ownership. */
    int (*begin)(void *);
    int (*validate)(void *);
    /* Close submission admission and prove drained CP/fences before release. */
    int (*drained)(void *);
    /* Once at acquired transaction end, and once after attempted release
     * effect. keep=1 never releases the resource/transport/idle lifetime. */
    void (*finish)(void *,unsigned stores,int keep);
    uint64_t rpmh_token;
};
/* One terminal acquisition and at most one release admission attempt. Real
 * retained CM3 maps/time/delay, no caller version, no remap/reset/CP operation. */
int cepheus_gpu_oob_acquire_native(const struct ceph_oob_native_owner *,struct ceph_gmu_oob *);
int cepheus_gpu_oob_release_native(struct ceph_gmu_oob *);
#endif
