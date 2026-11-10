#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>
#include <IOKit/IOBufferMemoryDescriptor.h>
extern "C" {
#include "cepheus_cp_workspace.h"
#include "cepheus_gmu_workspace.h"
#include "cepheus_cp_memory_private.h"
#include "cepheus_gpu_memory_private.h"
#include "cepheus_gmu_upload.h"
void ceph_test_cp_workspace_restore(struct ceph_cp_workspace *,const struct ceph_gmu_start_config *);
int ceph_test_cp_preinit(struct ceph_cp_workspace *);
}
int main(int argc,char **argv)
{
    if(argc!=3){puts("CP workspace real-SQE fixture skipped: provide explicit local file and mode");return 0;}
    unsigned mode=(unsigned)std::strtoul(argv[2],nullptr,10);assert(mode<=11);
    std::ifstream input(argv[1],std::ios::binary);assert(input);
    std::vector<uint8_t> fw((std::istreambuf_iterator<char>(input)),{});
    ceph_cp_workspace w={};ceph_gmu_workspace gmu={};uint64_t occupied[12]={};
    if(mode==1)fw.back()^=1;
    if(mode==2)for(auto &h:occupied)assert(!cepheus_gpu_memory_alloc(4096,&h));
    if(mode==3)mock_io::fail_complete=1;
    if(mode && mode<=3) {
        assert(cepheus_cp_workspace_prepare(&w,fw.data(),fw.size())<0);
        if(mode==3){assert(w.state==CE_CP_WS_RETAINED && w.firmware && mock_io::live==7);return 0;}
        assert(w.state==CE_CP_WS_RELEASED);
        if(mode==2)for(auto h:occupied)assert(!cepheus_gpu_memory_release_cpu(h));
        assert(!mock_io::live && !mock_io::maps);return 0;
    }
    assert(!cepheus_gmu_workspace_prepare(&gmu)); /* coexist within same1MiB pool */
    assert(!cepheus_cp_workspace_prepare(&w,fw.data(),fw.size()) && w.state==CE_CP_WS_READY);
    assert(w.sqe.bytes==32300 && w.sqe.version==0x16ee190 && !w.firmware && mock_io::live==12);
    assert(!memcmp(w.views[CE_CP_SQE].cpu,fw.data()+4,fw.size()-4));
    assert(!cepheus_cp_workspace_validate(&w));
    auto *sqe=(uint8_t *)w.views[CE_CP_SQE].cpu;sqe[1024]^=1;
    assert(cepheus_cp_workspace_validate(&w)<0);sqe[1024]^=1;
    sqe[32767]=1;assert(cepheus_cp_workspace_validate(&w)<0);sqe[32767]=0;
    auto saved=w.views[CE_CP_RING];w.views[CE_CP_RING]=w.views[CE_CP_RPTR];
    assert(cepheus_cp_workspace_validate(&w)<0);w.views[CE_CP_RING]=saved;
    for(unsigned role=0;role<CE_CP_BUFFERS;++role) {
        assert(w.views[role].cpu && w.views[role].bytes>=4096);
        auto *p=(uint8_t *)w.views[role].cpu;
        for(size_t i=role==CE_CP_SQE?w.sqe.bytes:0;i<w.views[role].bytes;++i)assert(!p[i]);
        assert(cepheus_gpu_memory_publish(w.handles[role])<0); /* held CPU views, no DMA */
    }
    uint64_t va[]={0xfc000000,0xfc010000,0xfc020000,0xfc030000,0x100000000ULL};
    va[0]=0x100000;assert(cepheus_cp_workspace_table(&w,va)<0 && !w.table.words);va[0]=0xfc000000;
    va[1]=va[0];assert(cepheus_cp_workspace_table(&w,va)<0 && w.state==CE_CP_WS_READY && !w.table.words);va[1]=0xfc010000;
    assert(!cepheus_cp_workspace_table(&w,va) && w.state==CE_CP_WS_TABLED && !ceph_vm39_matches(&w.table,w.regions,CE_CP_ROOT));
    assert(!cepheus_cp_workspace_validate(&w));
    w.table.words[0]^=1;assert(cepheus_cp_workspace_validate(&w)<0);w.table.words[0]^=1;
    for(unsigned i=0;i<CE_CP_ROOT;++i){uint64_t pa,flags;
        assert(!ceph_vm39_lookup(&w.table,va[i],&pa,&flags) && pa==w.views[i].pa);
        if(i==CE_CP_SQE)assert(!(flags&(3ULL<<53)) && (flags&(3u<<6))==(3u<<6));
    }
    assert(cepheus_cp_workspace_table(&w,va)<0);
    assert(cepheus_cp_workspace_prepare(&w,fw.data(),fw.size())<0);
    assert(!ceph_test_cp_preinit(&w));
    ceph_test_cp_workspace_restore(&w,&gmu.layout.start);
    if(mode>=4) {
        struct ceph_smmu_geometry geometry={};
        assert(!ceph_smmu_geometry((1u<<30)|(1u<<27)|(11u<<9)|16,(2u<<28)|8,0x1222,0x10000,&geometry));
        ceph_smmu_stream streams[16]={};for(auto &s:streams)s.s2cr=2u<<16;
        ceph_smmu_plan gp={};assert(!ceph_smmu_gmu_plan(streams,16,2,3,8,5,4,gmu.table.pa,0,&gp));
        assert(!cepheus_gmu_workspace_share(&gmu,&gp));
        streams[2]={gp.smr,gp.s2cr_translate};
        struct ceph_gx_smmu_plan plan={};assert(!ceph_gx_smmu_plan(streams,16,&geometry,0,1,2,w.table.pa,0,&plan));
        if(mode==5) {
            auto old=plan.context.cb;plan.context.cb=plan.context.s2cr_translate=3;
            assert(cepheus_cp_workspace_seal(&w,&plan)<0 && w.state==CE_CP_WS_TABLED);
            plan.context.cb=plan.context.s2cr_translate=old;
        }
        auto duplicate=w.handles[0];w.handles[0]=w.handles[1];
        assert(cepheus_cp_workspace_seal(&w,&plan)<0 && w.state==CE_CP_WS_TABLED);w.handles[0]=duplicate;
        uint64_t old_table=w.table.words[0];w.table.words[0]^=1;
        assert(cepheus_cp_workspace_seal(&w,&plan)<0 && w.state==CE_CP_WS_TABLED);w.table.words[0]=old_table;
        assert(ceph_memory_cp_seal(w.handles,w.views,w.regions,w.table.used,&plan,
            (ceph_cp_publication *)w.views[CE_CP_RING].cpu)<0 && !w.publication.memory.group);
        assert(!cepheus_cp_workspace_seal(&w,&plan) && w.state==CE_CP_WS_SEALED &&
            w.publication.memory.group!=gmu.publication.group && !ceph_memory_cp_validate(&w.publication));
        assert(cepheus_cp_workspace_release(&w)<0 && cepheus_cp_workspace_validate(&w)<0);
        for(auto h:w.handles){ceph_gpu_memory_view view={};
            assert(cepheus_gpu_memory_view_get(h,&view)<0 && cepheus_gpu_memory_view_put(h)<0 &&
                cepheus_gpu_memory_release_cpu(h)<0);}
        ceph_gmu_start_config config={};ceph_gpu_memory_view hfi={};ceph_gmu_mapping maps[2]={};
        assert(ceph_memory_smmu_acquire(&w.publication.memory,&config)<0 &&
            ceph_memory_upload_acquire(&w.publication.memory,&config,maps)<0 &&
            ceph_memory_hfi_acquire(&w.publication.memory,&hfi,&config)<0 &&
            ceph_memory_retire_begin(&w.publication.memory)<0 &&
            cepheus_gpu_memory_abandon_workspace(&w.publication.memory)<0);
        auto copy=w.publication;copy.regions[0].va+=4096;assert(ceph_memory_cp_validate(&copy)<0);
        copy=w.publication;copy.restore_sha256[0]^=1;assert(ceph_memory_cp_validate(&copy)<0);
        auto *restore=(uint32_t *)w.views[CE_CP_RESTORE].cpu;auto value=restore[5];restore[5]^=1;
        assert(ceph_memory_cp_validate(&w.publication)<0);restore[5]=value;
        ceph_cp_memory_view pinned={};pinned.publication.table_used=123;
        assert(ceph_memory_cp_acquire(&copy,&pinned)<0 && pinned.publication.table_used==123);
        assert(ceph_memory_cp_acquire(&w.publication,(ceph_cp_memory_view *)((uint8_t *)&pinned+1))<0 &&
            pinned.publication.table_used==123);
        assert(ceph_memory_cp_acquire(&w.publication,(ceph_cp_memory_view *)w.views[CE_CP_RING].cpu)<0);
        assert(!ceph_memory_cp_acquire(&w.publication,&pinned));
        assert(pinned.views[CE_CP_ROOT].pa==w.table.pa && ceph_memory_cp_acquire(&w.publication,&pinned)<0);
        assert(ceph_memory_cp_finish(&w.publication,-1)<0 && ceph_memory_cp_finish(&w.publication,INT32_MAX)<0);
        if(mode==6) {
            assert(!ceph_memory_cp_finish(&w.publication,CE_MEMORY_CP_REFUSED));
            assert(ceph_memory_cp_publish(&w.publication)<0 && !ceph_memory_cp_acquire(&w.publication,&pinned));
        }
        assert(!ceph_memory_cp_publish(&w.publication) && !ceph_memory_cp_publish(&w.publication));
        std::thread retire([&w]{for(unsigned i=0;i<1000;++i)assert(ceph_memory_retire_begin(&w.publication.memory)<0);});
        std::thread release([&w]{for(unsigned i=0;i<1000;++i)assert(cepheus_gpu_memory_release_cpu(w.handles[0])<0);});
        for(unsigned i=0;i<1000;++i)assert(!ceph_memory_cp_validate(&w.publication));
        retire.join();release.join();
        assert(ceph_memory_cp_finish(&w.publication,CE_MEMORY_CP_REFUSED)<0);
        if(mode>=7) {
            if(mode==7)((uint8_t *)w.views[CE_CP_SQE].cpu)[12]^=1;
            if(mode==8)w.table.words[0]^=1;
            if(mode==9)((uint8_t *)w.views[CE_CP_RESTORE].cpu)[32]^=1;
            if(mode==10)((uint8_t *)w.views[CE_CP_RESTORE].cpu)[1024]=1;
            if(mode<=10)assert(ceph_memory_cp_validate(&w.publication)<0 &&
                ceph_memory_cp_finish(&w.publication,CE_MEMORY_CP_INSTALLED)<0);
            assert(!ceph_memory_cp_finish(&w.publication,CE_MEMORY_CP_UNCERTAIN));
            assert(ceph_memory_cp_validate(&w.publication)<0);
            uint64_t refused=123;assert(cepheus_gpu_memory_alloc(4096,&refused)<0 && !refused);
        }else assert(!ceph_memory_cp_finish(&w.publication,CE_MEMORY_CP_INSTALLED) &&
            ceph_memory_cp_acquire(&w.publication,&pinned)<0 && !ceph_memory_cp_validate(&w.publication));
        ceph_gpu_group_status status={};assert(!cepheus_gpu_memory_group_status(w.publication.memory.group,&status));
        assert(status.retained_buffers==6 && status.pool_terminal==(mode>=7) && mock_io::live==12);
        puts("Actual CP pool seal/pin: identity/SQE/PTE/restore integrity, separate GMU exclusion, no retirement/MMIO PASS");
        return 0;
    }
    assert(!cepheus_cp_workspace_release(&w) && w.state==CE_CP_WS_RELEASED && mock_io::live==6);
    assert(!cepheus_gmu_workspace_release_cpu(&gmu) && !mock_io::live && !mock_io::maps && !mock_io::prepared);
    assert(cepheus_cp_workspace_release(&w)<0);
    puts("CP CPU workspace: real SQE + ring/RPTR/restore/scratch +39bit page tables, coexistence with GMU, zero DMA publication PASS");
}
