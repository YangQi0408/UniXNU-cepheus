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
int test_hfi_native_pool(uint64_t,const ceph_gmu_workspace *,const ceph_smmu_plan *,unsigned);
}
int main(int argc,char **argv)
{
    if(argc!=3){std::puts("HFI native actual-pool chain skipped: supply explicit local GMU image and mode");return 0;}
    unsigned mode=(unsigned)std::strtoul(argv[2],nullptr,10);assert(mode<=30);
    std::ifstream f(argv[1],std::ios::binary);assert(f);
    std::vector<uint8_t> fw((std::istreambuf_iterator<char>(f)),{});
    uint64_t snapshot=0;assert(!cepheus_gpu_firmware_snapshot(0,fw.data(),fw.size(),&snapshot));
    ceph_gmu_workspace w={};assert(!cepheus_gmu_workspace_prepare(&w));
    ceph_smmu_stream streams[16]={};for(auto &s:streams)s.s2cr=2u<<16;
    ceph_smmu_plan plan={};assert(!ceph_smmu_gmu_plan(streams,16,2,3,8,5,4,w.table.pa,0,&plan));
    assert(!cepheus_gmu_workspace_share(&w,&plan));
    int e=test_hfi_native_pool(snapshot,&w,&plan,mode);assert((e==0)==(mode==0 || mode==8 || (mode>=9 && mode<27)));
    assert(!cepheus_gpu_memory_release_cpu(snapshot));
    ceph_gpu_group_status status={};assert(!cepheus_gpu_memory_group_status(w.publication.group,&status));
    assert(status.retained_buffers==6 && mock_io::live==6);
    if(mode==1 || mode==5 || mode==6 || mode>=27) {
        assert(status.state==CE_GPU_GROUP_SHARED && !status.pool_terminal);
        assert(ceph_memory_hfi_validate(&w.publication)<0);
        assert(!ceph_memory_retire_begin(&w.publication));assert(ceph_memory_retire_finish(&w.publication,0)<0);
    } else {
        assert(ceph_memory_retire_begin(&w.publication)<0 && cepheus_gpu_memory_abandon_workspace(&w.publication)<0);
        bool healthy=mode==0 || mode==8 || mode==9 || mode==10 || mode==16 || mode==18 || mode==21 || mode==22 || mode==24 || mode==25;
        assert((ceph_memory_hfi_validate(&w.publication)==0)==healthy);
        assert(status.pool_terminal==!healthy);
        assert(status.state==(healthy?CE_GPU_GROUP_SHARED:CE_GPU_GROUP_UNCERTAIN));
    }
}
