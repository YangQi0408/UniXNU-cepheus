/* Private kernel CPU staging workspace, not a PAS allocator or user API.
 * Owns one sealed MDT/image pair under an independent 1MiB+16KiB budget.
 * MDT uses Device-nGnRnE; image uses Normal-NC and 1MiB PA alignment.
 * Effective CPU translation/attributes are checked before the first access.
 * This does NOT prove secure PA reachability, XPU/cache compatibility,
 * crypto ownership or authentication. These public CPU APIs never publish.
 * Callers must not hand these PAs to PAS. The separate private PAS engine must
 * acquire a real access/cache/ownership contract before exposing its own claim;
 * no resource backend or GPU startup hook is installed.
 */
#ifndef CEPHEUS_ZAP_MEMORY_H
#define CEPHEUS_ZAP_MEMORY_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct ceph_zap_memory_view {
    const volatile uint8_t *mdt;
    const uint8_t *image;
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
/* Explicit single CPU-only preflight: same allocator and mapping validation,
 * bounded patterns, cleanup and retained budget report. No firmware or PAS. */
int cepheus_zap_memory_selftest(void);
#ifdef __cplusplus
}
#endif
#endif
