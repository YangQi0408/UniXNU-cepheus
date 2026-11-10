#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <IOKit/IOBufferMemoryDescriptor.h>
extern "C" {
#include "cepheus_gmu_workspace.h"
#include "cepheus_gmu_upload.h"
#include "cepheus_gpu_memory_private.h"
#include "cepheus_smmu_plan.h"
}
int main(int argc,char **argv)
{
    assert(argc==2);unsigned mode=(unsigned)std::strtoul(argv[1],nullptr,10);assert(mode<=7);
    ceph_gmu_workspace w={};assert(!cepheus_gmu_workspace_prepare(&w));
    ceph_smmu_stream streams[16]={};for(auto &s:streams)s.s2cr=2u<<16;
    ceph_smmu_plan plan={};assert(!ceph_smmu_gmu_plan(streams,16,2,3,8,5,4,w.table.pa,0,&plan));
    assert(!cepheus_gmu_workspace_share(&w,&plan));
    ceph_gmu_start_config config={123,456,789,123,456};ceph_gmu_mapping maps[2]={};
    auto bad=w.publication;bad.handles[0]++;
    assert(ceph_memory_upload_acquire(&bad,&config,maps)<0 && config.chipid==123 && !maps[0].cpu);
    if(mode==0) {
        assert(!ceph_memory_smmu_acquire(&w.publication,&config));
        assert(ceph_memory_upload_acquire(&w.publication,&config,maps)<0);
        assert(!ceph_memory_smmu_finish(&w.publication,CE_MEMORY_SMMU_REFUSED));
    }
    if(mode==1) {
        auto saved=w.table.words[0];w.table.words[0]^=1;
        assert(ceph_memory_upload_acquire(&w.publication,&config,maps)<0);w.table.words[0]=saved;
    }
    assert(!ceph_memory_upload_acquire(&w.publication,&config,maps));
    assert(config.hfi_va==w.layout.start.hfi_va && config.log_va==w.layout.start.log_va);
    assert(maps[0].cpu==w.views[0].cpu && maps[0].va==0x4000 && maps[0].bytes==0x3c000 && maps[0].region==CE_GMU_ICACHE);
    assert(maps[1].cpu==w.views[1].cpu && maps[1].va==0x44000 && maps[1].bytes==0x3c000 && maps[1].region==CE_GMU_DCACHE);
    assert(!ceph_memory_upload_validate(&w.publication));
    assert(ceph_memory_smmu_acquire(&w.publication,&config)<0);
    assert(ceph_memory_upload_acquire(&w.publication,&config,maps)<0);
    assert(ceph_memory_retire_begin(&w.publication)<0 && cepheus_gpu_memory_abandon_workspace(&w.publication)<0);
    if(mode==2) {
        auto saved=w.table.words[0];w.table.words[0]^=1;
        assert(ceph_memory_upload_publish(&w.publication)<0);w.table.words[0]=saved;
    }
    if(mode==3) {
        auto *hfi=(uint32_t *)w.views[2].cpu;auto saved=hfi[0];hfi[0]^=1;
        assert(ceph_memory_upload_publish(&w.publication)<0);hfi[0]=saved;
    }
    if(mode==7) {
        std::thread retire([&w]{for(unsigned i=0;i<1000;++i)assert(ceph_memory_retire_begin(&w.publication)<0);});
        std::thread abandon([&w]{for(unsigned i=0;i<1000;++i)assert(cepheus_gpu_memory_abandon_workspace(&w.publication)<0);});
        for(unsigned i=0;i<1000;++i)assert(!ceph_memory_upload_publish(&w.publication));
        retire.join();abandon.join();
    }
    assert(!ceph_memory_upload_publish(&w.publication));
    bad=w.publication;bad.asid++;
    assert(ceph_memory_upload_finish(&bad,CE_MEMORY_UPLOAD_UNCERTAIN)<0);
    assert(ceph_memory_upload_finish(&w.publication,(ceph_memory_upload_result)99)<0);
    auto result=mode==4?CE_MEMORY_UPLOAD_UNCERTAIN:mode==5?CE_MEMORY_UPLOAD_COPIED:CE_MEMORY_UPLOAD_REFUSED;
    assert(!ceph_memory_upload_finish(&w.publication,result));
    assert(ceph_memory_upload_validate(&w.publication)<0 && ceph_memory_upload_publish(&w.publication)<0);
    assert(ceph_memory_upload_finish(&w.publication,result)<0);
    ceph_gpu_group_status status={};assert(!cepheus_gpu_memory_group_status(w.publication.group,&status));
    assert(status.retained_buffers==6 && mock_io::live==6);
    if(mode==4) {
        assert(status.state==CE_GPU_GROUP_UNCERTAIN && status.pool_terminal);
        assert(ceph_memory_retire_begin(&w.publication)<0 && ceph_memory_smmu_acquire(&w.publication,&config)<0);
    } else {
        assert(status.state==CE_GPU_GROUP_SHARED && !status.pool_terminal);
        assert(!ceph_memory_smmu_acquire(&w.publication,&config));
        assert(!ceph_memory_smmu_finish(&w.publication,CE_MEMORY_SMMU_REFUSED));
        assert(!ceph_memory_retire_begin(&w.publication));assert(ceph_memory_retire_finish(&w.publication,0)<0);
    }
    std::printf("Actual upload pool pin mode=%u: exact cache backing/PTE/HFI, install/retire/abandon exclusion and quarantine PASS (IOKit double)\n",mode);
}
