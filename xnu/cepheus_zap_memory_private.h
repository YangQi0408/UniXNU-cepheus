/* Trusted PAS orchestrator bridge. No user dispatch or caller-supplied safe
 * boolean: only the load/stop engine may advance these lifecycle operations. */
#ifndef CEPHEUS_ZAP_MEMORY_PRIVATE_H
#define CEPHEUS_ZAP_MEMORY_PRIVATE_H
#include "cepheus_zap_memory.h"
struct ceph_zap_claim {uint64_t handle;struct ceph_zap_memory_view memory;};
enum ceph_zap_memory_result {CE_ZAP_CPU_ABORT,CE_ZAP_KEEP,CE_ZAP_AUTHENTICATED,CE_ZAP_STOPPED};
#ifdef __cplusplus
extern "C" {
#endif
/* Claims the pair exclusively and retains its immutable source snapshot. */
int ceph_zap_memory_claim(uint64_t snapshot,struct ceph_zap_claim *);
/* Irreversible retention BEFORE the first possible SMC exposure. */
int ceph_zap_memory_expose(uint64_t handle);
/* Called only after a successful PAS mem_setup; never touches the MDT. */
int ceph_zap_memory_reload(uint64_t handle);
/* AUTHENTICATED/STOPPED require actual protocol success in the orchestrator.
 * A failed or interrupted protocol goes to KEEP, never CPU_ABORT/STOPPED. */
int ceph_zap_memory_finish(uint64_t handle,enum ceph_zap_memory_result);
/* Actual authenticated allocator record only, not a PAS or owner proof.
 * Requires exact retained claim identity; never returns caller-facing VAs. */
int ceph_zap_memory_authenticated(const struct ceph_zap_claim *);
/* Caller holds the SCM/GPLL0 admission lock order and serializes the ZAP
 * engine; checks the same allocator record without acquiring pool lock. */
int ceph_zap_memory_authenticated_unlocked(const struct ceph_zap_claim *);
#ifdef __cplusplus
}
#endif
#endif
