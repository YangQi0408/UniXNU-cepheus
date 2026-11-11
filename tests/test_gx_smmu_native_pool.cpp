#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>
#include <IOKit/IOBufferMemoryDescriptor.h>
extern "C" {
#include "cepheus_gmu_workspace.h"
#include "cepheus_cp_memory_private.h"
#include "cepheus_gpu_memory_private.h"
void ceph_test_cp_workspace_restore(struct ceph_cp_workspace *,const struct ceph_gmu_start_config *);
int test_gx_smmu_native_pool(uint64_t,const struct ceph_gmu_workspace *,const struct ceph_smmu_plan *,
    const struct ceph_gx_smmu_plan *,struct ceph_cp_workspace *,unsigned);
}
static std::vector<uint8_t> load(const char *path)
{
    std::ifstream file(path,std::ios::binary);assert(file);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)),{});
}
int main(int argc,char **argv)
{
    assert(argc==4);unsigned mode=(unsigned)std::strtoul(argv[3],nullptr,10);assert(mode<=24);
    auto gmu=load(argv[1]),sqe=load(argv[2]);uint64_t snapshot=0;
    assert(!cepheus_gpu_firmware_snapshot(0,gmu.data(),gmu.size(),&snapshot));
    struct ceph_gmu_workspace w={};assert(!cepheus_gmu_workspace_prepare(&w));
    struct ceph_cp_workspace cp={};assert(!cepheus_cp_workspace_prepare(&cp,sqe.data(),sqe.size()));
    const uint64_t va[]={0xfc000000,0xfc010000,0xfc020000,0xfc030000,0x100000000ULL};
    assert(!cepheus_cp_workspace_table(&cp,va));
    struct ceph_smmu_stream streams[16]={};for(auto &stream:streams)stream.s2cr=2u<<16;
    struct ceph_smmu_plan plan={};assert(!ceph_smmu_gmu_plan(streams,16,2,3,8,5,4,w.table.pa,0,&plan));
    assert(!cepheus_gmu_workspace_share(&w,&plan));streams[2]={plan.smr,plan.s2cr_translate};
    struct ceph_smmu_geometry geometry={};
    assert(!ceph_smmu_geometry((1u<<30)|(1u<<27)|(11u<<9)|16,(2u<<28)|8,0x1222,0x10000,&geometry));
    struct ceph_gx_smmu_plan cpplan={};
    assert(!ceph_gx_smmu_plan(streams,16,&geometry,0,1,2,cp.table.pa,0,&cpplan));
    assert(!test_gx_smmu_native_pool(snapshot,&w,&plan,&cpplan,&cp,mode));
    bool uncertain=mode==4 || mode==5 || mode==6 || (mode>=12 && mode<=20 && mode!=15);
    if(uncertain && mode>=12) {
        assert(!cepheus_gpu_memory_release_cpu(snapshot) && mock_io::live==12);
        for(auto h:cp.handles)assert(cepheus_gpu_memory_release_cpu(h)<0);
        uint64_t refused=123;assert(cepheus_gpu_memory_alloc(4096,&refused)<0 && !refused);
        puts("Actual native CP post-write failure retains CP/HFI backing, unexposed firmware snapshot releasable PASS (synthetic hardware)");return 0;
    }
    assert(!cepheus_gpu_memory_release_cpu(snapshot));
    if(mode==10 || mode==11 || mode==22 || mode==24) {
        assert(!cepheus_cp_workspace_release(&cp) && mock_io::live==6);
        puts("Actual native CP pre-write refusal leaves unexposed CP backing releasable PASS");return 0;
    }
    struct ceph_gpu_group_status status={};
    assert(!cepheus_gpu_memory_group_status(cp.publication.memory.group,&status));
    assert(status.retained_buffers==6 && mock_io::live==12);
    assert(status.pool_terminal==uncertain);
    assert((ceph_memory_cp_validate(&cp.publication)==0)==(mode!=4 && mode!=5 && mode!=6 && mode!=8));
    assert(ceph_memory_retire_begin(&cp.publication.memory)<0 && cepheus_cp_workspace_release(&cp)<0);
    puts("Actual CP publication/native GX SMMU with actual GMU SMMU/upload/CM3/HFI/OOB/GX: PASS (synthetic hardware)");
}
