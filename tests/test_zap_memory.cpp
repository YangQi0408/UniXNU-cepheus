#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include "cepheus_gpu_firmware.h"
#include "cepheus_zap_memory.h"
extern "C" {
#include "cepheus_gmu_workspace.h"
#include "cepheus_smmu_plan.h"
#include "cepheus_cp_memory_api.h"
}
static uint64_t snapshot;
static ceph_gmu_workspace gpu={};
static std::vector<uint8_t> read_file(const char *path)
{
    std::ifstream f(path,std::ios::binary);assert(f);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),{});
}
static void during_allocation()
{
    alignas(8) uint64_t out=123;assert(ceph_memory_cp_output_safe(&out,sizeof(out))<0);
    assert(cepheus_gpu_memory_release_cpu(snapshot)<0);
    unsigned before=mock_io::alloc_calls;uint64_t other=123;
    assert(cepheus_zap_memory_prepare(snapshot,&other)<0 && !other);
    assert(mock_io::alloc_calls==before);
}
static void abandon_during_allocation()
{
    mock_io::after_allocate=nullptr;
    assert(!cepheus_gpu_memory_abandon_workspace(&gpu.publication));
}
static void assert_source_only()
{assert(mock_io::live==1 && mock_io::prepared==1 && mock_io::maps==1);}

int main(int argc,char **argv)
{
    uint64_t h=123;ceph_zap_memory_view v={};
    assert(cepheus_zap_memory_prepare(0,&h)<0 && !h);
    assert(cepheus_zap_memory_get(0,&v)<0 && !v.image && !v.image_pa);
    assert(cepheus_zap_memory_put(0)<0 && cepheus_zap_memory_release_cpu(0)<0);
    assert(!cepheus_gpu_memory_alloc(4096,&h));
    uint64_t other=123;
    assert(cepheus_zap_memory_prepare(h,&other)<0 && !other);
    assert(cepheus_zap_memory_get(h,&v)<0 && cepheus_zap_memory_release_cpu(h)<0);
    assert(!cepheus_gpu_memory_release_cpu(h));
    if(argc!=3){puts("ZAP workspace invalid/unsealed handle rejection PASS; SKIP real lifecycle (supply CEPHEUS_ZAP_ELF)");return 0;}
    int mode=atoi(argv[2]);assert(mode>=0 && mode<=4);
    auto elf=read_file(argv[1]);
    assert(!cepheus_gpu_firmware_snapshot(CE_FW_A640_ZAP,elf.data(),elf.size(),&snapshot));
    if(mode==2 || mode==3) {
        if(mode==2)mock_io::fail_alloc_at=mock_io::alloc_calls+2;
        else mock_io::fail_map_at=mock_io::map_calls+2;
        mock_io::fail_complete=1;
        assert(cepheus_zap_memory_prepare(snapshot,&h)<0 && !h);
        mock_io::fail_complete=0;mock_io::fail_alloc_at=0;mock_io::fail_map_at=0;
        assert(!cepheus_gpu_memory_release_cpu(snapshot));
        assert(mock_io::live==(mode==2?1u:2u));
        assert(!cepheus_gpu_firmware_snapshot(CE_FW_A640_ZAP,elf.data(),elf.size(),&snapshot));
        unsigned calls=mock_io::alloc_calls;
        assert(cepheus_zap_memory_prepare(snapshot,&h)<0 && !h && mock_io::alloc_calls==calls);
        assert(!cepheus_gpu_memory_release_cpu(snapshot));
        puts("ZAP partial failure retains uncertain backing and blocks replacement PASS");return 0;
    }
    if(mode==4) {
        assert(!cepheus_gmu_workspace_prepare(&gpu));
        ceph_smmu_stream streams[8]={};for(auto &s:streams)s.s2cr=2u<<16;
        ceph_smmu_plan plan;
        assert(!ceph_smmu_gmu_plan(streams,8,2,3,8,5,4,gpu.table.pa,0,&plan));
        assert(!cepheus_gmu_workspace_share(&gpu,&plan));
        unsigned before=mock_io::live;
        mock_io::after_allocate=abandon_during_allocation;
        assert(cepheus_zap_memory_prepare(snapshot,&h)<0 && !h && mock_io::live==before);
        unsigned calls=mock_io::alloc_calls;
        assert(cepheus_zap_memory_prepare(snapshot,&h)<0 && !h && mock_io::alloc_calls==calls);
        assert(!cepheus_gpu_memory_release_cpu(snapshot));
        assert(mock_io::live==6);
        puts("ZAP allocation racing GPU abandonment discards private pair and refuses admission PASS");return 0;
    }
    if(!mode) {
        for(unsigned allocation=1;allocation<=2;++allocation) {
            mock_io::fail_alloc_at=mock_io::alloc_calls+allocation;
            assert(cepheus_zap_memory_prepare(snapshot,&h)<0 && !h);assert_source_only();
        }
        mock_io::fail_alloc_at=0;
        for(unsigned mapping=1;mapping<=2;++mapping) {
            mock_io::fail_map_at=mock_io::map_calls+mapping;
            assert(cepheus_zap_memory_prepare(snapshot,&h)<0 && !h);assert_source_only();
        }
        mock_io::fail_map_at=0;
        for(auto *fault:{&mock_io::fail_prepare,&mock_io::bad_segment,&mock_io::bad_cache,&mock_io::bad_pmap_cache}) {
            *fault=1;assert(cepheus_zap_memory_prepare(snapshot,&h)<0 && !h);*fault=0;assert_source_only();
        }
        unsigned maps=mock_io::map_calls;
        mock_io::bad_large_alignment=1;
        assert(cepheus_zap_memory_prepare(snapshot,&h)<0 && !h && mock_io::map_calls==maps+2);
        mock_io::bad_large_alignment=0;assert_source_only();
    }
    mock_io::after_allocate=during_allocation;
    assert(!cepheus_zap_memory_prepare(snapshot,&h));
    mock_io::after_allocate=nullptr;
    assert(!cepheus_gpu_memory_release_cpu(snapshot));
    assert(!cepheus_zap_memory_get(h,&v));
    alignas(8) uint64_t out=123;
    assert(!ceph_memory_cp_output_safe(&out,sizeof(out)));
    assert(ceph_memory_cp_output_safe(const_cast<const uint8_t *>(v.mdt),8)<0 && ceph_memory_cp_output_safe(v.image,8)<0);
    assert(ceph_memory_cp_output_safe(v.image+v.image_capacity-8,8)<0);
    assert(v.image_capacity==1048576 && !(v.image_pa&1048575));
    assert(v.mdt_capacity==16384 && v.mdt_bytes==6860 && v.logical_bytes==4096 && !v.entry_offset);
    std::string base=argv[1];base.resize(base.size()-4);
    auto mdt=read_file((base+".mdt").c_str()),load=read_file((base+".b02").c_str());
    assert(mdt.size()==v.mdt_bytes);
    for(size_t i=0;i<mdt.size();++i)assert(v.mdt[i]==mdt[i]);
    assert(!memcmp(v.image,load.data(),load.size()));
    assert(std::all_of(v.mdt+mdt.size(),v.mdt+v.mdt_capacity,[](uint8_t b){return !b;}));
    assert(std::all_of(v.image+load.size(),v.image+v.image_capacity,[](uint8_t b){return !b;}));
    ceph_gpu_memory_view generic={};
    assert(cepheus_gpu_memory_view_get(h,&generic)<0 && cepheus_gpu_memory_publish(h)<0);
    assert(cepheus_gpu_memory_view_put(h)<0 && cepheus_gpu_memory_release_cpu(h)<0);
    assert(cepheus_zap_memory_release_cpu(h)<0);
    std::vector<std::thread> readers;
    for(unsigned i=0;i<8;++i)readers.emplace_back([h,first=v.image[0]]{
        for(unsigned j=0;j<100;++j) {
            ceph_zap_memory_view copy={};assert(!cepheus_zap_memory_get(h,&copy));
            assert(copy.logical_bytes==4096 && copy.image[0]==first);
            assert(cepheus_zap_memory_release_cpu(h)<0);assert(!cepheus_zap_memory_put(h));
        }
    });
    for(auto &reader:readers)reader.join();
    assert(!cepheus_zap_memory_put(h));
    assert(cepheus_zap_memory_put(h)<0);
    uint64_t full[4];for(auto &id:full)assert(!cepheus_gpu_memory_alloc(256*1024,&id));
    assert(cepheus_gpu_memory_alloc(4096,&other)<0);
    for(auto id:full)assert(!cepheus_gpu_memory_release_cpu(id));
    if(mode==1)mock_io::fail_complete=1;
    int released=cepheus_zap_memory_release_cpu(h);
    mock_io::fail_complete=0;
    assert(released==(mode==1?-1:0));
    assert(cepheus_zap_memory_get(h,&v)<0 && cepheus_zap_memory_release_cpu(h)<0);
    uint64_t stale=h;
    assert(!cepheus_gpu_firmware_snapshot(CE_FW_A640_ZAP,elf.data(),elf.size(),&snapshot));
    if(mode==1) {
        unsigned calls=mock_io::alloc_calls;
        assert(cepheus_zap_memory_prepare(snapshot,&h)<0 && !h && mock_io::alloc_calls==calls);
    } else {
        assert(!cepheus_zap_memory_prepare(snapshot,&h) && h!=stale);
        assert(cepheus_zap_memory_get(stale,&v)<0 && cepheus_zap_memory_put(stale)<0);
        assert(!cepheus_zap_memory_release_cpu(h));
    }
    assert(!cepheus_gpu_memory_release_cpu(snapshot));
    assert(mock_io::live==(mode==1?2u:0u) && mock_io::prepared==mock_io::live && mock_io::maps==mock_io::live);
    assert(!mock_io::raw_views);
    puts("ZAP workspace: exact bytes, zero full capacity, PA alignment, independent budgets, source lease, concurrent reads, stale handles and CPU cleanup PASS; no PAS");
}
