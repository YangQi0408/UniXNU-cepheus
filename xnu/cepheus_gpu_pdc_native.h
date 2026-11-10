/* Native A640v2 PDC/RSC installer. No owner grant, reset release or CP start. */
#ifndef CEPHEUS_GPU_PDC_NATIVE_H
#define CEPHEUS_GPU_PDC_NATIVE_H
#include "cepheus_gpu_pdc.h"
#include "cepheus_gmu_start.h"
struct ceph_pdc_native_owner {
    void *ctx;
    /* Acquire actual cold PDC/RSC, CX/access clocks, firmware/workspace
     * identity, command-db resources and thermal supervision. Establish no
     * prerequisite by hidden stores; failure unwinds partial acquisition. */
    int (*begin)(void *,const struct ceph_pdc_input *,const struct ceph_gmu_start_config *);
    /* Protect the fixed mapping footprints, cold CM3/GX and resource levels
     * from every relevant writer/idle transition. Not supplied ready flags. */
    int (*validate)(void *);
    /* Non-sleeping boot-lifetime callbacks under the GPLL0 lock. No recursive
     * serialized native calls. keep=1 retains all prerequisites after any
     * attempted store, including uncertain PDC enable; never reset/free. */
    void (*finish)(void *,int keep);
};
/* Boot-lifetime one-shot. Fixed maps, copied input/config/callbacks. No caller
 * at startup; PDC programming is not an ARC ACK or GPU execution result. */
int cepheus_gpu_pdc_install_native(const struct ceph_pdc_native_owner *,
    const struct ceph_pdc_input *,const struct ceph_gmu_start_config *,struct ceph_pdc_install *);
#endif
