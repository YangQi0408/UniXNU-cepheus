#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>
#include <IOKit/IOBufferMemoryDescriptor.h>
extern "C" {
#include "cepheus_gmu_workspace.h"
#include "cepheus_gpu_memory_private.h"
#include "cepheus_smmu_plan.h"
int test_upload_native_pool(uint64_t,const ceph_gpu_publication *,unsigned);
}
int main(int argc,char **argv)
{
    if(argc!=3){std::puts("Native upload+actual pool skipped: supply explicit local GMU image and mode");return 0;}
    unsigned mode=(unsigned)std::strtoul(argv[2],nullptr,10);assert(mode<=5);
    std::ifstream f(argv[1],std::ios::binary);assert(f);std::vector<uint8_t> fw((std::istreambuf_iterator<char>(f)),{});
    uint64_t snapshot=0;assert(!cepheus_gpu_firmware_snapshot(0,fw.data(),fw.size(),&snapshot));
    ceph_gmu_workspace w={};assert(!cepheus_gmu_workspace_prepare(&w));
    ceph_smmu_stream streams[16]={};for(auto &s:streams)s.s2cr=2u<<16;
    ceph_smmu_plan plan={};assert(!ceph_smmu_gmu_plan(streams,16,2,3,8,5,4,w.table.pa,0,&plan));
    assert(!cepheus_gmu_workspace_share(&w,&plan));
    uint64_t handle=snapshot;if(mode==5)handle++;
    int e=test_upload_native_pool(handle,&w.publication,mode);assert((e==0)==(mode==0));
    assert(!cepheus_gpu_memory_release_cpu(snapshot));
    ceph_gpu_group_status status={};assert(!cepheus_gpu_memory_group_status(w.publication.group,&status));
    assert(status.retained_buffers==6 && mock_io::live==6);
    if(mode==2)assert(status.state==CE_GPU_GROUP_UNCERTAIN && status.pool_terminal);
    else {
        assert(status.state==CE_GPU_GROUP_SHARED && !status.pool_terminal);
        assert(!ceph_memory_retire_begin(&w.publication));assert(ceph_memory_retire_finish(&w.publication,0)<0);
    }
    std::printf("Actual native upload+sealed firmware+actual pool mode=%u PASS (emulated registers/IOKit, not device)\n",mode);
}
