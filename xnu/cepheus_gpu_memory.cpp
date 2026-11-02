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
#include <IOKit/IOLocks.h>
#include <mach/mach_types.h>
#include <sys/systm.h>
#include "cepheus_gpu_memory.h"
#include "cepheus_gpu_memory_state.h"

struct ceph_gpu_buffer {
    IOBufferMemoryDescriptor *memory;
    void *cpu;
    uint64_t pa;
    size_t bytes;
    bool prepared,published;
};

/* One-shot selftest counter and bounded outstanding buffer budget. */
static unsigned ceph_memory_once,ceph_memory_pages;
static IOLock *ceph_pool_lock;
static struct {struct ceph_mem_slot state;struct ceph_gpu_buffer buffer;} ceph_pool[16];

static IOLock *
ceph_gpu_pool_lock(void)
{
    IOLock *lock=__atomic_load_n(&ceph_pool_lock,__ATOMIC_ACQUIRE);
    if(lock)return lock;
    IOLock *candidate=IOLockAlloc();
    if(!candidate)return NULL;
    IOLock *expected=NULL;
    if(!__atomic_compare_exchange_n(&ceph_pool_lock,&expected,candidate,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) {
        IOLockFree(candidate);return expected;
    }
    return candidate;
}

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

/* Called with ceph_pool_lock held. */
static int
ceph_gpu_pool_index(uint64_t handle)
{
    uint32_t slot=(uint32_t)handle;
    if(!slot || slot>16 || !ceph_mem_matches(&ceph_pool[slot-1].state,slot-1,handle))return -1;
    return (int)slot-1;
}

extern "C" int
cepheus_gpu_memory_alloc(size_t bytes,uint64_t *handle)
{
    if(!handle || !bytes || bytes>256*1024 || (bytes&4095u))return -1;
    *handle=0;IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int index=-1;
    for(unsigned i=0;i<16;++i)if(!ceph_mem_reserve(&ceph_pool[i].state)){index=(int)i;break;}
    IOLockUnlock(lock);if(index<0)return -1;
    auto *slot=&ceph_pool[index];
    int e=ceph_gpu_buffer_alloc_cpu(&slot->buffer,bytes);
    IOLockLock(lock);
    (void)ceph_mem_finish_allocation(&slot->state,!e,slot->buffer.published);
    if(!e)*handle=ceph_mem_handle((unsigned)index,slot->state.generation);
    IOLockUnlock(lock);return e;
}

extern "C" int
cepheus_gpu_memory_view_get(uint64_t handle,struct ceph_gpu_memory_view *view)
{
    if(!view)return -1;
    *view={};IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int index=ceph_gpu_pool_index(handle),e=-1;
    if(index>=0 && !ceph_mem_view_get(&ceph_pool[index].state)) {
        const auto *b=&ceph_pool[index].buffer;*view={b->cpu,b->pa,b->bytes};e=0;
    }
    IOLockUnlock(lock);return e;
}

extern "C" int
cepheus_gpu_memory_view_put(uint64_t handle)
{
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int index=ceph_gpu_pool_index(handle);
    int e=index<0?-1:ceph_mem_view_put(&ceph_pool[index].state);
    IOLockUnlock(lock);return e;
}

extern "C" int
cepheus_gpu_memory_publish(uint64_t handle)
{
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int index=ceph_gpu_pool_index(handle),e=-1;
    if(index>=0 && !ceph_mem_publish(&ceph_pool[index].state)) {
        /* Finish NC writes before any device address is installed/doorbelled. */
        __asm__ volatile("dsb sy" ::: "memory");
        ceph_pool[index].buffer.published=true;e=0;
    }
    IOLockUnlock(lock);return e;
}

extern "C" int
cepheus_gpu_memory_release_cpu(uint64_t handle)
{
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int index=ceph_gpu_pool_index(handle);
    if(index<0 || ceph_mem_release_begin(&ceph_pool[index].state)) {IOLockUnlock(lock);return -1;}
    IOLockUnlock(lock);
    auto *slot=&ceph_pool[index];ceph_gpu_buffer_dispose_cpu(&slot->buffer);
    IOLockLock(lock);
    int e=slot->buffer.published?-1:0;
    (void)ceph_mem_finish_allocation(&slot->state,0,slot->buffer.published);
    IOLockUnlock(lock);return e;
}

extern "C" void
cepheus_gpu_memory_selftest(void)
{
    unsigned expected=0;
    if (!__atomic_compare_exchange_n(&ceph_memory_once,&expected,1u,false,
        __ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) return;
    uint64_t handle=0;struct ceph_gpu_memory_view b={};
    if (cepheus_gpu_memory_alloc(5*4096,&handle)) {
        printf("CEPHEUS GPU MEM: CPU-only allocation test failed (no GPU access)\n");
        return;
    }
    if(cepheus_gpu_memory_view_get(handle,&b)) {
        (void)cepheus_gpu_memory_release_cpu(handle);
        printf("CEPHEUS GPU MEM: CPU view failed\n");return;
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
    (void)cepheus_gpu_memory_view_put(handle);
    int released=cepheus_gpu_memory_release_cpu(handle);
    printf("CEPHEUS GPU MEM: CPU-only release=%d stale_release=%d\n",released,
        cepheus_gpu_memory_release_cpu(handle));
    printf("CEPHEUS GPU MEM: CPU-only test retained_pages=%u\n",
        __atomic_load_n(&ceph_memory_pages,__ATOMIC_ACQUIRE));
}
#endif
