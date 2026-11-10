/* Fixed TSENS mappings and actual kernel time, not a resource-owner grant. */
#ifndef CEPHEUS_GPU_TSENS_NATIVE_H
#define CEPHEUS_GPU_TSENS_NATIVE_H
#include "cepheus_gpu_tsens.h"
struct ceph_gpu_tsens_native_owner {
    void *ctx;
    /* Non-sleeping, boot-lifetime context. Prove both controllers' mappings,
     * ongoing conversion/clock health and exclusion of configuration writers.
     * Own the entire host-page-rounded coverage of all four 4KiB requests:
     * on SWEET 16KiB pages, SROT requests share a physical backing page.
     * Called under the TSENS lock; never reenter the native sampling APIs. */
    int (*validate)(void *);
};
/* Bind once without sensor configuration writes. Failed acquisition/sample
 * latches refusal; mappings remain boot-lifetime, with no recovery or reset.
 * The real platform owner must retain its own resource lifetime separately. */
int cepheus_gpu_tsens_bind_native(const struct ceph_gpu_tsens_native_owner *);
int cepheus_gpu_temperature_sample_native(struct ceph_gpu_temperature *);
int cepheus_gpu_thermal_now_native(uint64_t *);
#endif
