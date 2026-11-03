/* CPU-only CP backing. Separate from the GMU SID5 workspace; NO GPU-address
 * assignment, SMMU publication or DMA retirement API exists here yet. A future
 * GX address-space owner must map/seal it before any CP register configuration.
 */
#ifndef CEPHEUS_CP_WORKSPACE_H
#define CEPHEUS_CP_WORKSPACE_H
#include "cepheus_gpu_memory.h"
#include "cepheus_sqe.h"
enum {CE_CP_RING,CE_CP_RPTR,CE_CP_SQE,CE_CP_RESTORE,CE_CP_SCRATCH,CE_CP_BUFFERS};
enum ceph_cp_workspace_state {CE_CP_WS_NEW,CE_CP_WS_BUILDING,CE_CP_WS_READY,CE_CP_WS_RELEASED,CE_CP_WS_RETAINED};
struct ceph_cp_workspace {
    uint64_t handles[CE_CP_BUFFERS],firmware;
    struct ceph_gpu_memory_view views[CE_CP_BUFFERS];
    struct ceph_sqe_image sqe;
    unsigned leased,firmware_view;
    enum ceph_cp_workspace_state state;
};
#ifdef __cplusplus
extern "C" {
#endif
int cepheus_cp_workspace_prepare(struct ceph_cp_workspace *,const uint8_t *,size_t);
int cepheus_cp_workspace_release(struct ceph_cp_workspace *);
#ifdef __cplusplus
}
#endif
#endif
