/* XNU-owned GPU backing and separate ZAP CPU staging. No SMMU/GPU MMIO here.
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
#include <mach/vm_param.h>
#if defined(KERNEL) || defined(CEPHEUS_GPU_MEMORY_HOST_TEST)
extern "C" {
#include <vm/memory_types.h>
/* IOKitServer.h hides this bridge behind MACH_KERNEL_PRIVATE. Same ABI as
 * osfmk/device/iokit_rpc.c, which calls pmap_cache_attributes(pa>>PAGE_SHIFT). */
unsigned int IODefaultCacheBits(addr64_t pa);
/* Real pmap bounds used by pa_valid().
 * Without this check a mask-derived allocation may land below gPhysBase and
 * panic inside ml_static_vtop/phystokv_range.
 * Declared with the underlying 64-bit integer type: pmap_paddr_t is not visible
 * in this translation unit (it lives in the pmap-private headers, which this
 * file must not pull in), and the symbols really are 64-bit addresses. */
extern uint64_t vm_first_phys;
extern uint64_t vm_last_phys;
}
#else
#error "GPU memory requires XNU bounds or the explicit host IOKit test double"
#endif
#include <sys/systm.h>
#include "cepheus_gpu_memory.h"
#include "cepheus_gpu_memory_state.h"
#include "cepheus_gpu_firmware.h"
#include "cepheus_gpu_memory_private.h"
#include "cepheus_cp_memory_private.h"
#include "cepheus_gpu_va.h"
#include "cepheus_gmu_upload.h"
#include "cepheus_zap_image.h"
#include "cepheus_zap_memory.h"
#include "cepheus_zap_memory_private.h"
#include "cepheus_zap_cache.h"
#if defined(HAS_UCNORMAL_MEM) && HAS_UCNORMAL_MEM
#error "SWEET ZAP MDT requires VM_WIMG_RT to select Device-nGnRnE"
#endif
static_assert(VM_WIMG_RT==0x17,"Unexpected SWEET real-time memory attribute encoding");

struct ceph_gpu_buffer {
    IOBufferMemoryDescriptor *memory;
    IOMemoryMap *mapping;
    void *cpu;
    uint64_t pa;
    size_t bytes;
    unsigned *budget;
    bool prepared,published,restore_cache;
};

/* One-shot selftest counter and bounded outstanding buffer budget. */
static unsigned ceph_memory_once,ceph_memory_pages;
static unsigned ceph_zap_pages,ceph_zap_test_once;
struct ceph_buffer_policy {
    size_t alignment,maximum;unsigned *pages,limit,cache,wimg,attr;bool translate;
};
static const struct ceph_buffer_policy ceph_gpu_policy={4096,256*1024,&ceph_memory_pages,256,
    kIOMapWriteCombineCache,VM_WIMG_WCOMB,0x44,false};
static const struct ceph_buffer_policy ceph_zap_mdt_policy={4096,16384,&ceph_zap_pages,260,
    kIOMapRealTimeCache,VM_WIMG_RT,0,true};
static const struct ceph_buffer_policy ceph_zap_image_policy={1048576,1048576,&ceph_zap_pages,260,
    kIOMapWriteCombineCache,VM_WIMG_WCOMB,0x44,true};
enum ceph_zap_phase {CE_ZM_NONE,CE_ZM_CLAIMED,CE_ZM_EXPOSED,CE_ZM_COPYING,CE_ZM_IMAGE,CE_ZM_AUTH,CE_ZM_KEEP};
/* Index16 gives the separate workspace a disjoint handle namespace. */
static struct {
    struct ceph_mem_slot state;
    struct ceph_gpu_buffer mdt,image;
    struct ceph_zap_image layout;
    struct ceph_gpu_firmware_view source;
    uint64_t snapshot;
    enum ceph_zap_phase phase;
} ceph_zap;
static IOLock *ceph_pool_lock;
static struct {struct ceph_mem_slot state;struct ceph_gpu_buffer buffer;unsigned firmware_kind;size_t firmware_bytes;uint64_t group;} ceph_pool[16];
static struct ceph_gpu_publication ceph_groups[16];
static uint64_t ceph_group_generation;
static unsigned ceph_pool_terminal;
static unsigned ceph_group_status[16];
static unsigned ceph_group_installing[16];
static unsigned ceph_group_uploading[16];
static unsigned ceph_group_hfi[16]; /* 1 temporary, 2 sealed, 3 retained result */
static unsigned ceph_group_cp[16]; /* 1 sealed, 2 pinned, 3 exposed, 4 installed */
static struct ceph_cp_publication ceph_cp_groups[16];
static unsigned ceph_cp_building[16];
static struct ceph_cp_workspace ceph_cp_build;
static unsigned ceph_cp_build_active;

static void ceph_gpu_memory_barrier(void)
{
#if defined(__aarch64__) || defined(__arm64__)
    __asm__ volatile("dsb sy" ::: "memory");
#elif defined(CEPHEUS_GPU_MEMORY_HOST_TEST) && !defined(KERNEL)
    __atomic_thread_fence(__ATOMIC_SEQ_CST); /* test process only, never XNU */
#else
#error "GPU memory barrier requires arm64 (or explicit non-kernel host test)"
#endif
}

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
    if (!b->bytes || b->published) return; /* no speculative free after possible DMA */
    if(b->restore_cache) {
        /* IOKit changes cache attributes for all aliases through the memory
         * entry. Restore Device pages before returning them to the allocator;
         * never rely on a future ordinary bzero to tolerate Device mappings. */
        IOMemoryMap *normal=b->memory->createMappingInTask(kernel_task,0,
            kIOMapAnywhere|kIOMapUnique|kIOMapCopybackCache|kIOMapPrefault,0,b->bytes);
        bool valid=normal && normal->getLength()==b->bytes &&
            (normal->getMapOptions()&kIOMapCacheMask)==kIOMapCopybackCache && normal->getVirtualAddress();
        for(size_t offset=0;valid && offset<b->bytes;offset+=4096) {
            if(!(offset&((size_t)page_size-1)) &&
                (IODefaultCacheBits(b->pa+offset)&VM_WIMG_MASK)!=VM_WIMG_COPYBACK)valid=false;
            uintptr_t va=(uintptr_t)normal->getVirtualAddress()+offset;
            if(!ceph_zap_translation_matches(ceph_zap_translate_write(va),va,b->pa+offset,0xff))valid=false;
            if(b->cpu) {
                va=(uintptr_t)b->cpu+offset;
                if(!ceph_zap_translation_matches(ceph_zap_translate_write(va),va,b->pa+offset,0xff))valid=false;
            }
        }
        if(normal)normal->release();
        if(!valid) {
            b->published=true;
            printf("CEPHEUS ZAP MEM: cache restore failed; allocation retained\n");return;
        }
        b->restore_cache=false;
        printf("CEPHEUS ZAP MEM: restored pa=%#llx bytes=%#lx attr=0xff\n",
            (unsigned long long)b->pa,(unsigned long)b->bytes);
    }
    if (b->prepared && b->memory->complete(kIODirectionInOut)!=kIOReturnSuccess) {
        b->published=true; /* uncertain completion: retain allocation/accounting */
        printf("CEPHEUS GPU MEM: complete failed; allocation retained\n");
        return;
    }
    if (b->mapping) b->mapping->release();
    if (b->memory) b->memory->release();
    __atomic_fetch_sub(b->budget,(unsigned)(b->bytes/4096),__ATOMIC_RELEASE);
    b->memory=NULL;b->mapping=NULL;b->cpu=NULL;b->pa=0;b->bytes=0;b->prepared=false;
    b->budget=NULL;
}

static int
ceph_gpu_buffer_alloc_cpu(struct ceph_gpu_buffer *b,size_t bytes,const struct ceph_buffer_policy *policy)
{
    if (!b || b->bytes || !bytes || bytes>policy->maximum || (bytes&4095u)) return -1;
    /* SWEET XNU uses 16KiB host pages while GMU tables use 4KiB. Account
     * physical allocation, not the smaller requested length. No odd geometry. */
    size_t host_page=(size_t)page_size;
    if(host_page!=4096 && host_page!=16384)return -1;
    bytes=(bytes+host_page-1)&~(host_page-1);
    uint64_t window_mask=cepheus_gpu_pa_aligned_window_mask(vm_first_phys,vm_last_phys,host_page,policy->alignment);
    if(!window_mask)return -1; /* no fixed-mask fallback for invalid bounds */
    unsigned pages=(unsigned)(bytes/4096),used=__atomic_load_n(policy->pages,__ATOMIC_ACQUIRE);
    do {
        /* GPU and ZAP accounting are independent. Retained allocations keep
         * their charge in the originating budget, including failed complete. */
        if (used>policy->limit || pages>policy->limit-used) return -1;
    } while (!__atomic_compare_exchange_n(policy->pages,&used,used+pages,false,
        __ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE));
    b->bytes=bytes;b->budget=policy->pages;
    printf("CEPHEUS GPU MEM: phase=allocate bytes=%#lx host_page=%#lx managed=[%#llx,%#llx) mask=%#llx\n",
        (unsigned long)bytes,(unsigned long)host_page,(unsigned long long)vm_first_phys,
        (unsigned long long)vm_last_phys,(unsigned long long)window_mask);
    /* Default-cache allocation ONLY. Non-default cache flags cause IOBMD's
     * constructor to create a persistent memory entry and change attributes
     * before it returns, too early for our PA check. Request cache mode only in
     * the explicit map after validation. MapperNone prevents an IOVA being
     * mistaken for a CPU PA. maxPhys is an upper bound, never a lower bound. */
    b->memory=IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
        kIODirectionInOut|kIOMemoryHostPhysicallyContiguous|kIOMemoryMapperNone,
        bytes,window_mask);
    if (!b->memory) {ceph_gpu_buffer_dispose_cpu(b);return -1;}
    printf("CEPHEUS GPU MEM: phase=prepare\n");
    if (b->memory->prepare(kIODirectionInOut)!=kIOReturnSuccess) {
        ceph_gpu_buffer_dispose_cpu(b);return -1;
    }
    b->prepared=true;
    IOByteCount length=0;
    b->pa=b->memory->getPhysicalSegment(0,&length,kIOMemoryMapperNone);
    /* This check MUST run before createMappingInTask(). Remapping the buffer
     * with a different cache mode makes pmap change the page's attributes,
     * which goes pmap_sync_wimg -> pmap_sync_page_attributes_phys ->
     * flush_dcache(pa, PAGE_SIZE, TRUE) -> phystokv_range(pa). phystokv_range()
     * panics for any PA outside [gPhysBase, gPhysBase+real_phys_size), so a
     * buffer the allocator placed below the managed window would take the
     * kernel down during the mapping call itself -- after this point is far too
     * late. maxPhys in the allocation mask is only an upper bound and cannot
     * express this lower bound, so the check has to be here. */
    if(!b->pa || (b->pa&(host_page-1)) || (b->pa&(policy->alignment-1)) ||
        length<bytes || b->memory->getLength()!=bytes ||
        !cepheus_gpu_pa_range_managed(b->pa,bytes,vm_first_phys,vm_last_phys)) {
        printf("CEPHEUS GPU MEM: backing invalid pa=%#llx bytes=%#lx segment=%#lx host_page=%#lx managed=[%#llx,%#llx); refused before mapping\n",
            (unsigned long long)b->pa,(unsigned long)bytes,
            (unsigned long)length,(unsigned long)host_page,
            (unsigned long long)vm_first_phys,(unsigned long long)vm_last_phys);
        ceph_gpu_buffer_dispose_cpu(b);return -1;
    }
    /* Allocation cache flags alone do not establish the attributes of the
     * original getBytesNoCopy VA. Request an explicit kernel map and use
     * ONLY its VA; XNU performs cache-mode/alias transitions while mapping. */
    printf("CEPHEUS GPU MEM: phase=map_cpu pa=%#llx bytes=%#lx wimg=%#x\n",
        (unsigned long long)b->pa,(unsigned long)bytes,policy->wimg);
    b->restore_cache=policy->cache==kIOMapRealTimeCache;
    /* AT does not fault pages in. prepare() supplied the UPL; populate the new
     * mapping before checking translation, without touching Device bytes. */
    b->mapping=b->memory->createMappingInTask(kernel_task,0,
        kIOMapAnywhere|kIOMapUnique|policy->cache|(policy->translate?kIOMapPrefault:0),0,bytes);
    if(!b->mapping || b->mapping->getLength()!=bytes ||
        (b->mapping->getMapOptions()&kIOMapCacheMask)!=policy->cache) {
        ceph_gpu_buffer_dispose_cpu(b);return -1;
    }
    b->cpu=(void *)(uintptr_t)b->mapping->getVirtualAddress();
    if (!b->cpu || !b->pa || ((b->pa|(uintptr_t)b->cpu)&4095u) || length<bytes ||
        b->pa>=(1ULL<<40) || bytes>(1ULL<<40)-b->pa || b->memory->getLength()!=bytes) {
        ceph_gpu_buffer_dispose_cpu(b);return -1;
    }
    /* Attribute options describe the request; independently verify pmap's
     * effective host-page cache attributes before exposing any shared view.
     * Never repair this by forcing attributes on an unowned physical page. */
    for(size_t offset=0;offset<bytes;offset+=host_page) {
        unsigned attrs=IODefaultCacheBits(b->pa+offset)&VM_WIMG_MASK;
        if(attrs!=policy->wimg) {
            printf("CEPHEUS GPU MEM: pmap cache mismatch attrs=%#x expected=%#x; allocation refused\n",attrs,policy->wimg);
            ceph_gpu_buffer_dispose_cpu(b);return -1;
        }
    }
    if(policy->translate) {
        uint64_t first=0;
        for(size_t offset=0;offset<bytes;offset+=4096) {
            uintptr_t va=(uintptr_t)b->cpu+offset;
            uint64_t par=ceph_zap_translate_write(va);
            if(!offset)first=par;
            if(!ceph_zap_translation_matches(par,va,b->pa+offset,policy->attr)) {
                printf("CEPHEUS ZAP MEM: translation refused offset=%#lx par=%#llx expected_pa=%#llx attr=%#x\n",
                    (unsigned long)offset,(unsigned long long)par,(unsigned long long)(b->pa+offset),policy->attr);
                ceph_gpu_buffer_dispose_cpu(b);return -1;
            }
        }
        printf("CEPHEUS ZAP MEM: translated pa=%#llx bytes=%#lx attr=%#x first_par=%#llx pages=%lu\n",
            (unsigned long long)b->pa,(unsigned long)bytes,policy->attr,(unsigned long long)first,(unsigned long)(bytes/4096));
    }
    if(policy->cache==kIOMapRealTimeCache)ceph_zap_device_zero((volatile uint8_t *)b->cpu,bytes);
    else bzero(b->cpu,bytes);
    ceph_gpu_memory_barrier();
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
    if(ceph_pool_terminal) {IOLockUnlock(lock);return -1;}
    for(unsigned i=0;i<16;++i)if(!ceph_mem_reserve(&ceph_pool[i].state)){index=(int)i;break;}
    IOLockUnlock(lock);if(index<0)return -1;
    auto *slot=&ceph_pool[index];
    /* ALLOCATING excludes all handle lookups, so slow IOKit work and slot
     * initialization can run outside the pool mutex without concurrent views. */
    slot->firmware_bytes=0;slot->firmware_kind=0;slot->group=0;
    int e=ceph_gpu_buffer_alloc_cpu(&slot->buffer,bytes,&ceph_gpu_policy);
    IOLockLock(lock);
    /* Allocation may sleep outside this mutex while another thread abandons
     * the GPU session. Recheck before handing out a new usable handle. This
     * allocation was never published, so CPU-only disposal remains safe. */
    if(!e && ceph_pool_terminal) {
        IOLockUnlock(lock);ceph_gpu_buffer_dispose_cpu(&slot->buffer);IOLockLock(lock);e=-1;
    }
    (void)ceph_mem_finish_allocation(&slot->state,!e,slot->buffer.published);
    if(!e)*handle=ceph_mem_handle((unsigned)index,slot->state.generation);
    if(e && slot->buffer.published)printf("CEPHEUS GPU MEM: retained slot=%u bytes=%lu generation=%u\n",
        (unsigned)index,(unsigned long)slot->buffer.bytes,slot->state.generation);
    IOLockUnlock(lock);return e;
}

extern "C" int
cepheus_gpu_firmware_snapshot(unsigned kind,const uint8_t *data,size_t bytes,uint64_t *handle)
{
    if(!handle)return -1;
    *handle=0;
    if(!data || !bytes || bytes!=ceph_firmware_size(kind))return -1;
    uint64_t id=0;
    if(cepheus_gpu_memory_alloc((bytes+4095u)&~(size_t)4095u,&id))return -1;
    struct ceph_gpu_memory_view view={};
    if(cepheus_gpu_memory_view_get(id,&view)) { (void)cepheus_gpu_memory_release_cpu(id);return -1; }
    memcpy(view.cpu,data,bytes);
    ceph_gpu_memory_barrier();
    int e=ceph_firmware_verify((enum ceph_firmware_kind)kind,(const uint8_t *)view.cpu,bytes);
    (void)cepheus_gpu_memory_view_put(id);
    if(e) { (void)cepheus_gpu_memory_release_cpu(id);return -1; }
    IOLock *lock=ceph_gpu_pool_lock(); /* already allocated by memory_alloc */
    IOLockLock(lock);int index=ceph_gpu_pool_index(id);
    e=index<0?-1:ceph_mem_seal(&ceph_pool[index].state);
    if(!e) {ceph_pool[index].firmware_bytes=bytes;ceph_pool[index].firmware_kind=kind;*handle=id;}
    IOLockUnlock(lock);
    if(e)(void)cepheus_gpu_memory_release_cpu(id);
    return e;
}

extern "C" int
cepheus_gpu_firmware_get(uint64_t handle,unsigned expected_kind,struct ceph_gpu_firmware_view *view)
{
    if(!view || !ceph_firmware_size(expected_kind))return -1;
    *view={};IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int index=ceph_gpu_pool_index(handle),e=-1;
    if(index>=0 && ceph_pool[index].firmware_bytes && ceph_pool[index].firmware_kind==expected_kind &&
        !ceph_mem_readonly_get(&ceph_pool[index].state)) {
        *view={(const uint8_t *)ceph_pool[index].buffer.cpu,ceph_pool[index].firmware_bytes,expected_kind};e=0;
    }
    IOLockUnlock(lock);return e;
}

extern "C" int
cepheus_gpu_memory_view_get(uint64_t handle,struct ceph_gpu_memory_view *view)
{
    if(!view)return -1;
    *view={};IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int index=ceph_gpu_pool_index(handle),e=-1;
    if(index>=0 && !ceph_cp_building[index] && !ceph_mem_view_get(&ceph_pool[index].state)) {
        const auto *b=&ceph_pool[index].buffer;*view={b->cpu,b->pa,b->bytes};e=0;
    }
    IOLockUnlock(lock);return e;
}

extern "C" int
cepheus_gpu_memory_view_put(uint64_t handle)
{
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int index=ceph_gpu_pool_index(handle);
    int e=index<0 || ceph_cp_building[index]?-1:ceph_mem_view_put(&ceph_pool[index].state);
    IOLockUnlock(lock);return e;
}

extern "C" int
cepheus_gpu_memory_publish(uint64_t handle)
{
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int index=ceph_gpu_pool_index(handle),e=-1;
    if(!ceph_pool_terminal && index>=0 && !ceph_cp_building[index] && !ceph_mem_publish(&ceph_pool[index].state)) {
        /* Finish NC writes before any device address is installed/doorbelled. */
        ceph_gpu_memory_barrier();
        ceph_pool[index].buffer.published=true;e=0;
    }
    IOLockUnlock(lock);return e;
}

extern "C" int
cepheus_gpu_memory_release_cpu(uint64_t handle)
{
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int index=ceph_gpu_pool_index(handle);
    if(index<0 || ceph_cp_building[index] || ceph_mem_release_begin(&ceph_pool[index].state)) {IOLockUnlock(lock);return -1;}
    IOLockUnlock(lock);
    auto *slot=&ceph_pool[index];ceph_gpu_buffer_dispose_cpu(&slot->buffer);
    IOLockLock(lock);
    int e=slot->buffer.published?-1:0;
    (void)ceph_mem_finish_allocation(&slot->state,0,slot->buffer.published);
    if(e)printf("CEPHEUS GPU MEM: retained slot=%u bytes=%lu generation=%u\n",
        (unsigned)index,(unsigned long)slot->buffer.bytes,slot->state.generation);
    IOLockUnlock(lock);return e;
}

static void ceph_zap_dispose_cpu(void)
{
    ceph_gpu_buffer_dispose_cpu(&ceph_zap.image);
    ceph_gpu_buffer_dispose_cpu(&ceph_zap.mdt);
}

static int ceph_zap_alloc_cpu(void)
{
    int e=ceph_gpu_buffer_alloc_cpu(&ceph_zap.mdt,8192,&ceph_zap_mdt_policy);
    if(!e)e=ceph_gpu_buffer_alloc_cpu(&ceph_zap.image,1048576,&ceph_zap_image_policy);
    if(!e && ceph_zap_overlap(ceph_zap.mdt.pa,ceph_zap.mdt.bytes,ceph_zap.image.pa,ceph_zap.image.bytes))e=-1;
    return e;
}

extern "C" int
cepheus_zap_memory_prepare(uint64_t snapshot,uint64_t *handle)
{
    if(!handle)return -1;
    *handle=0;
    struct ceph_gpu_firmware_view source={};
    if(cepheus_gpu_firmware_get(snapshot,CE_FW_A640_ZAP,&source))return -1;
    struct ceph_zap_image layout={};
    int e=ceph_zap_parse(source.data,source.bytes,&layout);
    /* Fixed verified A640 artifact only; never grow the budget from ELF data. */
    if(e || layout.mdt_bytes!=6860 || layout.logical_bytes!=4096 || layout.allocation_bytes!=1048576) {
        (void)cepheus_gpu_memory_view_put(snapshot);return -1;
    }
    IOLock *lock=ceph_gpu_pool_lock(); /* firmware_get already created it */
    IOLockLock(lock);
    e=ceph_pool_terminal?-1:ceph_mem_reserve(&ceph_zap.state);
    IOLockUnlock(lock);
    if(e) { (void)cepheus_gpu_memory_view_put(snapshot);return -1; }
    /* ALLOCATING excludes get/release while IOKit may sleep. The sealed source
     * stays leased throughout both allocations and copies. No PA is published. */
    e=ceph_zap_alloc_cpu();
    if(!e)e=ceph_zap_stage(source.data,source.bytes,(uint8_t *)ceph_zap.mdt.cpu,ceph_zap.mdt.bytes,
        (uint8_t *)ceph_zap.image.cpu,ceph_zap.image.bytes,&ceph_zap.layout);
    if(!e)ceph_gpu_memory_barrier();
    if(cepheus_gpu_memory_view_put(snapshot))e=-1;
    IOLockLock(lock);
    if(ceph_pool_terminal)e=-1;
    if(e) {
        IOLockUnlock(lock);ceph_zap_dispose_cpu();IOLockLock(lock);
    }
    bool retained=ceph_zap.mdt.published || ceph_zap.image.published;
    (void)ceph_mem_finish_allocation(&ceph_zap.state,!e,retained);
    if(!e) {
        (void)ceph_mem_seal(&ceph_zap.state);
        *handle=ceph_mem_handle(16,ceph_zap.state.generation);
    }
    if(retained)printf("CEPHEUS ZAP MEM: CPU cleanup uncertain; workspace retained\n");
    IOLockUnlock(lock);return e;
}

extern "C" int
cepheus_zap_memory_get(uint64_t handle,struct ceph_zap_memory_view *view)
{
    if(!view)return -1;
    *view={};IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int e=-1;
    if(ceph_mem_matches(&ceph_zap.state,16,handle) && !ceph_mem_readonly_get(&ceph_zap.state)) {
        *view={(const uint8_t *)ceph_zap.mdt.cpu,(const uint8_t *)ceph_zap.image.cpu,
            ceph_zap.mdt.pa,ceph_zap.image.pa,ceph_zap.mdt.bytes,ceph_zap.image.bytes,
            ceph_zap.layout.mdt_bytes,ceph_zap.layout.logical_bytes,ceph_zap.layout.entry_offset};e=0;
    }
    IOLockUnlock(lock);return e;
}

extern "C" int
cepheus_zap_memory_put(uint64_t handle)
{
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);
    int e=ceph_mem_matches(&ceph_zap.state,16,handle)?ceph_mem_view_put(&ceph_zap.state):-1;
    IOLockUnlock(lock);return e;
}

extern "C" int
cepheus_zap_memory_release_cpu(uint64_t handle)
{
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);
    if(!ceph_mem_matches(&ceph_zap.state,16,handle) || ceph_mem_release_begin(&ceph_zap.state)) {
        IOLockUnlock(lock);return -1;
    }
    IOLockUnlock(lock);ceph_zap_dispose_cpu();IOLockLock(lock);
    bool retained=ceph_zap.mdt.published || ceph_zap.image.published;
    (void)ceph_mem_finish_allocation(&ceph_zap.state,0,retained);
    if(retained)printf("CEPHEUS ZAP MEM: CPU cleanup uncertain; workspace retained\n");
    IOLockUnlock(lock);return retained?-1:0;
}

extern "C" int
ceph_zap_memory_claim(uint64_t snapshot,struct ceph_zap_claim *claim)
{
    if(!claim)return -1;
    *claim={};struct ceph_gpu_firmware_view source={};
    if(cepheus_gpu_firmware_get(snapshot,CE_FW_A640_ZAP,&source))return -1;
    uint64_t handle=0;
    int e=cepheus_zap_memory_prepare(snapshot,&handle);
    IOLock *lock=ceph_gpu_pool_lock();
    if(!e) {
        IOLockLock(lock);
        if(ceph_pool_terminal || !ceph_mem_matches(&ceph_zap.state,16,handle) ||
            ceph_zap.state.state!=CE_MEM_SEALED || ceph_zap.state.views)e=-1;
        else {
            ceph_zap.state.state=CE_MEM_SHARED;ceph_zap.phase=CE_ZM_CLAIMED;
            ceph_zap.snapshot=snapshot;ceph_zap.source=source;
            claim->handle=handle;
            claim->memory={(const uint8_t *)ceph_zap.mdt.cpu,(const uint8_t *)ceph_zap.image.cpu,
                ceph_zap.mdt.pa,ceph_zap.image.pa,ceph_zap.mdt.bytes,ceph_zap.image.bytes,
                ceph_zap.layout.mdt_bytes,ceph_zap.layout.logical_bytes,ceph_zap.layout.entry_offset};
        }
        IOLockUnlock(lock);
    }
    if(e) {
        if(handle)(void)cepheus_zap_memory_release_cpu(handle);
        (void)cepheus_gpu_memory_view_put(snapshot);
    }
    return e;
}

extern "C" int
ceph_zap_memory_expose(uint64_t handle)
{
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int e=-1;
    if(!ceph_pool_terminal && ceph_mem_matches(&ceph_zap.state,16,handle) &&
        ceph_zap.state.state==CE_MEM_SHARED && ceph_zap.phase==CE_ZM_CLAIMED) {
        ceph_zap.mdt.published=ceph_zap.image.published=true;
        ceph_zap.phase=CE_ZM_EXPOSED;ceph_gpu_memory_barrier();e=0;
    }
    IOLockUnlock(lock);return e;
}

extern "C" int
ceph_zap_memory_reload(uint64_t handle)
{
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);
    if(ceph_pool_terminal || !ceph_mem_matches(&ceph_zap.state,16,handle) ||
        ceph_zap.state.state!=CE_MEM_SHARED || ceph_zap.phase!=CE_ZM_EXPOSED || !ceph_zap.snapshot) {
        IOLockUnlock(lock);return -1;
    }
    ceph_zap.phase=CE_ZM_COPYING;IOLockUnlock(lock);
    struct ceph_zap_image layout={};
    int e=ceph_zap_stage_image(ceph_zap.source.data,ceph_zap.source.bytes,
        (uint8_t *)ceph_zap.image.cpu,ceph_zap.image.bytes,&layout);
    ceph_gpu_memory_barrier();
    IOLockLock(lock);
    if(ceph_pool_terminal)e=-1;
    ceph_zap.phase=e?CE_ZM_EXPOSED:CE_ZM_IMAGE;
    IOLockUnlock(lock);return e;
}

extern "C" int
ceph_zap_memory_finish(uint64_t handle,enum ceph_zap_memory_result result)
{
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);
    if(!ceph_mem_matches(&ceph_zap.state,16,handle) || ceph_zap.state.state!=CE_MEM_SHARED ||
        ceph_zap.phase==CE_ZM_COPYING || result<CE_ZAP_CPU_ABORT || result>CE_ZAP_STOPPED ||
        (result==CE_ZAP_CPU_ABORT && ceph_zap.phase!=CE_ZM_CLAIMED) ||
        (result==CE_ZAP_AUTHENTICATED && ceph_zap.phase!=CE_ZM_IMAGE) ||
        (result==CE_ZAP_STOPPED && ceph_zap.phase!=CE_ZM_AUTH)) {
        IOLockUnlock(lock);return -1;
    }
    int e=0;
    if(result==CE_ZAP_AUTHENTICATED && ceph_pool_terminal)e=-1;
    if(ceph_zap.snapshot) {
        int index=ceph_gpu_pool_index(ceph_zap.snapshot);
        if(index<0 || ceph_mem_view_put(&ceph_pool[index].state))e=-1;
        else {ceph_zap.snapshot=0;ceph_zap.source={};}
    }
    if(e || result==CE_ZAP_KEEP) {
        ceph_zap.mdt.published=ceph_zap.image.published=true;
        ceph_zap.phase=CE_ZM_KEEP;ceph_zap.state.state=CE_MEM_QUARANTINED;
        IOLockUnlock(lock);return e;
    }
    if(result==CE_ZAP_AUTHENTICATED) {
        ceph_zap.phase=CE_ZM_AUTH;IOLockUnlock(lock);return 0;
    }
    /* Only successful shutdown or a never-exposed claim can reach disposal.
     * Hold ALLOCATING while complete() sleeps to exclude every public API. */
    ceph_zap.mdt.published=ceph_zap.image.published=false;
    ceph_zap.state.state=CE_MEM_ALLOCATING;
    IOLockUnlock(lock);ceph_zap_dispose_cpu();IOLockLock(lock);
    bool retained=ceph_zap.mdt.published || ceph_zap.image.published;
    (void)ceph_mem_finish_allocation(&ceph_zap.state,0,retained);
    ceph_zap.phase=retained?CE_ZM_KEEP:CE_ZM_NONE;
    IOLockUnlock(lock);return retained?-1:0;
}
extern "C" int ceph_zap_memory_authenticated(const struct ceph_zap_claim *claim)
{
    if(!claim)return -1;
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int e=-1;
    const auto *v=&claim->memory;
    if(!ceph_pool_terminal && ceph_mem_matches(&ceph_zap.state,16,claim->handle) &&
        ceph_zap.state.state==CE_MEM_SHARED && ceph_zap.phase==CE_ZM_AUTH &&
        !ceph_zap.state.views && !ceph_zap.snapshot &&
        ceph_zap.mdt.published && ceph_zap.image.published &&
        ceph_zap.mdt.prepared && ceph_zap.image.prepared &&
        v->mdt==ceph_zap.mdt.cpu && v->image==ceph_zap.image.cpu &&
        v->mdt_pa==ceph_zap.mdt.pa && v->image_pa==ceph_zap.image.pa &&
        v->mdt_capacity==ceph_zap.mdt.bytes && v->image_capacity==ceph_zap.image.bytes &&
        v->mdt_bytes==ceph_zap.layout.mdt_bytes && v->logical_bytes==ceph_zap.layout.logical_bytes &&
        v->entry_offset==ceph_zap.layout.entry_offset)e=0;
    IOLockUnlock(lock);return e;
}

extern "C" int
cepheus_zap_memory_selftest(void)
{
    unsigned expected=0;
    if(!__atomic_compare_exchange_n(&ceph_zap_test_once,&expected,1u,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return -1;
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);
    int e=ceph_pool_terminal?-1:ceph_mem_reserve(&ceph_zap.state);
    IOLockUnlock(lock);
    if(e) {printf("CEPHEUS ZAP TEST: refused occupied or terminal workspace\n");return -1;}
    printf("CEPHEUS ZAP TEST: begin version=1 cpu_only=1\n");
    e=ceph_zap_alloc_cpu();
    if(!e) {
        volatile uint8_t *mdt=(volatile uint8_t *)ceph_zap.mdt.cpu;
        volatile uint32_t *image=(volatile uint32_t *)ceph_zap.image.cpu;
        for(size_t i=0;i<ceph_zap.mdt.bytes;++i)mdt[i]=(uint8_t)(0x5au^i);
        for(size_t i=0;i<ceph_zap.image.bytes/4;++i)image[i]=0xa6400000u^(uint32_t)i;
        ceph_gpu_memory_barrier();
        for(size_t i=0;i<ceph_zap.mdt.bytes;++i)if(mdt[i]!=(uint8_t)(0x5au^i))e=-1;
        for(size_t i=0;i<ceph_zap.image.bytes/4;++i)if(image[i]!=(0xa6400000u^(uint32_t)i))e=-1;
        printf("CEPHEUS ZAP TEST: pattern=%s mdt_bytes=%lu image_bytes=%lu logical_bytes=4096\n",
            e?"FAIL":"PASS",(unsigned long)ceph_zap.mdt.bytes,(unsigned long)ceph_zap.image.bytes);
        ceph_zap_device_zero(mdt,ceph_zap.mdt.bytes);
        bzero(ceph_zap.image.cpu,ceph_zap.image.bytes);ceph_gpu_memory_barrier();
    }
    ceph_zap_dispose_cpu();
    IOLockLock(lock);
    bool retained=ceph_zap.mdt.published || ceph_zap.image.published;
    if(retained || ceph_pool_terminal)e=-1;
    (void)ceph_mem_finish_allocation(&ceph_zap.state,0,retained);
    unsigned pages=__atomic_load_n(&ceph_zap_pages,__ATOMIC_ACQUIRE);
    if(pages)e=-1;
    printf("CEPHEUS ZAP TEST: end result=%d retained_pages=%u cpu_only=1\n",e,pages);
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
    ceph_gpu_memory_barrier();
    bool okay=true;
    for (unsigned i=0;i<count;++i) if (words[i]!=(0xa6400000u^i)) {okay=false;break;}
    printf("CEPHEUS GPU MEM: CPU-only NC contiguous bytes=%lu pa=%#llx pattern=%s; no DMA/SMMU\n",
        (unsigned long)b.bytes,(unsigned long long)b.pa,okay?"PASS":"FAIL");
    bzero(b.cpu,b.bytes);ceph_gpu_memory_barrier();
    (void)cepheus_gpu_memory_view_put(handle);
    int released=cepheus_gpu_memory_release_cpu(handle);
    printf("CEPHEUS GPU MEM: CPU-only release=%d stale_release=%d\n",released,
        cepheus_gpu_memory_release_cpu(handle));
    printf("CEPHEUS GPU MEM: CPU-only test retained_pages=%u\n",
        __atomic_load_n(&ceph_memory_pages,__ATOMIC_ACQUIRE));
}

/* These batch operations preserve exact handles, root PA and group membership.
 * All-or-none checks happen under the pool lock before state mutations. */
static int ceph_memory_group_indices(const struct ceph_gpu_publication *p,int indices[CE_GPU_WORKSPACE_BUFFERS])
{
    if(!p || !p->group)return -1;
    unsigned group=(unsigned)(p->group&255u);
    if(!group || group>16)return -1;
    const auto *saved=&ceph_groups[group-1];
    if(saved->group!=p->group || saved->ttbr0!=p->ttbr0 || saved->ttbr1!=p->ttbr1 ||
        saved->smr!=p->smr || saved->cb!=p->cb || saved->slot!=p->slot || saved->asid!=p->asid)return -1;
    for(unsigned i=0;i<CE_GPU_WORKSPACE_BUFFERS;++i) {
        if(saved->handles[i]!=p->handles[i])return -1;
        indices[i]=ceph_gpu_pool_index(p->handles[i]);
        if(indices[i]<0 || ceph_pool[indices[i]].group!=p->group)return -1;
    }
    return (int)group-1;
}

extern "C" int ceph_memory_share_group(const uint64_t handles[CE_GPU_WORKSPACE_BUFFERS],const struct ceph_gpu_publication *identity,
    struct ceph_gpu_publication *out)
{
    if(!handles || !identity || !out)return -1;
    *out={};IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int indices[CE_GPU_WORKSPACE_BUFFERS],group=-1,e=-1;
    if(ceph_pool_terminal)goto done;
    for(unsigned i=0;i<CE_GPU_WORKSPACE_BUFFERS;++i) {
        indices[i]=ceph_gpu_pool_index(handles[i]);
        if(indices[i]<0 || ceph_cp_building[indices[i]] || !ceph_mem_can_share(&ceph_pool[indices[i]].state) ||
            ceph_pool[indices[i]].buffer.published || ceph_pool[indices[i]].group)goto done;
        for(unsigned j=0;j<i;++j)if(indices[j]==indices[i])goto done;
    }
    if((identity->ttbr0&0xffffffffffULL)!=ceph_pool[indices[CE_GPU_WORKSPACE_ROOT]].buffer.pa)goto done;
    for(unsigned i=0;i<16;++i)if(ceph_group_cp[i] && ceph_groups[i].group) {
        const auto *cp=&ceph_groups[i];
        if(cp->cb==identity->cb || cp->slot==identity->slot || cp->asid==identity->asid ||
            ceph_smmu_overlap(cp->smr,identity->smr&0x7fff,(identity->smr>>16)&0x7fff))goto done;
    }
    {
        struct ceph_gpu_memory_view views[CE_GPU_WORKSPACE_BUFFERS];
        for(unsigned i=0;i<CE_GPU_WORKSPACE_BUFFERS;++i) {
            const auto *b=&ceph_pool[indices[i]].buffer;views[i]={b->cpu,b->pa,b->bytes};
        }
        if(ceph_memory_validate_workspace(views,identity))goto done;
    }
    for(unsigned i=0;i<16;++i)if(!ceph_groups[i].group){group=(int)i;break;}
    if(group<0 || ceph_group_generation==0x00ffffffffffffffULL)goto done;
    {
        struct ceph_gpu_publication p=*identity;
        p.group=(++ceph_group_generation<<8)|((unsigned)group+1u);
        for(unsigned i=0;i<CE_GPU_WORKSPACE_BUFFERS;++i)p.handles[i]=handles[i];
        ceph_gpu_memory_barrier();
        for(unsigned i=0;i<CE_GPU_WORKSPACE_BUFFERS;++i) {
            auto *slot=&ceph_pool[indices[i]];
            slot->state.state=CE_MEM_SHARED;slot->buffer.published=true;slot->group=p.group;
        }
        ceph_groups[group]=p;ceph_group_status[group]=CE_GPU_GROUP_SHARED;*out=p;e=0;
    }
done:IOLockUnlock(lock);return e;
}

static int ceph_memory_cp_group(const struct ceph_cp_publication *p,int indices[CE_CP_BUFFERS])
{
    if(!p || ceph_pool_terminal)return -1;
    int group=ceph_memory_group_indices(&p->memory,indices);
    if(group<0 || !ceph_group_cp[group] || ceph_group_status[group]!=CE_GPU_GROUP_SHARED)return -1;
    const auto *saved=&ceph_cp_groups[group];
    if(p->table_used!=saved->table_used)return -1;
    for(unsigned i=0;i<32;++i)if(p->restore_sha256[i]!=saved->restore_sha256[i])return -1;
    for(unsigned i=0;i<CE_CP_ROOT;++i) {
        const auto *a=&p->regions[i],*b=&saved->regions[i];
        if(a->va!=b->va || a->pa!=b->pa || a->bytes!=b->bytes || a->permissions!=b->permissions)return -1;
    }
    for(unsigned i=0;i<CE_CP_BUFFERS;++i)
        if(ceph_pool[indices[i]].state.state!=CE_MEM_SHARED || ceph_pool[indices[i]].state.views!=1 ||
            !ceph_pool[indices[i]].buffer.published || ceph_pool[indices[i]].firmware_kind)return -1;
    return group;
}
extern "C" int ceph_memory_cp_output_safe(const void *out,size_t bytes)
{
    uintptr_t dst=(uintptr_t)out;
    if(!dst || (dst&7u) || !bytes || bytes>UINTPTR_MAX-dst)return -1;
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int e=-1;
    const void *records[]={ceph_pool,ceph_groups,ceph_cp_groups,&ceph_cp_build,&ceph_zap};
    const size_t sizes[]={sizeof(ceph_pool),sizeof(ceph_groups),sizeof(ceph_cp_groups),sizeof(ceph_cp_build),sizeof(ceph_zap)};
    for(unsigned n=0;n<5;++n) {
        uintptr_t base=(uintptr_t)records[n];
        if(dst<base+sizes[n] && base<dst+bytes)goto done;
    }
    /* ZAP allocations use a separate budget and can be mutated outside the
     * mutex while ALLOCATING/COPYING. Refuse without reading those fields. */
    if(ceph_zap.state.state==CE_MEM_ALLOCATING || ceph_zap.phase==CE_ZM_COPYING)goto done;
    if(ceph_zap.state.state!=CE_MEM_FREE && ceph_zap.state.state!=CE_MEM_RETIRED) {
        const struct ceph_gpu_buffer *buffers[]={&ceph_zap.mdt,&ceph_zap.image};
        for(unsigned n=0;n<2;++n) {
            uintptr_t base=(uintptr_t)buffers[n]->cpu;
            if(base && (buffers[n]->bytes>UINTPTR_MAX-base ||
                (dst<base+buffers[n]->bytes && base<dst+bytes)))goto done;
        }
    }
    for(unsigned n=0;n<16;++n) {
        const auto *slot=&ceph_pool[n];
        if(slot->state.state==CE_MEM_ALLOCATING)goto done;
        if(slot->state.state==CE_MEM_FREE || slot->state.state==CE_MEM_RETIRED)continue;
        uintptr_t base=(uintptr_t)slot->buffer.cpu;
        if(base && (slot->buffer.bytes>UINTPTR_MAX-base ||
            (dst<base+slot->buffer.bytes && base<dst+bytes)))goto done;
    }
    e=0;
done:IOLockUnlock(lock);return e;
}
static int ceph_memory_cp_build_check(const struct ceph_cp_workspace *w,int indices[CE_CP_BUFFERS])
{
    if(!w || ceph_pool_terminal || w->state!=CE_CP_WS_TABLED || w->firmware || w->firmware_view ||
        w->restore_ready || w->publication.memory.group || w->sqe.offset!=4 || w->sqe.bytes!=32300 ||
        w->sqe.words!=8075 || w->sqe.version!=0x16ee190 ||
        w->leased!=((1u<<CE_CP_BUFFERS)-1) || !w->table.used || w->table.used>32 || w->table.pages!=32)return -1;
    const size_t minimum[]={32768,4096,32768,4096,4096,131072};
    for(unsigned i=0;i<CE_CP_BUFFERS;++i) {
        indices[i]=ceph_gpu_pool_index(w->handles[i]);
        if(indices[i]<0)return -1;
        const auto *slot=&ceph_pool[indices[i]];const auto *b=&slot->buffer;
        uintptr_t base=(uintptr_t)b->cpu;
        if(!ceph_mem_can_share(&slot->state) || slot->group || b->published || slot->firmware_kind || slot->firmware_bytes ||
            w->views[i].cpu!=b->cpu || w->views[i].pa!=b->pa || w->views[i].bytes!=b->bytes ||
            !base || (base&4095u) || b->bytes<minimum[i] || b->bytes>((minimum[i]+16383)&~(size_t)16383) ||
            b->bytes>UINTPTR_MAX-base || !b->pa || (b->pa&4095u) || b->pa>=(1ULL<<40) || b->bytes>(1ULL<<40)-b->pa ||
            ceph_cp_memory_output(w->views,w,sizeof(*w),b->cpu,b->bytes))return -1;
        for(unsigned j=0;j<i;++j)if(indices[i]==indices[j])return -1;
        if(i<CE_CP_ROOT && (w->regions[i].pa!=b->pa || w->regions[i].bytes!=minimum[i] || w->regions[i].va<0x300000 ||
            w->regions[i].permissions!=ceph_cp_workspace_permissions(i)))return -1;
        if(ceph_cp_build_active) {
            if(w->handles[i]!=ceph_cp_build.handles[i] || !ceph_cp_building[indices[i]])return -1;
            if(i<CE_CP_ROOT) {
                const auto *a=&w->regions[i],*saved=&ceph_cp_build.regions[i];
                if(a->va!=saved->va || a->pa!=saved->pa || a->bytes!=saved->bytes || a->permissions!=saved->permissions)return -1;
            }
        }else if(ceph_cp_building[indices[i]])return -1;
    }
    if(ceph_cp_build_active && w->table.used!=ceph_cp_build.table.used)return -1;
    struct ceph_vm_pool table=w->table;
    if(w->table.words!=w->views[CE_CP_ROOT].cpu || w->table.pa!=w->views[CE_CP_ROOT].pa ||
        ceph_vm39_matches(&table,w->regions,CE_CP_ROOT))return -1;
    static const uint8_t expected[32]={0xec,0x4c,0x62,0xfc,0x9d,0x38,0x44,0x33,0x7d,0x54,0xfc,0x6a,0x04,0xfe,0x01,0x82,
        0x9e,0xff,0xf3,0x77,0x9f,0xe0,0x3b,0xcc,0xe9,0x6e,0xe8,0x7d,0x2d,0x52,0x3c,0xe0};
    uint8_t digest[32];
    if(ceph_firmware_sha256((const uint8_t *)w->views[CE_CP_SQE].cpu,32300,digest))return -1;
    for(unsigned i=0;i<32;++i)if(digest[i]!=expected[i])return -1;
    const volatile uint8_t *sqe=(const volatile uint8_t *)w->views[CE_CP_SQE].cpu;
    for(size_t i=32300;i<w->views[CE_CP_SQE].bytes;++i)if(sqe[i])return -1;
    return 0;
}
extern "C" int ceph_memory_cp_build_acquire(const struct ceph_cp_workspace *w)
{
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int indices[CE_CP_BUFFERS],e=-1;
    if(!ceph_cp_build_active && !ceph_memory_cp_build_check(w,indices)) {
        ceph_cp_build=*w;ceph_cp_build_active=1;
        for(unsigned i=0;i<CE_CP_BUFFERS;++i)ceph_cp_building[indices[i]]=1;
        e=0;
    }
    IOLockUnlock(lock);return e;
}
extern "C" int ceph_memory_cp_build_validate(const struct ceph_cp_workspace *w)
{
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int indices[CE_CP_BUFFERS];
    int e=ceph_cp_build_active?ceph_memory_cp_build_check(w,indices):-1;
    IOLockUnlock(lock);return e;
}
extern "C" int ceph_memory_cp_build_finish(const struct ceph_cp_workspace *w,int uncertain)
{
    if(!w || (uncertain!=0 && uncertain!=1))return -1;
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int indices[CE_CP_BUFFERS],e=-1;
    if(!ceph_cp_build_active)goto done;
    for(unsigned i=0;i<CE_CP_BUFFERS;++i) {
        indices[i]=ceph_gpu_pool_index(w->handles[i]);
        if(indices[i]<0 || w->handles[i]!=ceph_cp_build.handles[i] || !ceph_cp_building[indices[i]])goto done;
    }
    if(!uncertain && ceph_memory_cp_build_check(w,indices))goto done;
    for(unsigned i=0;i<CE_CP_BUFFERS;++i) {
        if(uncertain)ceph_pool[indices[i]].state.state=CE_MEM_QUARANTINED;
        else ceph_cp_building[indices[i]]=0;
    }
    if(uncertain)ceph_pool_terminal=1;
    else ceph_cp_build_active=0;
    e=0;
done:IOLockUnlock(lock);return e;
}
static int ceph_memory_cp_tables(const struct ceph_cp_publication *p,const int indices[CE_CP_BUFFERS],
    struct ceph_cp_memory_view *out)
{
    struct ceph_gpu_memory_view views[CE_CP_BUFFERS];
    for(unsigned i=0;i<CE_CP_BUFFERS;++i) {
        const auto *b=&ceph_pool[indices[i]].buffer;views[i]={b->cpu,b->pa,b->bytes};
    }
    if(ceph_cp_memory_layout(views,p) ||
        (out && ceph_cp_memory_output(views,out,sizeof(*out),p,sizeof(*p))))return -1;
    if(out) {
        out->publication=*p;
        for(unsigned i=0;i<CE_CP_BUFFERS;++i)out->views[i]=views[i];
    }
    return 0;
}
static int ceph_memory_cp_seal_locked(const uint64_t handles[CE_CP_BUFFERS],
    const struct ceph_gpu_memory_view supplied[CE_CP_BUFFERS],const struct ceph_vm_region regions[CE_CP_ROOT],
    unsigned used,const struct ceph_gx_smmu_plan *plan,struct ceph_cp_publication *out,unsigned building)
{
    if(!handles || !supplied || !regions || !out || !ceph_gx_smmu_plan_canonical(plan))return -1;
    int indices[CE_CP_BUFFERS],group=-1,e=-1;
    struct ceph_cp_publication p={};struct ceph_gpu_memory_view views[CE_CP_BUFFERS];
    if(ceph_pool_terminal)goto done;
    for(unsigned i=0;i<CE_CP_BUFFERS;++i) {
        indices[i]=ceph_gpu_pool_index(handles[i]);
        if(indices[i]<0 || ceph_cp_building[indices[i]]!=building || !ceph_mem_can_share(&ceph_pool[indices[i]].state) ||
            ceph_pool[indices[i]].buffer.published || ceph_pool[indices[i]].group ||
            ceph_pool[indices[i]].firmware_kind)goto done;
        for(unsigned j=0;j<i;++j)if(indices[j]==indices[i])goto done;
        const auto *b=&ceph_pool[indices[i]].buffer;views[i]={b->cpu,b->pa,b->bytes};
        if(supplied[i].cpu!=b->cpu || supplied[i].pa!=b->pa || supplied[i].bytes!=b->bytes)goto done;
        p.memory.handles[i]=handles[i];
    }
    p.memory.ttbr0=plan->context.ttbr0;p.memory.ttbr1=plan->context.ttbr1;p.memory.smr=plan->context.smr;
    p.memory.cb=plan->context.cb;p.memory.slot=plan->context.slot;p.memory.asid=plan->context.asid;
    p.table_used=used;
    for(unsigned i=0;i<CE_CP_ROOT;++i)p.regions[i]=regions[i];
    if(ceph_cp_memory_output(views,out,sizeof(*out),plan,sizeof(*plan)) ||
        ceph_cp_memory_output(views,out,sizeof(*out),supplied,sizeof(*supplied)*CE_CP_BUFFERS) ||
        ceph_cp_memory_output(views,out,sizeof(*out),regions,sizeof(*regions)*CE_CP_ROOT) ||
        ceph_cp_memory_output(views,out,sizeof(*out),handles,sizeof(*handles)*CE_CP_BUFFERS) ||
        views[CE_CP_RESTORE].bytes<CE_CP_REGLIST_WORDS*4 ||
        ceph_firmware_sha256((const uint8_t *)views[CE_CP_RESTORE].cpu,CE_CP_REGLIST_WORDS*4,p.restore_sha256) ||
        ceph_cp_memory_layout(views,&p))goto done;
    for(unsigned i=0;i<16;++i) {
        if(!ceph_groups[i].group){if(group<0)group=(int)i;continue;}
        const auto *other=&ceph_groups[i];
        if(other->cb==p.memory.cb || other->slot==p.memory.slot || other->asid==p.memory.asid ||
            ceph_smmu_overlap(other->smr,0,0x401))goto done;
    }
    if(group<0 || ceph_group_generation==0x00ffffffffffffffULL)goto done;
    p.memory.group=(++ceph_group_generation<<8)|((unsigned)group+1u);
    ceph_gpu_memory_barrier();
    for(unsigned i=0;i<CE_CP_BUFFERS;++i) {
        auto *slot=&ceph_pool[indices[i]];
        slot->state.state=CE_MEM_SHARED;slot->buffer.published=true;slot->group=p.memory.group;
        if(building)ceph_cp_building[indices[i]]=0;
    }
    ceph_groups[group]=p.memory;ceph_cp_groups[group]=p;ceph_group_cp[group]=1;
    ceph_group_status[group]=CE_GPU_GROUP_SHARED;*out=p;e=0;
done:return e;
}
extern "C" int ceph_memory_cp_seal(const uint64_t handles[CE_CP_BUFFERS],
    const struct ceph_gpu_memory_view supplied[CE_CP_BUFFERS],const struct ceph_vm_region regions[CE_CP_ROOT],
    unsigned used,const struct ceph_gx_smmu_plan *plan,struct ceph_cp_publication *out)
{
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);
    int e=ceph_memory_cp_seal_locked(handles,supplied,regions,used,plan,out,0);
    IOLockUnlock(lock);return e;
}
extern "C" int ceph_memory_cp_build_seal(const struct ceph_cp_workspace *w,
    const struct ceph_gx_smmu_plan *plan,struct ceph_cp_publication *out)
{
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int indices[CE_CP_BUFFERS],e=-1;
    if(ceph_cp_build_active && !ceph_memory_cp_build_check(w,indices) &&
        !ceph_cp_memory_output(w->views,out,sizeof(*out),w,sizeof(*w)) &&
        !ceph_cp_memory_output(w->views,out,sizeof(*out),&ceph_cp_build,sizeof(ceph_cp_build))) {
        e=ceph_memory_cp_seal_locked(w->handles,w->views,w->regions,w->table.used,plan,out,1);
        if(!e){ceph_cp_build_active=0;ceph_cp_build={};}
    }
    IOLockUnlock(lock);return e;
}
extern "C" int ceph_memory_cp_acquire(const struct ceph_cp_publication *p,struct ceph_cp_memory_view *out)
{
    if(!out)return -1;
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int indices[CE_CP_BUFFERS],e=-1,group=ceph_memory_cp_group(p,indices);
    if(group>=0 && ceph_group_cp[group]==1 && !ceph_memory_cp_tables(p,indices,out)) {
        ceph_group_cp[group]=2;e=0;
    }
    IOLockUnlock(lock);return e;
}
extern "C" int ceph_memory_cp_validate(const struct ceph_cp_publication *p)
{
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int indices[CE_CP_BUFFERS],group=ceph_memory_cp_group(p,indices);
    int e=group>=0?ceph_memory_cp_tables(p,indices,NULL):-1;
    IOLockUnlock(lock);return e;
}
extern "C" int ceph_memory_cp_publish(const struct ceph_cp_publication *p)
{
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int indices[CE_CP_BUFFERS],e=-1,group=ceph_memory_cp_group(p,indices);
    if(group>=0 && (ceph_group_cp[group]==2 || ceph_group_cp[group]==3) && !ceph_memory_cp_tables(p,indices,NULL)) {
        ceph_gpu_memory_barrier();ceph_group_cp[group]=3;e=0;
    }
    IOLockUnlock(lock);return e;
}
extern "C" int ceph_memory_cp_installed(const struct ceph_cp_publication *p)
{
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int indices[CE_CP_BUFFERS],group=ceph_memory_cp_group(p,indices);
    int e=group>=0 && ceph_group_cp[group]==4?ceph_memory_cp_tables(p,indices,NULL):-1;
    IOLockUnlock(lock);return e;
}
extern "C" int ceph_memory_cp_ring_publish(const struct ceph_cp_publication *p)
{
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int indices[CE_CP_BUFFERS],e=-1,group=ceph_memory_cp_group(p,indices);
    if(group<0 || ceph_group_cp[group]!=4 || ceph_memory_cp_tables(p,indices,NULL))goto done;
    for(unsigned n=CE_CP_RING;n<=CE_CP_RPTR;++n) {
        const auto *b=&ceph_pool[indices[n]].buffer;
        const volatile uint8_t *cpu=(const volatile uint8_t *)b->cpu;
        for(size_t i=0;i<b->bytes;++i)if(cpu[i])goto done;
    }
    ceph_gpu_memory_barrier();e=0;
done:IOLockUnlock(lock);return e;
}
extern "C" int ceph_memory_cp_finish(const struct ceph_cp_publication *p,int result)
{
    if(!p || (result!=CE_MEMORY_CP_REFUSED && result!=CE_MEMORY_CP_INSTALLED && result!=CE_MEMORY_CP_UNCERTAIN))return -1;
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int indices[CE_CP_BUFFERS],group=ceph_memory_group_indices(&p->memory,indices),e=-1;
    /* Use saved identity even for uncertainty, but never touch stale groups. */
    if(group<0 || !ceph_group_cp[group] || p->table_used!=ceph_cp_groups[group].table_used)goto done;
    for(unsigned i=0;i<32;++i)if(p->restore_sha256[i]!=ceph_cp_groups[group].restore_sha256[i])goto done;
    for(unsigned i=0;i<CE_CP_ROOT;++i) {
        const auto *a=&p->regions[i],*b=&ceph_cp_groups[group].regions[i];
        if(a->va!=b->va || a->pa!=b->pa || a->bytes!=b->bytes || a->permissions!=b->permissions)goto done;
    }
    if(result==CE_MEMORY_CP_UNCERTAIN) {
        ceph_group_status[group]=CE_GPU_GROUP_UNCERTAIN;ceph_pool_terminal=1;
        for(unsigned i=0;i<CE_CP_BUFFERS;++i)ceph_pool[indices[i]].state.state=CE_MEM_QUARANTINED;
        e=0;goto done;
    }
    if(ceph_memory_cp_group(p,indices)<0 || ceph_memory_cp_tables(p,indices,NULL))goto done;
    if(result==CE_MEMORY_CP_REFUSED && ceph_group_cp[group]==2){ceph_group_cp[group]=1;e=0;}
    if(result==CE_MEMORY_CP_INSTALLED && ceph_group_cp[group]==3){ceph_group_cp[group]=4;e=0;}
done:IOLockUnlock(lock);return e;
}

extern "C" int ceph_memory_retire_begin(const struct ceph_gpu_publication *p)
{
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int indices[CE_GPU_WORKSPACE_BUFFERS],e=-1;
    int group=ceph_memory_group_indices(p,indices);
    if(group<0 || ceph_group_installing[group] || ceph_group_uploading[group] || ceph_group_hfi[group] || ceph_group_cp[group])goto done;
    for(unsigned i=0;i<CE_GPU_WORKSPACE_BUFFERS;++i)if(!ceph_mem_can_retire(&ceph_pool[indices[i]].state))goto done;
    for(unsigned i=0;i<CE_GPU_WORKSPACE_BUFFERS;++i)ceph_pool[indices[i]].state.state=CE_MEM_RETIRING;
    ceph_group_status[(unsigned)(p->group&255u)-1]=CE_GPU_GROUP_RETIRING;
    e=0;
done:IOLockUnlock(lock);return e;
}

extern "C" int ceph_memory_retire_finish(const struct ceph_gpu_publication *p,int success)
{
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int indices[CE_GPU_WORKSPACE_BUFFERS];int group=ceph_memory_group_indices(p,indices);
    if(group<0){IOLockUnlock(lock);return -1;}
    for(unsigned i=0;i<CE_GPU_WORKSPACE_BUFFERS;++i)if(ceph_pool[indices[i]].state.state!=CE_MEM_RETIRING){IOLockUnlock(lock);return -1;}
    if(!success) {
        ceph_pool_terminal=1;
        ceph_group_status[group]=CE_GPU_GROUP_UNCERTAIN;
        for(unsigned i=0;i<CE_GPU_WORKSPACE_BUFFERS;++i)ceph_pool[indices[i]].state.state=CE_MEM_QUARANTINED;
        printf("CEPHEUS GPU MEM: terminal GPU session group=%#llx; all six allocations retained; new allocations blocked\n",(unsigned long long)p->group);
        IOLockUnlock(lock);return -1;
    }
    /* Stop/unmap/TLB proof is complete, callers have surrendered CPU views.
     * Keep slots unobservable while slow IOKit completion runs. */
    for(unsigned i=0;i<CE_GPU_WORKSPACE_BUFFERS;++i) {
        ceph_pool[indices[i]].state.views=0;
        ceph_pool[indices[i]].state.state=CE_MEM_ALLOCATING;
        ceph_pool[indices[i]].buffer.published=false;
    }
    IOLockUnlock(lock);
    int e=0;
    for(unsigned i=CE_GPU_WORKSPACE_BUFFERS;i>0;--i) {
        auto *slot=&ceph_pool[indices[i-1]];ceph_gpu_buffer_dispose_cpu(&slot->buffer);
        IOLockLock(lock);
        if(slot->buffer.published)e=-1;
        (void)ceph_mem_finish_allocation(&slot->state,0,slot->buffer.published);
        if(!slot->buffer.published)slot->group=0;
        else printf("CEPHEUS GPU MEM: detached but complete failed slot=%u bytes=%lu\n",
            (unsigned)indices[i-1],(unsigned long)slot->buffer.bytes);
        IOLockUnlock(lock);
    }
    IOLockLock(lock);
    if(!e) {ceph_groups[group]={};ceph_group_status[group]=0;}
    else {ceph_group_status[group]=CE_GPU_GROUP_DETACHED_RETAINED;ceph_pool_terminal=1;}
    IOLockUnlock(lock);return e;
}

extern "C" int cepheus_gpu_memory_group_status(uint64_t group,struct ceph_gpu_group_status *out)
{
    if(!out)return -1;
    *out={};IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);unsigned slot=(unsigned)(group&255u);
    if(!group || !slot || slot>16 || ceph_groups[slot-1].group!=group) {IOLockUnlock(lock);return -1;}
    out->identity=ceph_groups[slot-1];out->state=ceph_group_status[slot-1];out->pool_terminal=ceph_pool_terminal;
    /* ALLOCATING buffers are being completed outside the lock. Their pointer
     * and byte fields may change; don't race them for diagnostic counts. A
     * RETIRING group can therefore report only its currently stable slots. */
    for(unsigned i=0;i<16;++i)if(ceph_pool[i].state.state!=CE_MEM_ALLOCATING &&
        ceph_pool[i].group==group && ceph_pool[i].buffer.memory) {
        ++out->retained_buffers;out->retained_bytes+=ceph_pool[i].buffer.bytes;
    }
    IOLockUnlock(lock);return 0;
}

extern "C" int cepheus_gpu_memory_abandon_workspace(const struct ceph_gpu_publication *p)
{
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int indices[CE_GPU_WORKSPACE_BUFFERS];
    int group=ceph_memory_group_indices(p,indices);
    if(group<0 || ceph_group_status[group]!=CE_GPU_GROUP_SHARED ||
        ceph_group_installing[group] || ceph_group_uploading[group] || ceph_group_hfi[group] || ceph_group_cp[group]) {IOLockUnlock(lock);return -1;}
    for(unsigned i=0;i<CE_GPU_WORKSPACE_BUFFERS;++i)
        if(ceph_pool[indices[i]].state.state!=CE_MEM_SHARED) {IOLockUnlock(lock);return -1;}
    for(unsigned i=0;i<CE_GPU_WORKSPACE_BUFFERS;++i)ceph_pool[indices[i]].state.state=CE_MEM_QUARANTINED;
    ceph_group_status[group]=CE_GPU_GROUP_UNCERTAIN;ceph_pool_terminal=1;
    printf("CEPHEUS GPU MEM: startup abandoned group=%#llx; all backing retained, pool terminal\n",(unsigned long long)p->group);
    IOLockUnlock(lock);return 0;
}

static int ceph_memory_smmu_group(const struct ceph_gpu_publication *p,int indices[CE_GPU_WORKSPACE_BUFFERS])
{
    if(ceph_pool_terminal)return -1;
    int group=ceph_memory_group_indices(p,indices);
    if(group<0 || ceph_group_status[group]!=CE_GPU_GROUP_SHARED || ceph_group_cp[group])return -1;
    for(unsigned i=0;i<CE_GPU_WORKSPACE_BUFFERS;++i)
        if(ceph_pool[indices[i]].state.state!=CE_MEM_SHARED ||
            ceph_pool[indices[i]].state.views!=1 || !ceph_pool[indices[i]].buffer.published)return -1;
    return group;
}
static int ceph_memory_smmu_tables(const struct ceph_gpu_publication *p,const int indices[CE_GPU_WORKSPACE_BUFFERS],
    struct ceph_gmu_start_config *config)
{
    struct ceph_gpu_memory_view views[CE_GPU_WORKSPACE_BUFFERS];
    for(unsigned i=0;i<CE_GPU_WORKSPACE_BUFFERS;++i) {
        const auto *b=&ceph_pool[indices[i]].buffer;views[i]={b->cpu,b->pa,b->bytes};
    }
    if(ceph_memory_validate_workspace(views,p))return -1;
    if(config) {
        struct ceph_gpu_backing backing[CE_BUF_COUNT];
        for(unsigned i=0;i<CE_BUF_COUNT;++i)backing[i]={views[i].pa,views[i].bytes};
        struct ceph_gpu_va_plan layout;if(ceph_gpu_va_plan(backing,&layout))return -1;
        *config=layout.start;
    }
    return 0;
}
extern "C" int ceph_memory_smmu_acquire(const struct ceph_gpu_publication *p,struct ceph_gmu_start_config *out)
{
    if(!out)return -1;
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int indices[CE_GPU_WORKSPACE_BUFFERS];int group=ceph_memory_smmu_group(p,indices),e=-1;
    struct ceph_gmu_start_config config;
    if(group>=0 && !ceph_group_installing[group] && !ceph_group_uploading[group] && !ceph_group_hfi[group] &&
        !ceph_memory_smmu_tables(p,indices,&config)) {
        ceph_group_installing[group]=1;*out=config;e=0;
    }
    IOLockUnlock(lock);return e;
}
extern "C" int ceph_memory_smmu_publish(const struct ceph_gpu_publication *p)
{
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int indices[CE_GPU_WORKSPACE_BUFFERS];int group=ceph_memory_smmu_group(p,indices),e=-1;
    if(group>=0 && ceph_group_installing[group] && !ceph_memory_smmu_tables(p,indices,NULL)) {
        /* All backing is Normal-NC; order the actual materialized tables,
         * not a caller's scratch pointer or a no-op publication callback. */
        ceph_gpu_memory_barrier();e=0;
    }
    IOLockUnlock(lock);return e;
}
extern "C" int ceph_memory_smmu_validate(const struct ceph_gpu_publication *p)
{
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int indices[CE_GPU_WORKSPACE_BUFFERS];int group=ceph_memory_smmu_group(p,indices);
    int e=group>=0 && ceph_group_installing[group]?0:-1;
    IOLockUnlock(lock);return e;
}
extern "C" int ceph_memory_smmu_finish(const struct ceph_gpu_publication *p,int result)
{
    if(result!=CE_MEMORY_SMMU_REFUSED && result!=CE_MEMORY_SMMU_INSTALLED && result!=CE_MEMORY_SMMU_UNCERTAIN)return -1;
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int indices[CE_GPU_WORKSPACE_BUFFERS];int group=ceph_memory_group_indices(p,indices);
    if(group<0 || !ceph_group_installing[group] || ceph_group_status[group]!=CE_GPU_GROUP_SHARED) {
        IOLockUnlock(lock);return -1;
    }
    ceph_group_installing[group]=0;
    if(result==CE_MEMORY_SMMU_UNCERTAIN) {
        ceph_group_status[group]=CE_GPU_GROUP_UNCERTAIN;ceph_pool_terminal=1;
        for(unsigned i=0;i<CE_GPU_WORKSPACE_BUFFERS;++i)ceph_pool[indices[i]].state.state=CE_MEM_QUARANTINED;
    }
    IOLockUnlock(lock);return 0;
}
extern "C" int ceph_memory_upload_acquire(const struct ceph_gpu_publication *p,
    struct ceph_gmu_start_config *out,struct ceph_gmu_mapping maps[2])
{
    if(!out || !maps)return -1;
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int indices[CE_GPU_WORKSPACE_BUFFERS],e=-1;
    int group=ceph_memory_smmu_group(p,indices);struct ceph_gmu_start_config config;
    if(group>=0 && !ceph_group_installing[group] && !ceph_group_uploading[group] && !ceph_group_hfi[group] &&
        !ceph_memory_smmu_tables(p,indices,&config)) {
        ceph_group_uploading[group]=1;*out=config;
        maps[0]={0x4000,0x3c000,(uint8_t *)ceph_pool[indices[0]].buffer.cpu,CE_GMU_ICACHE};
        maps[1]={0x44000,0x3c000,(uint8_t *)ceph_pool[indices[1]].buffer.cpu,CE_GMU_DCACHE};e=0;
    }
    IOLockUnlock(lock);return e;
}
extern "C" int ceph_memory_upload_validate(const struct ceph_gpu_publication *p)
{
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int indices[CE_GPU_WORKSPACE_BUFFERS];int group=ceph_memory_smmu_group(p,indices);
    int e=group>=0 && ceph_group_uploading[group]?0:-1;
    IOLockUnlock(lock);return e;
}
extern "C" int ceph_memory_upload_publish(const struct ceph_gpu_publication *p)
{
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int indices[CE_GPU_WORKSPACE_BUFFERS];int group=ceph_memory_smmu_group(p,indices),e=-1;
    if(group>=0 && ceph_group_uploading[group] && !ceph_memory_smmu_tables(p,indices,NULL)) {
        ceph_gpu_memory_barrier();e=0;
    }
    IOLockUnlock(lock);return e;
}
extern "C" int ceph_memory_upload_finish(const struct ceph_gpu_publication *p,int result)
{
    if(result!=CE_MEMORY_UPLOAD_REFUSED && result!=CE_MEMORY_UPLOAD_COPIED && result!=CE_MEMORY_UPLOAD_UNCERTAIN)return -1;
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int indices[CE_GPU_WORKSPACE_BUFFERS];int group=ceph_memory_group_indices(p,indices);
    if(group<0 || !ceph_group_uploading[group] || ceph_group_status[group]!=CE_GPU_GROUP_SHARED) {
        IOLockUnlock(lock);return -1;
    }
    ceph_group_uploading[group]=0;
    if(result==CE_MEMORY_UPLOAD_UNCERTAIN) {
        ceph_group_status[group]=CE_GPU_GROUP_UNCERTAIN;ceph_pool_terminal=1;
        for(unsigned i=0;i<CE_GPU_WORKSPACE_BUFFERS;++i)ceph_pool[indices[i]].state.state=CE_MEM_QUARANTINED;
    }
    IOLockUnlock(lock);return 0;
}
extern "C" int ceph_memory_hfi_acquire(const struct ceph_gpu_publication *p,
    struct ceph_gpu_memory_view *out,struct ceph_gmu_start_config *config_out)
{
    if(!out || !config_out)return -1;
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int indices[CE_GPU_WORKSPACE_BUFFERS],e=-1;
    int group=ceph_memory_smmu_group(p,indices);struct ceph_gmu_start_config config;
    if(group>=0 && !ceph_group_installing[group] && !ceph_group_uploading[group] && !ceph_group_hfi[group] &&
        !ceph_memory_smmu_tables(p,indices,&config)) {
        const auto *b=&ceph_pool[indices[2]].buffer;
        ceph_group_hfi[group]=1;*out={b->cpu,b->pa,b->bytes};*config_out=config;e=0;
    }
    IOLockUnlock(lock);return e;
}
extern "C" int ceph_memory_hfi_validate(const struct ceph_gpu_publication *p)
{
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int indices[CE_GPU_WORKSPACE_BUFFERS];int group=ceph_memory_smmu_group(p,indices);
    int e=group>=0 && ceph_group_hfi[group]?0:-1;
    IOLockUnlock(lock);return e;
}
extern "C" int ceph_memory_hfi_publish(const struct ceph_gpu_publication *p)
{
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int indices[CE_GPU_WORKSPACE_BUFFERS];int group=ceph_memory_smmu_group(p,indices);
    int e=-1;
    if(group>=0 && ceph_group_hfi[group]) {ceph_gpu_memory_barrier();e=0;}
    IOLockUnlock(lock);return e;
}
extern "C" int ceph_memory_hfi_seal(const struct ceph_gpu_publication *p)
{
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int indices[CE_GPU_WORKSPACE_BUFFERS];int group=ceph_memory_smmu_group(p,indices);
    int e=-1;
    if(group>=0 && ceph_group_hfi[group]==1) {ceph_group_hfi[group]=2;e=0;}
    IOLockUnlock(lock);return e;
}
extern "C" int ceph_memory_hfi_finish(const struct ceph_gpu_publication *p,int result)
{
    if(result!=CE_MEMORY_HFI_REFUSED && result!=CE_MEMORY_HFI_RETAINED && result!=CE_MEMORY_HFI_UNCERTAIN)return -1;
    IOLock *lock=ceph_gpu_pool_lock();if(!lock)return -1;
    IOLockLock(lock);int indices[CE_GPU_WORKSPACE_BUFFERS];int group=ceph_memory_group_indices(p,indices);
    if(group<0 || !ceph_group_hfi[group] || ceph_group_status[group]!=CE_GPU_GROUP_SHARED ||
        (result==CE_MEMORY_HFI_REFUSED && ceph_group_hfi[group]!=1) ||
        (result==CE_MEMORY_HFI_RETAINED && ceph_group_hfi[group]!=2)) {IOLockUnlock(lock);return -1;}
    if(result==CE_MEMORY_HFI_REFUSED)ceph_group_hfi[group]=0;
    if(result==CE_MEMORY_HFI_RETAINED)ceph_group_hfi[group]=3;
    if(result==CE_MEMORY_HFI_UNCERTAIN) {
        ceph_group_hfi[group]=2;ceph_group_status[group]=CE_GPU_GROUP_UNCERTAIN;ceph_pool_terminal=1;
        for(unsigned i=0;i<CE_GPU_WORKSPACE_BUFFERS;++i)ceph_pool[indices[i]].state.state=CE_MEM_QUARANTINED;
    }
    IOLockUnlock(lock);return 0;
}
#endif
