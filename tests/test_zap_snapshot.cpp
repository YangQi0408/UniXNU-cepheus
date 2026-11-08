#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include "cepheus_gpu_firmware.h"
#include "cepheus_gpu_memory.h"
#include "cepheus_zap_image.h"

static std::vector<uint8_t> read_file(const char *path)
{
    std::ifstream f(path,std::ios::binary);assert(f);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),{});
}
int main(int argc,char **argv)
{
    uint64_t h=0;std::vector<uint8_t> bad(14256);
    assert(cepheus_gpu_firmware_snapshot(CE_FW_A640_ZAP,bad.data(),bad.size(),&h)<0 && !h);
    assert(cepheus_gpu_firmware_snapshot(3,bad.data(),bad.size(),&h)<0 && !h);
    assert(!mock_io::live && !mock_io::prepared);
    if(argc!=2){puts("ZAP snapshot rejection PASS; SKIP real artifact (supply CEPHEUS_ZAP_ELF)");return 0;}
    auto elf=read_file(argv[1]);assert(elf.size()==14256);
    assert(!cepheus_gpu_firmware_snapshot(CE_FW_A640_ZAP,elf.data(),elf.size(),&h));
    ceph_gpu_memory_view writable={};ceph_gpu_firmware_view source={};
    assert(cepheus_gpu_memory_view_get(h,&writable)<0 && cepheus_gpu_memory_publish(h)<0);
    assert(cepheus_gpu_firmware_get(h,CE_FW_A640_GMU,&source)<0);
    assert(!cepheus_gpu_firmware_get(h,CE_FW_A640_ZAP,&source));
    assert(cepheus_gpu_memory_release_cpu(h)<0);
    elf.back()^=1;assert(source.data[source.bytes-1]!=elf.back());
    std::vector<uint8_t> mdt(6860+16,0x5a),image(4096+16,0x5a);ceph_zap_image p={};
    assert(cepheus_gpu_zap_snapshot_prepare(h,mdt.data(),6859,image.data(),image.size(),&p)<0);
    assert(std::all_of(mdt.begin(),mdt.end(),[](uint8_t v){return v==0x5a;}));
    assert(std::all_of(image.begin(),image.end(),[](uint8_t v){return v==0x5a;}));
    assert(cepheus_gpu_zap_snapshot_prepare(h,const_cast<uint8_t *>(source.data),source.bytes,image.data(),image.size(),&p)<0);
    assert(!cepheus_gpu_zap_snapshot_prepare(h,mdt.data(),mdt.size(),image.data(),image.size(),&p));
    assert(p.mdt_bytes==6860 && p.logical_bytes==4096 && p.allocation_bytes==1048576);
    assert(p.relocation_base==0x5000 && p.entry_offset==0 && p.segments==1);
    assert(p.load[0].filesz==1968 && p.load[0].memsz==1968 && !p.load[0].image_offset);
    std::string base=argv[1];base.resize(base.size()-4);
    auto actual_mdt=read_file((base+".mdt").c_str());
    auto actual_load=read_file((base+".b02").c_str());
    assert(actual_mdt.size()==6860 && !memcmp(mdt.data(),actual_mdt.data(),actual_mdt.size()));
    assert(actual_load.size()==1968 && !memcmp(image.data(),actual_load.data(),actual_load.size()));
    assert(std::all_of(image.begin()+1968,image.begin()+4096,[](uint8_t v){return !v;}));
    assert(std::all_of(image.begin()+4096,image.end(),[](uint8_t v){return v==0x5a;}));
    assert(std::all_of(mdt.begin()+6860,mdt.end(),[](uint8_t v){return v==0x5a;}));
    uint64_t corrupt=0;assert(cepheus_gpu_firmware_snapshot(CE_FW_A640_ZAP,elf.data(),elf.size(),&corrupt)<0 && !corrupt);
    assert(!cepheus_gpu_memory_view_put(h));assert(!cepheus_gpu_memory_release_cpu(h));
    assert(cepheus_gpu_zap_snapshot_prepare(h,mdt.data(),mdt.size(),image.data(),image.size(),&p)<0);
    assert(!mock_io::live && !mock_io::prepared && !mock_io::maps);
    puts("ZAP sealed snapshot: exact local MDT/b02 bytes, zero padding, immutable source, wrong/stale kind and corrupt input rejection PASS; no PAS");
}
