#pragma once
#include "IOLib.h"
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <mach/vm_param.h>
/* Test double for the IOKit allocator ONLY. Actual production .cpp is compiled
 * and linked into this host executable, not reimplemented. No GPU simulated. */
namespace mock_io {
inline std::atomic<unsigned> live{0},prepared{0},released{0};
inline std::atomic<uint64_t> next_pa{0x120000000ULL};
inline std::atomic<int> fail_alloc{0},fail_prepare{0},fail_complete{0},bad_segment{0};
}
class IOBufferMemoryDescriptor {
    void *memory=nullptr;size_t length=0;bool wired=false;uint64_t pa=0;
public:
    static IOBufferMemoryDescriptor *inTaskWithPhysicalMask(void *,unsigned options,size_t bytes,uint64_t mask){
        assert(options==(kIODirectionInOut|kIOMemoryHostPhysicallyContiguous|kIOMemoryMapperNone|kIOMapWriteCombineCache));
        assert(mask==0x000000fffffff000ULL);
        if(mock_io::fail_alloc)return nullptr;
        auto *p=new IOBufferMemoryDescriptor;
        assert(!(bytes&(page_size-1)));
        if(posix_memalign(&p->memory,page_size,bytes)){delete p;return nullptr;}
        p->length=bytes;p->pa=mock_io::next_pa.fetch_add(bytes);++mock_io::live;return p;
    }
    int prepare(int){if(mock_io::fail_prepare)return -1;assert(!wired);wired=true;++mock_io::prepared;return 0;}
    int complete(int){assert(wired);if(mock_io::fail_complete)return -1;wired=false;--mock_io::prepared;return 0;}
    uint64_t getPhysicalSegment(size_t offset,IOByteCount *bytes,unsigned options){
        assert(!offset && options==kIOMemoryMapperNone && wired);*bytes=mock_io::bad_segment?4096:length;
        return pa; /* simulated unique PA, never claim hardware mapping */
    }
    void *getBytesNoCopy(){return memory;}
    size_t getLength(){return length;}
    void release(){assert(!wired);free(memory);--mock_io::live;++mock_io::released;delete this;}
};
