#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include "cepheus_gpu_memory.h"

int main(int argc,char **argv)
{
    uint64_t h=0;assert(cepheus_gpu_memory_alloc(1,&h)<0 && !mock_io::live);
    mock_io::fail_alloc=1;assert(cepheus_gpu_memory_alloc(4096,&h)<0 && !h && !mock_io::live);mock_io::fail_alloc=0;
    mock_io::fail_prepare=1;assert(cepheus_gpu_memory_alloc(4096,&h)<0 && !h && !mock_io::live);mock_io::fail_prepare=0;
    mock_io::bad_segment=1;assert(cepheus_gpu_memory_alloc(8192,&h)<0 && !h && !mock_io::live && !mock_io::prepared);mock_io::bad_segment=0;
    assert(!cepheus_gpu_memory_alloc(8192,&h));struct ceph_gpu_memory_view view={};
    assert(!cepheus_gpu_memory_view_get(h,&view) && view.bytes==16384 && view.pa>=0x120000000ULL && !(view.pa&0x3fff));
    assert(cepheus_gpu_memory_publish(h)<0 && cepheus_gpu_memory_release_cpu(h)<0);
    memset(view.cpu,0xab,view.bytes);assert(!cepheus_gpu_memory_view_put(h));
    uint64_t stale=h;assert(!cepheus_gpu_memory_release_cpu(h) && !mock_io::live);
    assert(!cepheus_gpu_memory_alloc(4096,&h) && h!=stale);
    assert(cepheus_gpu_memory_view_get(stale,&view)<0 && cepheus_gpu_memory_release_cpu(stale)<0);
    assert(!cepheus_gpu_memory_release_cpu(h));
    uint64_t slots[16];
    for(auto &x:slots)assert(!cepheus_gpu_memory_alloc(4096,&x));
    uint64_t extra;assert(cepheus_gpu_memory_alloc(4096,&extra)<0);
    for(auto x:slots)assert(!cepheus_gpu_memory_release_cpu(x));
    /* Exercise exact pool mutex and allocation-outside-lock implementation. */
    std::vector<std::thread> threads;
    for(unsigned i=0;i<8;++i)threads.emplace_back([]{
        for(unsigned n=0;n<100;++n){uint64_t x;assert(!cepheus_gpu_memory_alloc(4096,&x));
            ceph_gpu_memory_view v={};assert(!cepheus_gpu_memory_view_get(x,&v));
            memset(v.cpu,0x5a,v.bytes);assert(!cepheus_gpu_memory_view_put(x));assert(!cepheus_gpu_memory_release_cpu(x));}
    });
    for(auto &t:threads)t.join();
    assert(!mock_io::live && !mock_io::prepared);
    uint64_t large[4],c;for(auto &x:large)assert(!cepheus_gpu_memory_alloc(256*1024,&x));
    assert(cepheus_gpu_memory_alloc(4096,&c)<0 && !c);
    for(auto x:large)assert(!cepheus_gpu_memory_release_cpu(x));
    std::vector<uint8_t> zeros(37680);assert(cepheus_gpu_firmware_snapshot(0,zeros.data(),zeros.size(),&h)<0 && !h && !mock_io::live);
    if(argc==3)for(unsigned kind=0;kind<2;++kind){
        std::ifstream f(argv[kind+1],std::ios::binary);assert(f);std::vector<uint8_t> fw((std::istreambuf_iterator<char>(f)),{});
        assert(!cepheus_gpu_firmware_snapshot(kind,fw.data(),fw.size(),&h));
        assert(cepheus_gpu_memory_view_get(h,&view)<0 && cepheus_gpu_memory_publish(h)<0);
        ceph_gpu_firmware_view ro={};assert(cepheus_gpu_firmware_get(h,1-kind,&ro)<0);
        assert(!cepheus_gpu_firmware_get(h,kind,&ro) && ro.bytes==fw.size() && !memcmp(ro.data,fw.data(),fw.size()));
        fw.back()^=1;assert(ro.data[ro.bytes-1]!=fw.back()); /* copy, not original mutable storage */
        assert(cepheus_gpu_memory_release_cpu(h)<0);assert(!cepheus_gpu_memory_view_put(h));assert(!cepheus_gpu_memory_release_cpu(h));
    }
    cepheus_gpu_memory_selftest();assert(!mock_io::live && !mock_io::prepared);
    assert(!cepheus_gpu_memory_alloc(4096,&h));assert(!cepheus_gpu_memory_publish(h));
    assert(cepheus_gpu_memory_release_cpu(h)<0 && cepheus_gpu_memory_view_get(h,&view)<0);
    uint64_t bad;assert(!cepheus_gpu_memory_alloc(4096,&bad));mock_io::fail_complete=1;
    assert(cepheus_gpu_memory_release_cpu(bad)<0);mock_io::fail_complete=0;
    assert(cepheus_gpu_memory_release_cpu(bad)<0 && mock_io::live==2 && mock_io::prepared==2);
    puts("Exact XNU memory C++ with host IOKit double: concurrency, budget, sealed snapshots, failure retention PASS (not real IOKit execution)");
}
