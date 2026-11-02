#ifndef CEPHEUS_GPU_MEMORY_H
#define CEPHEUS_GPU_MEMORY_H
#ifdef __cplusplus
extern "C" {
#endif
/* Thread context, opt-in only. Allocates/tests/frees private DRAM, no GPU DMA. */
void cepheus_gpu_memory_selftest(void);
#ifdef __cplusplus
}
#endif
#endif
