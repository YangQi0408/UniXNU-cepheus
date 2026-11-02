#ifndef CEPHEUS_GPU_MEMORY_H
#define CEPHEUS_GPU_MEMORY_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Thread context, opt-in only. Allocates/tests/frees private DRAM, no GPU DMA. */
void cepheus_gpu_memory_selftest(void);
/* Internal kernel API, not an IOUserClient dispatch. Views are CPU leases;
 * return them before publish/release. No raw userspace pointers or DMA commands.
 * Published buffers intentionally cannot be freed until a verified retirement
 * backend exists. Synchronize CPU access yourself; cache type is Normal-NC. */
struct ceph_gpu_memory_view {void *cpu;uint64_t pa;size_t bytes;};
int cepheus_gpu_memory_alloc(size_t bytes,uint64_t *handle);
int cepheus_gpu_memory_view_get(uint64_t handle,struct ceph_gpu_memory_view *view);
int cepheus_gpu_memory_view_put(uint64_t handle);
int cepheus_gpu_memory_publish(uint64_t handle);
int cepheus_gpu_memory_release_cpu(uint64_t handle);
#ifdef __cplusplus
}
#endif
#endif
