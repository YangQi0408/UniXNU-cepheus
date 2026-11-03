#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include <IOKit/IOBufferMemoryDescriptor.h>
extern "C" {
#include "cepheus_cp_workspace.h"
#include "cepheus_gmu_workspace.h"
}
int main(int argc,char **argv)
{
    if(argc!=3){puts("CP workspace real-SQE fixture skipped: provide explicit local file and mode");return 0;}
    unsigned mode=(unsigned)std::strtoul(argv[2],nullptr,10);assert(mode<=3);
    std::ifstream input(argv[1],std::ios::binary);assert(input);
    std::vector<uint8_t> fw((std::istreambuf_iterator<char>(input)),{});
    ceph_cp_workspace w={};ceph_gmu_workspace gmu={};uint64_t occupied[12]={};
    if(mode==1)fw.back()^=1;
    if(mode==2)for(auto &h:occupied)assert(!cepheus_gpu_memory_alloc(4096,&h));
    if(mode==3)mock_io::fail_complete=1;
    if(mode) {
        assert(cepheus_cp_workspace_prepare(&w,fw.data(),fw.size())<0);
        if(mode==3){assert(w.state==CE_CP_WS_RETAINED && w.firmware && mock_io::live==6);return 0;}
        assert(w.state==CE_CP_WS_RELEASED);
        if(mode==2)for(auto h:occupied)assert(!cepheus_gpu_memory_release_cpu(h));
        assert(!mock_io::live && !mock_io::maps);return 0;
    }
    assert(!cepheus_gmu_workspace_prepare(&gmu)); /* coexist within same1MiB pool */
    assert(!cepheus_cp_workspace_prepare(&w,fw.data(),fw.size()) && w.state==CE_CP_WS_READY);
    assert(w.sqe.bytes==32300 && w.sqe.version==0x16ee190 && !w.firmware && mock_io::live==11);
    assert(!memcmp(w.views[CE_CP_SQE].cpu,fw.data()+4,fw.size()-4));
    for(unsigned role=0;role<CE_CP_BUFFERS;++role) {
        assert(w.views[role].cpu && w.views[role].bytes>=4096);
        auto *p=(uint8_t *)w.views[role].cpu;
        for(size_t i=role==CE_CP_SQE?w.sqe.bytes:0;i<w.views[role].bytes;++i)assert(!p[i]);
        assert(cepheus_gpu_memory_publish(w.handles[role])<0); /* held CPU views, no DMA */
    }
    assert(cepheus_cp_workspace_prepare(&w,fw.data(),fw.size())<0);
    assert(!cepheus_cp_workspace_release(&w) && w.state==CE_CP_WS_RELEASED && mock_io::live==6);
    assert(!cepheus_gmu_workspace_release_cpu(&gmu) && !mock_io::live && !mock_io::maps && !mock_io::prepared);
    assert(cepheus_cp_workspace_release(&w)<0);
    puts("CP CPU workspace: real sealed SQE payload + ring/RPTR/restore/scratch, coexistence with GMU, zero DMA publication PASS");
}
