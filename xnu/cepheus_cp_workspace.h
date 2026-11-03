/* CPU-only CP backing. Separate from the GMU SID5 workspace; NO GPU-address
 * assignment, SMMU publication or DMA retirement API exists here yet. A future
 * GX address-space owner must map/seal it before any CP register configuration.
 */
#ifndef CEPHEUS_CP_WORKSPACE_H
#define CEPHEUS_CP_WORKSPACE_H
#include "cepheus_gpu_memory.h"
#include "cepheus_sqe.h"
#include "cepheus_gpu_vm39.h"
enum {CE_CP_RING,CE_CP_RPTR,CE_CP_SQE,CE_CP_RESTORE,CE_CP_SCRATCH,CE_CP_ROOT,CE_CP_BUFFERS};
enum ceph_cp_workspace_state {CE_CP_WS_NEW,CE_CP_WS_BUILDING,CE_CP_WS_READY,CE_CP_WS_RELEASED,CE_CP_WS_RETAINED,CE_CP_WS_TABLED};
struct ceph_cp_workspace {
    uint64_t handles[CE_CP_BUFFERS],firmware;
    struct ceph_gpu_memory_view views[CE_CP_BUFFERS];
    struct ceph_sqe_image sqe;
    struct ceph_vm_pool table;
    struct ceph_vm_region regions[CE_CP_ROOT];
    unsigned leased,firmware_view;
    enum ceph_cp_workspace_state state;
};
#ifdef __cplusplus
extern "C" {
#endif
int cepheus_cp_workspace_prepare(struct ceph_cp_workspace *,const uint8_t *,size_t);
int cepheus_cp_workspace_release(struct ceph_cp_workspace *);
/* Build only CPU page-table data from caller-reserved GPU VAs. Does not install
 * a context or publish DMA. VA reservation/GMEM carveout ownership is external. */
int cepheus_cp_workspace_table(struct ceph_cp_workspace *,const uint64_t va[CE_CP_ROOT]);
#ifdef __cplusplus
}
#endif
#endif
