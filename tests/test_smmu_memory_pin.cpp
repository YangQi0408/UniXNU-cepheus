#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <climits>
#include <IOKit/IOBufferMemoryDescriptor.h>
extern "C" {
#include "cepheus_gmu_workspace.h"
#include "cepheus_gpu_memory_private.h"
#include "cepheus_smmu_plan.h"
}
int main(int argc,char **argv)
{
    assert(argc==2);unsigned mode=(unsigned)std::strtoul(argv[1],nullptr,10);assert(mode<=7);
    ceph_gmu_workspace w={};assert(!cepheus_gmu_workspace_prepare(&w));
    ceph_smmu_stream streams[16]={};for(auto &s:streams)s.s2cr=2u<<16;
    ceph_smmu_plan plan={};
    assert(!ceph_smmu_gmu_plan(streams,16,2,3,8,5,4,w.table.pa,0,&plan));
    assert(!cepheus_gmu_workspace_share(&w,&plan));
    ceph_gmu_start_config config={123,456,789,123,456};
    if(mode==0) {
        auto bad=w.publication;bad.handles[0]++;
        assert(ceph_memory_smmu_acquire(&bad,&config)<0 && config.chipid==123);
        bad=w.publication;bad.ttbr0+=4096;
        assert(ceph_memory_smmu_acquire(&bad,&config)<0 && config.chipid==123);
        assert(ceph_memory_smmu_publish(&w.publication)<0);
        assert(ceph_memory_smmu_finish(&w.publication,CE_MEMORY_SMMU_REFUSED)<0);
    }
    uint64_t original=w.table.words[0];
    if(mode==1) {
        w.table.words[0]^=1;
        assert(ceph_memory_smmu_acquire(&w.publication,&config)<0 && config.chipid==123);
        w.table.words[0]=original;
    }
    assert(!ceph_memory_smmu_acquire(&w.publication,&config));
    assert(!ceph_memory_smmu_validate(&w.publication));
    assert(config.chipid==w.layout.start.chipid && config.hfi_va==w.layout.start.hfi_va &&
        config.hfi_bytes==20480 && config.log_va==w.layout.start.log_va && config.log_bytes==4096);
    assert(ceph_memory_smmu_acquire(&w.publication,&config)<0);
    assert(ceph_memory_retire_begin(&w.publication)<0);
    assert(cepheus_gpu_memory_abandon_workspace(&w.publication)<0);
    assert(cepheus_gmu_workspace_release_cpu(&w)<0);
    for(auto h:w.handles) {
        ceph_gpu_memory_view view={};
        assert(cepheus_gpu_memory_view_get(h,&view)<0 && !view.cpu);
        assert(cepheus_gpu_memory_view_put(h)<0 && cepheus_gpu_memory_release_cpu(h)<0);
    }
    if(mode==2) {
        w.table.words[0]^=1;assert(ceph_memory_smmu_publish(&w.publication)<0);
        w.table.words[0]=original;
    }
    if(mode==3) {
        auto table=(volatile uint32_t *)w.views[2].cpu;auto saved=table[0];table[0]^=1;
        assert(ceph_memory_smmu_publish(&w.publication)<0);table[0]=saved;
    }
    if(mode==7) {
        std::thread retire([&w]{for(unsigned i=0;i<1000;++i)assert(ceph_memory_retire_begin(&w.publication)<0);});
        std::thread abandon([&w]{for(unsigned i=0;i<1000;++i)assert(cepheus_gpu_memory_abandon_workspace(&w.publication)<0);});
        for(unsigned i=0;i<1000;++i)assert(!ceph_memory_smmu_publish(&w.publication));
        retire.join();abandon.join();
    }
    assert(!ceph_memory_smmu_publish(&w.publication));
    auto bad=w.publication;bad.asid++;
    assert(ceph_memory_smmu_finish(&bad,CE_MEMORY_SMMU_UNCERTAIN)<0);
    for(int code:{99,-1,INT_MIN,INT_MAX})assert(ceph_memory_smmu_finish(&w.publication,code)<0);
    assert(!ceph_memory_smmu_validate(&w.publication));
    ceph_memory_smmu_result result=mode==4?CE_MEMORY_SMMU_UNCERTAIN:
        mode==5?CE_MEMORY_SMMU_INSTALLED:CE_MEMORY_SMMU_REFUSED;
    assert(!ceph_memory_smmu_finish(&w.publication,result));
    assert(ceph_memory_smmu_validate(&w.publication)<0);
    assert(ceph_memory_smmu_finish(&w.publication,result)<0);
    ceph_gpu_group_status status={};assert(!cepheus_gpu_memory_group_status(w.publication.group,&status));
    assert(status.retained_buffers==6 && mock_io::live==6);
    if(mode==4) {
        assert(status.state==CE_GPU_GROUP_UNCERTAIN && status.pool_terminal);
        uint64_t handle=123;assert(cepheus_gpu_memory_alloc(4096,&handle)<0);
        assert(ceph_memory_retire_begin(&w.publication)<0 && ceph_memory_smmu_acquire(&w.publication,&config)<0);
    } else {
        assert(status.state==CE_GPU_GROUP_SHARED && !status.pool_terminal);
        assert(!ceph_memory_smmu_acquire(&w.publication,&config));
        assert(!ceph_memory_smmu_finish(&w.publication,CE_MEMORY_SMMU_REFUSED));
        assert(!ceph_memory_retire_begin(&w.publication));
        assert(ceph_memory_retire_finish(&w.publication,0)<0);
    }
    std::printf("Actual six-buffer SMMU pin mode=%u: pool identities/PTEs/HFI, refusal/installed/uncertain lifecycle and retirement exclusion PASS (IOKit double)\n",mode);
}
