#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include "cepheus_zap_memory.h"
#include "cepheus_zap_cache.h"
int main(int argc,char **argv)
{
    assert(argc==2);int mode=atoi(argv[1]);assert(mode>=0 && mode<=10);
    uint64_t pa=0x120004000ULL;
    assert(ceph_zap_translation_matches(pa,0x4000,pa,0));
    assert(ceph_zap_translation_matches(pa|(0x44ULL<<56),0x4023,pa+35,0x44));
    assert(!ceph_zap_translation_matches(pa|1,0x4000,pa,0));
    assert(!ceph_zap_translation_matches(pa|(0x44ULL<<56),0x4000,pa,0));
    assert(!ceph_zap_translation_matches(pa,0x4001,pa,0));
    if(mode>=1 && mode<=3) {
        mock_io::bad_translation_at=mode==1?1:mode==2?4:260;
        mock_io::translation_fault=mode;
    }
    if(mode==4)mock_io::fail_restore=1;
    if(mode==5)mock_io::bad_restore_par=1;
    if(mode==6)mock_io::fail_alloc_at=2;
    if(mode==7)mock_io::bad_large_alignment=1;
    if(mode==8)mock_io::fail_complete=1;
    if(mode==9)mock_io::fail_map_at=1;
    if(mode==10)mock_io::stale_restore_alias=1;
    int result=cepheus_zap_memory_selftest();
    assert(result==(mode?-1:0));
    unsigned retained=mode==4 || mode==5 || mode==10?1:mode==8?2:0;
    assert(mock_io::live==retained && mock_io::prepared==retained && mock_io::maps==retained);
    if(!mode)assert(mock_io::translation_calls==268 && mock_io::alloc_calls==2);
    unsigned calls=mock_io::alloc_calls;
    assert(cepheus_zap_memory_selftest()<0 && mock_io::alloc_calls==calls);
    uint64_t generic;assert(!cepheus_gpu_memory_alloc(4096,&generic));
    mock_io::fail_complete=0;assert(!cepheus_gpu_memory_release_cpu(generic));
    assert(mock_io::live==retained && !mock_io::raw_views);
    printf("ZAP preflight mode=%d PASS: real allocation/translation policy, one-shot, cleanup and isolated budget; fake IOKit/PAR only\n",mode);
}
