#include <cassert>
#include <cstdio>
#include <IOKit/IOBufferMemoryDescriptor.h>
extern "C" {
#include "cepheus_gmu_workspace.h"
}
int main(void)
{
    ceph_gmu_workspace w={};
    assert(!cepheus_gmu_workspace_prepare(&w) && w.state==CE_WS_READY_CPU && w.table.used==5);
    assert(mock_io::live==5 && w.leased==31);
    assert(w.views[2].bytes==32768 && w.layout.regions[2].bytes==20480);
    assert(cepheus_gpu_memory_publish(w.handles[0])<0); /* workspace CPU leases still held */
    assert(cepheus_gmu_workspace_prepare(&w)<0);
    assert(!cepheus_gmu_workspace_release_cpu(&w) && !mock_io::live && !mock_io::prepared);
    for(unsigned occupied=12;occupied<=16;++occupied) {
        uint64_t handles[16];for(unsigned i=0;i<occupied;++i)assert(!cepheus_gpu_memory_alloc(4096,&handles[i]));
        unsigned before=mock_io::live;w={};assert(cepheus_gmu_workspace_prepare(&w)<0 && mock_io::live==before);
        for(unsigned i=0;i<occupied;++i)assert(!cepheus_gpu_memory_release_cpu(handles[i]));
    }
    mock_io::bad_segment=1;w={};assert(cepheus_gmu_workspace_prepare(&w)<0 && !mock_io::live);mock_io::bad_segment=0;
    cepheus_gmu_workspace_selftest();assert(!mock_io::live && !mock_io::prepared);
    w={};assert(!cepheus_gmu_workspace_prepare(&w));mock_io::fail_complete=1;
    assert(cepheus_gmu_workspace_release_cpu(&w)<0 && w.state==CE_WS_RETAINED && mock_io::live==5);
    assert(!w.table.words && !w.table.used && !w.layout.start.hfi_va && !mock_io::raw_views);
    puts("Real C workspace + real C++ pool with IOKit double: NC backing->VA->page tables->HFI and failure cleanup PASS");
}
