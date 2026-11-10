/* Fixed cold AOP connection and GPU ACD=1 message, not channel ownership. */
#ifndef CEPHEUS_QMP_NATIVE_H
#define CEPHEUS_QMP_NATIVE_H
#include "cepheus_qmp_connect.h"
struct ceph_qmp_native_owner {
    void *ctx;
    /* Acquire the actual mailbox/IPCC/config bandwidth and exclude every
     * local/foreign writer. No writes on failure; no callback reentry. */
    int (*begin)(void *);
    int (*validate)(void *);
    /* Called once after acquisition (or uncertain scope keep). Clean success
     * still keeps the new connected channel, never disconnects or clears RX. */
    void (*finish)(void *,unsigned stores,unsigned notifies,int keep);
    uint64_t rpmh_token;
};
struct ceph_qmp_native_result {
    struct ceph_qmp_connect connection;
    struct ceph_qmp_tx acd;
    int error;
};
/* One terminal attempt, shared GPLL0 serialization, real fixed maps/time/delay.
 * No startup caller; consumption is not an application-level ACD ACK. */
int cepheus_gpu_qmp_acd_native(const struct ceph_qmp_native_owner *,struct ceph_qmp_native_result *);
#endif
