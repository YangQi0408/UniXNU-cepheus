/* Private ARC raise transport, not a resource-owner implementation. The owner
 * must prove the current apps-DRV request, exclusive writers and deep-idle
 * exclusion for the entire call. ACK does not prove physical rail voltage.
 */
#ifndef CEPHEUS_ARC_ACTIVE_H
#define CEPHEUS_ARC_ACTIVE_H
#include "cepheus_arc.h"
struct ceph_arc_active_owner {
    void *ctx;
    int (*validate)(void *,const struct ceph_arc_resource *,unsigned,unsigned);
    /* Non-sleeping guard for initialized RSC/DB mappings, stable command DB,
     * exclusive apps writers and deep-idle exclusion, before decoding too. */
    int (*validate_access)(void *);
    /* Once after any attempted store, while the shared transport lock is held.
     * keep=1 retains resources on uncertainty; keep=0 is ACK+cleanup, not off. */
    void (*finish)(void *,unsigned attempted_stores,int keep);
};
/* Only cx.lvl/mx.lvl, strictly increasing table indices, no userspace API.
 * Caller updates its owner ledger only on success; any uncertain request must
 * retain the rail lease. Uses existing initialized RPMh mappings only.
 * No fallback current=0 or retry after an I/O failure. */
int cepheus_gpu_arc_raise(const char *,unsigned,unsigned,const struct ceph_arc_active_owner *);
#endif
