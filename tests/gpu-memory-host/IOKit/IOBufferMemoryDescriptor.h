#pragma once
#include "IOLib.h"
#include <atomic>
#include <cassert>
#include <cstdlib>
/* Test double for the IOKit allocator ONLY. Actual production .cpp is compiled
 * and linked into this host executable, not reimplemented. No GPU simulated. */
namespace mock_io {
inline std::atomic<unsigned> live{0},prepared{0},released{0};
inline std::atomic<int> fail_alloc{0},fail_prepare{0},fail_complete{0},bad_segment{0};
}
class IOBufferMemoryDescriptor {
    void *memory=nullptr;size_t length=0;bool wired=false;
public:
    static IOBufferMemoryDescriptor *inTaskWithPhysicalMask(void *,unsigned options,size_t bytes,uint64_t mask){
        assert(options==(kIODirectionInOut|kIOMemoryHostPhysicallyContiguous|kIOMemoryMapperNone|kIOMapWriteCombineCache));
        assert(mask==0x000000fffffff000ULL);
        if(mock_io::fail_alloc)return nullptr;
        auto *p=new IOBufferMemoryDescriptor;
        if(posix_memalign(&p->memory,4096,bytes)){delete p;return nullptr;}
        p->length=bytes;++mock_io::live;return p;
    }
    int prepare(int){if(mock_io::fail_prepare)return -1;assert(!wired);wired=true;++mock_io::prepared;return 0;}
    int complete(int){assert(wired);if(mock_io::fail_complete)return -1;wired=false;--mock_io::prepared;return 0;}
    uint64_t getPhysicalSegment(size_t offset,IOByteCount *bytes,unsigned options){
        assert(!offset && options==kIOMemoryMapperNone && wired);*bytes=mock_io::bad_segment?4096:length;
        return 0x120000000ULL; /* simulated PA, never claim hardware mapping */
    }
    void *getBytesNoCopy(){return memory;}
    size_t getLength(){return length;}
    void release(){assert(!wired);free(memory);--mock_io::live;++mock_io::released;delete this;}
};
