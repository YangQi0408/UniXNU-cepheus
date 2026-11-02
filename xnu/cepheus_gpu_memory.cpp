/* XNU-owned GMU backing allocation/cache primitives. No SMMU/GPU MMIO here.
 * Default off: exported selftest only runs under cephmem=1. It allocates,
 * checks actual PA contiguity and CPU visibility, then releases CPU-only memory.
 * Device publication latches retention; there is deliberately no bool-based
 * "GPU done" API to release a still-live DMA target. A future verified stop/unmap
 * backend must supply the retirement path before this is used for submission.
 */
#include <pexpert/arm64/board_config.h>
#if defined(SWEET)
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <mach/mach_types.h>
#include <sys/systm.h>
#include "cepheus_gpu_memory.h"

struct ceph_gpu_buffer {
    IOBufferMemoryDescriptor *memory;
    void *cpu;
    uint64_t pa;
    size_t bytes;
    bool prepared,published;
};

/* One-shot selftest counter and bounded outstanding buffer budget. */
static unsigned ceph_memory_once,ceph_memory_pages;

static void
ceph_gpu_buffer_dispose_cpu(struct ceph_gpu_buffer *b)
{
    if (b->published) return; /* no speculative free after possible DMA */
    if (b->prepared && b->memory->complete(kIODirectionInOut)!=kIOReturnSuccess) {
        b->published=true; /* uncertain completion: retain allocation/accounting */
        printf("CEPHEUS GPU MEM: complete failed; allocation retained\n");
        return;
    }
    if (b->memory) b->memory->release();
    __atomic_fetch_sub(&ceph_memory_pages,(unsigned)(b->bytes/4096),__ATOMIC_RELEASE);
    b->memory=NULL;b->cpu=NULL;b->pa=0;b->bytes=0;b->prepared=false;
}

static int
ceph_gpu_buffer_alloc_cpu(struct ceph_gpu_buffer *b,size_t bytes)
{
    if (!b || b->memory || !bytes || bytes>256*1024 || (bytes&4095u)) return -1;
    unsigned pages=(unsigned)(bytes/4096),used=__atomic_load_n(&ceph_memory_pages,__ATOMIC_ACQUIRE);
    do {
        if (used>128 || pages>128-used) return -1;
    } while (!__atomic_compare_exchange_n(&ceph_memory_pages,&used,used+pages,false,
        __ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE));
    b->bytes=bytes;
    /* Mask encodes 40-bit host PA and 4KiB alignment. MapperNone prevents the
     * platform mapper silently returning someone else's IOVA as a CPU PA.
     * WriteCombine is Normal-NC on arm64; InhibitCache is Device memory and is
     * deliberately NOT used for DRAM/shared HFI buffers. */
    b->memory=IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
        kIODirectionInOut|kIOMemoryHostPhysicallyContiguous|kIOMemoryMapperNone|
        kIOMapWriteCombineCache,bytes,0x000000fffffff000ULL);
    if (!b->memory) {ceph_gpu_buffer_dispose_cpu(b);return -1;}
    if (b->memory->prepare(kIODirectionInOut)!=kIOReturnSuccess) {
        ceph_gpu_buffer_dispose_cpu(b);return -1;
    }
    b->prepared=true;
    IOByteCount length=0;
    b->pa=b->memory->getPhysicalSegment(0,&length,kIOMemoryMapperNone);
    b->cpu=b->memory->getBytesNoCopy();
    if (!b->cpu || !b->pa || ((b->pa|(uintptr_t)b->cpu)&4095u) || length<bytes ||
        b->pa>=(1ULL<<40) || bytes>(1ULL<<40)-b->pa || b->memory->getLength()!=bytes) {
        ceph_gpu_buffer_dispose_cpu(b);return -1;
    }
    bzero(b->cpu,bytes);
    __asm__ volatile("dsb sy" ::: "memory");
    return 0;
}

extern "C" void
cepheus_gpu_memory_selftest(void)
{
    unsigned expected=0;
    if (!__atomic_compare_exchange_n(&ceph_memory_once,&expected,1u,false,
        __ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) return;
    struct ceph_gpu_buffer b={};
    if (ceph_gpu_buffer_alloc_cpu(&b,5*4096)) {
        printf("CEPHEUS GPU MEM: CPU-only allocation test failed (no GPU access)\n");
        return;
    }
    volatile uint32_t *words=(volatile uint32_t *)b.cpu;
    unsigned count=(unsigned)(b.bytes/sizeof(uint32_t));
    for (unsigned i=0;i<count;++i) words[i]=0xa6400000u^i;
    __asm__ volatile("dsb sy" ::: "memory");
    bool okay=true;
    for (unsigned i=0;i<count;++i) if (words[i]!=(0xa6400000u^i)) {okay=false;break;}
    printf("CEPHEUS GPU MEM: CPU-only NC contiguous bytes=%lu pa=%#llx pattern=%s; no DMA/SMMU\n",
        (unsigned long)b.bytes,b.pa,okay?"PASS":"FAIL");
    bzero(b.cpu,b.bytes);__asm__ volatile("dsb sy" ::: "memory");
    ceph_gpu_buffer_dispose_cpu(&b);
    printf("CEPHEUS GPU MEM: CPU-only test retained_pages=%u\n",
        __atomic_load_n(&ceph_memory_pages,__ATOMIC_ACQUIRE));
}
#endif
