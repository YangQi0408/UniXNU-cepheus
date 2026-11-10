#include <cassert>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <IOKit/IOBufferMemoryDescriptor.h>
extern "C" {
#include "cepheus_gmu_workspace.h"
#include "cepheus_gpu_memory_private.h"
#include "cepheus_smmu_plan.h"
#include "cepheus_hfi.h"
#include "cepheus_gmu_upload.h"
}
int main(int argc,char **argv)
{
    assert(argc==2);unsigned mode=(unsigned)std::strtoul(argv[1],nullptr,10);assert(mode<=6);
    ceph_gmu_workspace w={};assert(!cepheus_gmu_workspace_prepare(&w));
    ceph_smmu_stream streams[16]={};for(auto &s:streams)s.s2cr=2u<<16;
    ceph_smmu_plan plan={};assert(!ceph_smmu_gmu_plan(streams,16,2,3,8,5,4,w.table.pa,0,&plan));
    assert(!cepheus_gmu_workspace_share(&w,&plan));
    ceph_gpu_memory_view view={};view.pa=123;
    ceph_gmu_start_config config={};config.chipid=456;
    auto bad=w.publication;bad.ttbr1^=4096;
    assert(ceph_memory_hfi_acquire(&bad,&view,&config)<0 && view.pa==123 && config.chipid==456);
    assert(ceph_memory_hfi_validate(&w.publication)<0 && ceph_memory_hfi_publish(&w.publication)<0);
    assert(ceph_memory_hfi_seal(&w.publication)<0 && ceph_memory_hfi_finish(&w.publication,CE_MEMORY_HFI_REFUSED)<0);
    if(mode==0 || mode==1) {
        ceph_gmu_mapping upload_maps[2]={};
        assert(!(mode?ceph_memory_upload_acquire(&w.publication,&config,upload_maps):
            ceph_memory_smmu_acquire(&w.publication,&config)));
        assert(ceph_memory_hfi_acquire(&w.publication,&view,&config)<0);
        assert(!(mode?ceph_memory_upload_finish(&w.publication,CE_MEMORY_UPLOAD_REFUSED):
            ceph_memory_smmu_finish(&w.publication,CE_MEMORY_SMMU_REFUSED)));
    }
    if(mode==2) {
        auto table=(uint32_t *)w.views[2].cpu;table[17]=4;
        assert(ceph_memory_hfi_acquire(&w.publication,&view,&config)<0);table[17]=0;
    }
    assert(!ceph_memory_hfi_acquire(&w.publication,&view,&config));
    assert(view.cpu==w.views[2].cpu && view.pa==w.views[2].pa && view.bytes==w.views[2].bytes &&
        config.hfi_va==w.layout.start.hfi_va && config.hfi_bytes==20480);
    assert(!ceph_memory_hfi_validate(&w.publication) && !ceph_memory_hfi_publish(&w.publication));
    assert(ceph_memory_hfi_acquire(&w.publication,&view,&config)<0);
    assert(ceph_memory_smmu_acquire(&w.publication,&config)<0);
    ceph_gmu_mapping maps[2]={};assert(ceph_memory_upload_acquire(&w.publication,&config,maps)<0);
    assert(ceph_memory_retire_begin(&w.publication)<0 && cepheus_gpu_memory_abandon_workspace(&w.publication)<0);
    for(auto h:w.handles)assert(cepheus_gpu_memory_view_put(h)<0 && cepheus_gpu_memory_release_cpu(h)<0);
    for(int code:{99,-1,INT_MIN,INT_MAX})assert(ceph_memory_hfi_finish(&w.publication,code)<0);
    assert(!ceph_memory_hfi_validate(&w.publication));
    if(mode==3) {
        assert(ceph_memory_hfi_finish(&w.publication,CE_MEMORY_HFI_RETAINED)<0);
        assert(!ceph_memory_hfi_finish(&w.publication,CE_MEMORY_HFI_REFUSED));
        assert(ceph_memory_hfi_validate(&w.publication)<0);
        assert(!ceph_memory_retire_begin(&w.publication));
        assert(ceph_memory_retire_finish(&w.publication,0)<0);
    } else {
        assert(!ceph_memory_hfi_seal(&w.publication));
        assert(ceph_memory_hfi_seal(&w.publication)<0);
        assert(ceph_memory_hfi_finish(&w.publication,CE_MEMORY_HFI_REFUSED)<0);
        if(mode==6) {
            std::thread retire([&]{for(unsigned i=0;i<1000;++i)assert(ceph_memory_retire_begin(&w.publication)<0);});
            std::thread abandon([&]{for(unsigned i=0;i<1000;++i)assert(cepheus_gpu_memory_abandon_workspace(&w.publication)<0);});
            for(unsigned i=0;i<1000;++i)assert(!ceph_memory_hfi_publish(&w.publication));
            retire.join();abandon.join();
        }
        auto table=(uint32_t *)view.cpu;table[17]=4;
        assert(!ceph_memory_hfi_validate(&w.publication) && !ceph_memory_hfi_publish(&w.publication));
        assert(!ceph_memory_hfi_finish(&w.publication,mode==4?CE_MEMORY_HFI_UNCERTAIN:CE_MEMORY_HFI_RETAINED));
        assert(ceph_memory_hfi_finish(&w.publication,CE_MEMORY_HFI_RETAINED)<0);
        assert(ceph_memory_retire_begin(&w.publication)<0 && cepheus_gpu_memory_abandon_workspace(&w.publication)<0);
        assert((ceph_memory_hfi_validate(&w.publication)==0)==(mode!=4));
    }
    ceph_gpu_group_status status={};assert(!cepheus_gpu_memory_group_status(w.publication.group,&status));
    assert(status.retained_buffers==6 && mock_io::live==6);
    if(mode==3 || mode==4)assert(status.pool_terminal && status.state==CE_GPU_GROUP_UNCERTAIN);
    else assert(!status.pool_terminal && status.state==CE_GPU_GROUP_SHARED);
    std::printf("Actual HFI pool pin mode=%u: exact queue backing/cold header, install/upload/retire exclusion, permanent seal, raw status refusal and uncertain retention PASS (IOKit double)\n",mode);
}
