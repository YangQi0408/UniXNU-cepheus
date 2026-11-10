/* Native cold CX/GMU power transport, not an ownership or frequency source. */
#ifndef CEPHEUS_GPU_POWER_NATIVE_H
#define CEPHEUS_GPU_POWER_NATIVE_H
#include "cepheus_gpu_power_txn.h"
struct ceph_power_native_owner {
    void *ctx;
    /* Same no-writes-on-failure contract as ceph_power_ops.prepare. Acquire
     * cold resources, actual inherited votes, measured parent and live CXO.
     * Called with the GPLL0 lock held: parent hold/measurement must already
     * be complete; callbacks must not reenter those serialized APIs. Native
     * admission also binds their retained parent and successful counter
     * record. Nominal-XO counter consistency is necessary, not calibrated
     * rate proof: prepare still owns that proof and live CXO supervision. */
    int (*prepare)(void *,struct ceph_power_input *);
    int (*validate)(void *);
    int (*vote)(void *,uint32_t,unsigned);
    /* Non-sleeping lifetime: copied callbacks/context survive uncertainty.
     * keep=1 retains all resources; no voltage-off, reset or recovery here. */
    void (*finish)(void *,int keep);
};
/* Boot-lifetime singleton, real mappings/delay, shared GPLL0 serialization.
 * No startup caller. Never accepts caller-provided MMIO or delay functions. */
int cepheus_gpu_power_start_native(const struct ceph_power_native_owner *,struct ceph_power_txn *);
#endif
