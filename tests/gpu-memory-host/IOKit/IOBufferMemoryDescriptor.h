#pragma once
#include "IOLib.h"
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>
#include <mach/vm_param.h>
#include <vm/memory_types.h>
#include "cepheus_gpu_memory.h"
/* Test double for the IOKit allocator ONLY. Actual production .cpp is compiled
 * and linked into this host executable, not reimplemented. No GPU simulated. */
namespace mock_io {
inline std::atomic<unsigned> live{0},prepared{0},released{0};
inline std::atomic<unsigned> maps{0},raw_views{0};
inline std::atomic<unsigned> map_calls{0},cache_queries{0},alloc_calls{0};
inline std::atomic<uint64_t> next_pa{0x120000000ULL};
inline std::atomic<int> fail_alloc{0},fail_prepare{0},fail_complete{0},bad_segment{0};
inline std::atomic<int> fail_map{0},bad_cache{0};
inline std::atomic<int> bad_pmap_cache{0};
inline std::atomic<unsigned> fail_alloc_at{0},fail_map_at{0};
inline std::atomic<int> bad_large_alignment{0};
inline std::atomic<int> fail_restore{0},bad_restore_par{0},stale_restore_alias{0};
inline std::atomic<unsigned> translation_calls{0},bad_translation_at{0};
inline std::atomic<int> translation_fault{0};
struct map_record {uintptr_t va;size_t bytes;uint64_t pa;unsigned cache;bool untouched,resident;};
inline std::mutex map_lock;
inline std::vector<map_record> regions;
inline void (*after_allocate)()=nullptr; /* deterministic outside-pool-lock fault injection */
}
extern "C" {
inline uint64_t vm_first_phys=0x101004000ULL,vm_last_phys=0x1c1004000ULL;
inline unsigned int IODefaultCacheBits(addr64_t pa) {
    assert(cepheus_gpu_pa_range_managed(pa,page_size,vm_first_phys,vm_last_phys));
    ++mock_io::cache_queries;
    std::lock_guard<std::mutex> guard(mock_io::map_lock);
    for(const auto &m:mock_io::regions)if(pa>=m.pa && pa-m.pa<m.bytes) {
        if(m.cache==kIOMapCopybackCache)return VM_WIMG_COPYBACK;
        return mock_io::bad_pmap_cache?0:m.cache==kIOMapRealTimeCache?VM_WIMG_RT:VM_WIMG_WCOMB;
    }
    return VM_WIMG_COPYBACK;
}
}
inline uint64_t ceph_zap_translate_write(uintptr_t va)
{
    unsigned call=++mock_io::translation_calls;
    std::lock_guard<std::mutex> guard(mock_io::map_lock);
    for(auto &m:mock_io::regions)if(va>=m.va && va-m.va<m.bytes) {
        if(!m.resident)return 1; /* AT cannot service a missing mapping */
        uint64_t attr=m.cache==kIOMapRealTimeCache?0:m.cache==kIOMapCopybackCache?0xff:0x44;
        uint64_t par=(attr<<56)|((m.pa+va-m.va)&~4095ULL);
        if(m.cache==kIOMapCopybackCache)return mock_io::bad_restore_par?par^0x100000000000000ULL:par;
        if(call==mock_io::bad_translation_at) {
            m.untouched=true;
            switch(mock_io::translation_fault) {
            case 1:return par|1;
            case 2:return par^4096;
            case 3:return par^0x4400000000000000ULL;
            default:assert(0);
            }
        }
        return par;
    }
    assert(0);return 1;
}
class IOMemoryMap {
    void *mapped;size_t length;unsigned options;
public:
    IOMemoryMap(size_t bytes,unsigned flags,uint64_t pa):mapped(nullptr),length(bytes),options(flags){
        assert(!posix_memalign(&mapped,page_size,bytes));memset(mapped,0xc3,bytes);++mock_io::maps;
        std::lock_guard<std::mutex> guard(mock_io::map_lock);
        unsigned cache=flags&kIOMapCacheMask;
        for(auto &m:mock_io::regions)if(m.pa==pa && !(cache==kIOMapCopybackCache && mock_io::stale_restore_alias))m.cache=cache;
        mock_io::regions.push_back({(uintptr_t)mapped,bytes,pa,cache,false,bool(flags&kIOMapPrefault)});
    }
    uintptr_t getVirtualAddress(){return (uintptr_t)mapped;}
    size_t getLength(){return length;}
    unsigned getMapOptions(){return mock_io::bad_cache && (options&kIOMapCacheMask)!=kIOMapCopybackCache?0:options;}
    void release(){
        {std::lock_guard<std::mutex> guard(mock_io::map_lock);
        for(auto i=mock_io::regions.begin();i!=mock_io::regions.end();++i)if(i->va==(uintptr_t)mapped){
            if(i->untouched)for(size_t n=0;n<length;++n)assert(((const uint8_t *)mapped)[n]==0xc3);
            mock_io::regions.erase(i);break;}}
        free(mapped);--mock_io::maps;delete this;
    }
};
class IOBufferMemoryDescriptor {
    void *memory=nullptr;size_t length=0;bool wired=false;uint64_t pa=0;
public:
    static IOBufferMemoryDescriptor *inTaskWithPhysicalMask(void *,unsigned options,size_t bytes,uint64_t mask){
        /* Cache flags here would change attributes BEFORE production checks. */
        assert(options==(kIODirectionInOut|kIOMemoryHostPhysicallyContiguous|kIOMemoryMapperNone));
        uint64_t alignment=((~mask)&(mask-1))+1;
        assert(alignment==4096 || alignment==1048576);
        assert(mask && mask==cepheus_gpu_pa_aligned_window_mask(vm_first_phys,vm_last_phys,page_size,alignment));
        unsigned call=++mock_io::alloc_calls;
        if(mock_io::fail_alloc || call==mock_io::fail_alloc_at)return nullptr;
        auto *p=new IOBufferMemoryDescriptor;
        assert(!(bytes&(page_size-1)));
        if(posix_memalign(&p->memory,page_size,bytes)){delete p;return nullptr;}
        p->length=bytes;
        if(alignment==4096)p->pa=mock_io::next_pa.fetch_add(bytes);
        else {
            uint64_t next=mock_io::next_pa.load(),aligned;
            do {aligned=(next+alignment-1)&~(alignment-1);}
            while(!mock_io::next_pa.compare_exchange_weak(next,aligned+bytes));
            p->pa=aligned+(mock_io::bad_large_alignment?page_size:0);
        }
        ++mock_io::live;
        if(mock_io::after_allocate)mock_io::after_allocate();
        return p;
    }
    int prepare(int){if(mock_io::fail_prepare)return -1;assert(!wired);wired=true;++mock_io::prepared;return 0;}
    int complete(int){assert(wired);if(mock_io::fail_complete)return -1;wired=false;--mock_io::prepared;return 0;}
    uint64_t getPhysicalSegment(size_t offset,IOByteCount *bytes,unsigned options){
        assert(!offset && options==kIOMemoryMapperNone && wired);*bytes=mock_io::bad_segment?4096:length;
        return pa; /* simulated unique PA, never claim hardware mapping */
    }
    void *getBytesNoCopy(){++mock_io::raw_views;return memory;}
    IOMemoryMap *createMappingInTask(void *,uint64_t address,unsigned flags,size_t offset,size_t bytes){
        unsigned call=++mock_io::map_calls;
        assert(cepheus_gpu_pa_range_managed(pa,bytes,vm_first_phys,vm_last_phys));
        unsigned cache=flags&kIOMapCacheMask;
        assert(cache==kIOMapWriteCombineCache || cache==kIOMapRealTimeCache || cache==kIOMapCopybackCache);
        assert(wired && !address && !offset && bytes==length &&
            (flags&~kIOMapPrefault)==(kIOMapAnywhere|kIOMapUnique|cache));
        bool fail=cache==kIOMapCopybackCache?bool(mock_io::fail_restore):bool(mock_io::fail_map);
        return (fail || call==mock_io::fail_map_at)?nullptr:new IOMemoryMap(bytes,flags,pa);
    }
    size_t getLength(){return length;}
    void release(){assert(!wired);free(memory);--mock_io::live;++mock_io::released;delete this;}
};
