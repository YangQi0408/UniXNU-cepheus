#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>
#include <IOKit/IOBufferMemoryDescriptor.h>
extern "C" {
#include "cepheus_cp_memory_api.h"
#include "cepheus_gmu_workspace.h"
void ceph_test_cp_workspace_restore(struct ceph_cp_workspace *,const struct ceph_gmu_start_config *);
}
int main(int argc,char **argv)
{
    assert(argc==3);unsigned mode=(unsigned)std::strtoul(argv[2],nullptr,10);assert(mode<=9);
    std::ifstream input(argv[1],std::ios::binary);assert(input);
    std::vector<uint8_t> fw((std::istreambuf_iterator<char>(input)),{});
    ceph_cp_workspace w={};ceph_gmu_workspace gmu={};
    assert(!cepheus_gmu_workspace_prepare(&gmu));
    assert(!cepheus_cp_workspace_prepare(&w,fw.data(),fw.size()));
    const uint64_t va[]={0xfc000000,0xfc010000,0xfc020000,0xfc030000,0x100000000ULL};
    assert(!cepheus_cp_workspace_table(&w,va));
    ceph_cp_publication output={};
    assert(!ceph_memory_cp_output_safe(&output,sizeof(output)));
    for(auto view:gmu.views)assert(ceph_memory_cp_output_safe(view.cpu,sizeof(output))<0);
    for(auto view:w.views)assert(ceph_memory_cp_output_safe(view.cpu,sizeof(output))<0);
    assert(ceph_memory_cp_output_safe((void *)(UINTPTR_MAX-7),sizeof(output))<0);
    ceph_test_cp_workspace_restore(&w,&gmu.layout.start);
    assert(ceph_memory_cp_build_acquire(&w)<0);w.restore_ready=0;
    assert(!ceph_memory_cp_build_acquire(&w) && ceph_memory_cp_build_acquire(&w)<0);
    assert(!ceph_memory_cp_build_validate(&w));
    for(auto h:w.handles) {
        ceph_gpu_memory_view view={};
        assert(cepheus_gpu_memory_view_get(h,&view)<0 && cepheus_gpu_memory_view_put(h)<0 &&
            cepheus_gpu_memory_publish(h)<0 && cepheus_gpu_memory_release_cpu(h)<0);
    }
    auto changed=w;
    changed.sqe.version^=1;assert(ceph_memory_cp_build_validate(&changed)<0);
    changed=w;changed.views[CE_CP_ROOT].bytes=4096;assert(ceph_memory_cp_build_validate(&changed)<0);
    changed=w;changed.regions[0].bytes+=4096;assert(ceph_memory_cp_build_validate(&changed)<0);
    changed=w;changed.restore_ready=1;assert(ceph_memory_cp_build_validate(&changed)<0);
    std::thread blocked([&w]{for(unsigned i=0;i<100;++i)assert(cepheus_gpu_memory_view_put(w.handles[0])<0);});
    for(unsigned i=0;i<100;++i)assert(!ceph_memory_cp_build_validate(&w));
    blocked.join();
    ceph_smmu_stream streams[16]={};for(auto &s:streams)s.s2cr=2u<<16;
    struct ceph_smmu_geometry geometry={};
    assert(!ceph_smmu_geometry((1u<<30)|(1u<<27)|(11u<<9)|16,(2u<<28)|8,0x1222,0x10000,&geometry));
    struct ceph_gx_smmu_plan plan={};assert(!ceph_gx_smmu_plan(streams,16,&geometry,0,1,2,w.table.pa,0,&plan));
    ceph_cp_publication p={};p.table_used=123;
    assert(ceph_memory_cp_seal(w.handles,w.views,w.regions,w.table.used,&plan,&p)<0 && p.table_used==123);
    assert(ceph_memory_cp_build_seal(&w,&plan,(ceph_cp_publication *)w.views[0].cpu)<0);
    assert(ceph_memory_cp_build_seal(&w,&plan,&w.publication)<0 && !w.publication.memory.group);
    if(mode==1) {
        changed=w;changed.regions[0].va+=0x1000000;
        changed.table.used=0;
        assert(!ceph_vm39_build(&changed.table,changed.regions,CE_CP_ROOT));
        assert(!ceph_vm39_matches(&changed.table,changed.regions,CE_CP_ROOT));
        assert(ceph_memory_cp_build_validate(&changed)<0);
    }
    if(mode==2)((uint8_t *)w.views[CE_CP_SQE].cpu)[32767]=1;
    if(mode==3)((uint8_t *)w.views[CE_CP_SQE].cpu)[12]^=1;
    if(mode==4)w.table.words[0]^=1;
    if(mode==5)((uint8_t *)w.views[CE_CP_RESTORE].cpu)[1024]=1;
    if(mode==6)((uint32_t *)w.views[CE_CP_RESTORE].cpu)[13]=0;
    if(mode==7)w.sqe.words++;
    if(mode==8)w.views[CE_CP_RING].bytes=4096;
    if(mode==9) {
        assert(!ceph_memory_cp_build_finish(&w,0) && ceph_memory_cp_build_validate(&w)<0);
        assert(!ceph_memory_cp_build_acquire(&w));
    }
    if(mode && mode!=9) {
        assert(ceph_memory_cp_build_seal(&w,&plan,&p)<0 && p.table_used==123);
        assert(!ceph_memory_cp_build_finish(&w,1));
        uint64_t h=123;assert(cepheus_gpu_memory_alloc(4096,&h)<0 && !h);
        assert(mock_io::live==12);
    }else {
        assert(!ceph_memory_cp_build_seal(&w,&plan,&p));
        assert(p.memory.group && !ceph_memory_cp_validate(&p));
        assert(ceph_memory_cp_build_finish(&w,0)<0 && ceph_memory_cp_build_validate(&w)<0);
        assert(mock_io::live==12);
    }
    puts("Actual CP build pin: immutable layout, blocked ordinary access, atomic seal, quarantine PASS (CPU-only fixture)");
}
