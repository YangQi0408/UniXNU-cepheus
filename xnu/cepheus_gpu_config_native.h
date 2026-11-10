/* Actual RPMh command-db + completed boot-local eFuse sample, no votes. */
#ifndef CEPHEUS_GPU_CONFIG_NATIVE_H
#define CEPHEUS_GPU_CONFIG_NATIVE_H
#include "cepheus_gpu_config.h"
#include "cepheus_rpmh_scope.h"
struct ceph_gpu_config_native_owner {
    void *ctx;
    /* Verify the actual board/profile against this expected FDT identity,
     * and retain command-db mapping/data stability. This is not supplied by
     * the local RPMh writer scope or a matching caller-provided chip ID. */
    int (*begin)(void *,const char *expected_fdt_sha256);
    int (*validate)(void *);
    void (*finish)(void *,int error);
    uint64_t rpmh_token;
};
/* One attempt per boot; no rh_map, hardware store, SMC or startup caller.
 * A successful parsed value is copied; no external DB callback/fuse is used.
 * Output is unchanged on refusal. The owner must keep the inputs immutable. */
int cepheus_gpu_config_build_native(const struct ceph_gpu_config_native_owner *,
    struct ceph_gpu_config *);
/* Check an exact semantic copy against the private retained record and owner.
 * Success is configuration identity only, never resource admission. */
int cepheus_gpu_config_check_native(const struct ceph_gpu_config *);
#endif
