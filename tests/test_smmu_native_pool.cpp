#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <IOKit/IOBufferMemoryDescriptor.h>
extern "C" {
#include "cepheus_gmu_workspace.h"
#include "cepheus_gpu_memory_private.h"
#include "cepheus_smmu_plan.h"
int test_smmu_native_pool(const ceph_smmu_plan *,const ceph_gpu_publication *,unsigned);
}
int main(int argc,char **argv)
{
    assert(argc==2);unsigned mode=(unsigned)std::strtoul(argv[1],nullptr,10);assert(mode<=5);
    ceph_gmu_workspace w={};assert(!cepheus_gmu_workspace_prepare(&w));
    ceph_smmu_stream streams[16]={};for(auto &s:streams)s.s2cr=2u<<16;
    ceph_smmu_plan plan={};assert(!ceph_smmu_gmu_plan(streams,16,2,3,8,5,4,w.table.pa,0,&plan));
    assert(!cepheus_gmu_workspace_share(&w,&plan));
    uint64_t original=w.table.words[0];if(mode==5)w.table.words[0]^=1;
    int e=test_smmu_native_pool(&plan,&w.publication,mode);assert((e==0)==(mode<2));
    ceph_gpu_group_status status={};assert(!cepheus_gpu_memory_group_status(w.publication.group,&status));
    assert(status.retained_buffers==6 && mock_io::live==6);
    if(mode==3 || mode==4) {
        assert(status.state==CE_GPU_GROUP_UNCERTAIN && status.pool_terminal);
        assert(ceph_memory_retire_begin(&w.publication)<0);
        uint64_t handle=123;assert(cepheus_gpu_memory_alloc(4096,&handle)<0);
    } else {
        assert(status.state==CE_GPU_GROUP_SHARED && !status.pool_terminal);
        if(mode==5)w.table.words[0]=original;
        ceph_gmu_start_config config={};assert(!ceph_memory_smmu_acquire(&w.publication,&config));
        assert(!ceph_memory_smmu_finish(&w.publication,CE_MEMORY_SMMU_REFUSED));
        assert(!ceph_memory_retire_begin(&w.publication));assert(ceph_memory_retire_finish(&w.publication,0)<0);
    }
    std::printf("Actual native SMMU + actual C++ pool mode=%u: prepared root, real group pin/publication and refusal/uncertainty retention PASS (emulated registers/IOKit)\n",mode);
}
