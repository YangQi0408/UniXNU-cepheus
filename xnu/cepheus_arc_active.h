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
};
/* Only cx.lvl/mx.lvl, strictly increasing table indices, no userspace API.
 * Caller updates its owner ledger only on success; any uncertain request must
 * retain the rail lease. No fallback current=0 or retry after an I/O failure. */
int cepheus_gpu_arc_raise(const char *,unsigned,unsigned,const struct ceph_arc_active_owner *);
#endif
