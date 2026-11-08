/* Private kernel CPU staging workspace, not a PAS allocator or user API.
 * Owns one sealed MDT/image pair under an independent 1MiB+16KiB budget.
 * Both are Normal-NC mappings of managed contiguous DRAM; image PA is 1MiB
 * aligned. This does NOT prove secure PA reachability, XPU/cache compatibility,
 * crypto ownership or authentication. No publish/SMC/GPU startup path exists.
 * Callers must not hand these PAs to PAS. A future secure backend must establish
 * its own access/cache/ownership contract and retain memory across ambiguity.
 */
#ifndef CEPHEUS_ZAP_MEMORY_H
#define CEPHEUS_ZAP_MEMORY_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct ceph_zap_memory_view {
    const uint8_t *mdt,*image;
    uint64_t mdt_pa,image_pa;
    size_t mdt_capacity,image_capacity;
    uint32_t mdt_bytes,logical_bytes,entry_offset;
};
/* Thread context. Source must be an existing digest-verified ZAP snapshot.
 * Holds its readonly lease until both copies finish. Only one pair may exist;
 * failed IOKit completion permanently retains backing and blocks replacement.
 * Workspace handles cannot be used with the generic GPU pool APIs. */
int cepheus_zap_memory_prepare(uint64_t snapshot,uint64_t *handle);
int cepheus_zap_memory_get(uint64_t handle,struct ceph_zap_memory_view *view);
int cepheus_zap_memory_put(uint64_t handle);
/* All readonly leases must be returned first. CPU-only release, no assertion
 * of secure shutdown or device quiescence. Never use after hardware exposure. */
int cepheus_zap_memory_release_cpu(uint64_t handle);
#ifdef __cplusplus
}
#endif
#endif
