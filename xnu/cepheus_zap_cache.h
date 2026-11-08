/* CPU translation checks for privately owned ZAP staging pages only.
 * PAR_EL1 describes a stage1 EL1 translation, not secure-world reachability.
 * Armv8 PAR F=bit0, PA[47:12], ATTR[63:56]; SM8150 uses <=40-bit PA.
 */
#ifndef CEPHEUS_ZAP_CACHE_H
#define CEPHEUS_ZAP_CACHE_H
#include <stddef.h>
#include <stdint.h>
static inline int ceph_zap_translation_matches(uint64_t par,uintptr_t va,uint64_t pa,unsigned attr)
{
    return !(par&1) && pa && pa<(1ULL<<40) && attr<=255 &&
        (par>>56)==attr && ((par&0x0000fffffffff000ULL)|(va&4095u))==pa;
}
#if defined(__aarch64__) || defined(__arm64__)
static inline uint64_t ceph_zap_translate_write(uintptr_t va)
{
    uint64_t flags,par;
    /* Same IRQ/FIQ masking as XNU mmu_kvtop. No call or scheduling point
     * between AT and PAR; ISB completes translation before the result read. */
    __asm__ volatile("mrs %0, daif\n\tmsr daifset, #3\n\tat s1e1w, %2\n\t"
        "isb\n\tmrs %1, par_el1\n\tmsr daif, %0"
        : "=&r"(flags),"=&r"(par) : "r"(va) : "memory");
    return par;
}
#elif defined(CEPHEUS_GPU_MEMORY_HOST_TEST) && !defined(KERNEL)
/* Test-double translation service; never present in a kernel build. */
uint64_t ceph_zap_translate_write(uintptr_t);
#else
#error "ZAP translation requires arm64 or explicit host test"
#endif
/* Force byte stores for Device-nGnRnE destinations. Ordinary memcpy/bzero
 * may use vector or unaligned accesses. The source remains normal memory. */
static inline void ceph_zap_device_zero(volatile uint8_t *dst,size_t bytes)
{for(size_t i=0;i<bytes;++i)dst[i]=0;}
#endif
