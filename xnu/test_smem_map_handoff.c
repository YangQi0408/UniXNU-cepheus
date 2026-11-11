#include <stdio.h>
#include <string.h>
#include "cepheus_smem_map_handoff.h"
#define CHECK(c) do { if (!(c)) {fprintf(stderr,"failed %u\n",__LINE__);return 1;} } while(0)
static struct ceph_smem_map_handoff h, saved;
static void seal(void) { h.checksum=ceph_smem_map_checksum(&h); }
static void fixture(void)
{
    memset(&h,0,sizeof(h));
    h.magic=CE_SMEM_MAP_MAGIC;h.version=1;h.bytes=sizeof(h);h.complete=h.ebs_done=h.ebs_attempts=1;
    h.tree_address=0x90000000;h.tree_bytes=8192;h.property_offset=128;
    h.base=CE_SMEM_MAP_BASE;h.span=CE_SMEM_MAP_SPAN;h.begin_ticks=1;h.end_ticks=2;
    h.probe_source_sha256[0]=h.profile_source_sha256[0]=h.profile_header_sha256[0]=1;
    h.before=(struct ceph_smem_map_cpu){4,0x80000000,5,32,0x90000000,0,0xff4400,5};h.after=h.before;
    h.resource=(struct ceph_smem_map_resource){.hob_type=3,.hob_bytes=48,.type=5,.attributes=0x800,
        .base=CE_SMEM_MAP_BASE,.bytes=CE_SMEM_MAP_SPAN};
    h.gcd=(struct ceph_smem_map_gcd){.base=CE_SMEM_MAP_BASE,.bytes=CE_SMEM_MAP_SPAN,.type=1};
    h.pages=512;h.hobs=3;h.resource_matches=h.resource_complete=h.gcd_complete=h.cpu_stable=1;
    for(unsigned i=0;i<512;++i)h.par[i]=UINT64_C(0x4400000000000000)| (h.base+(uint64_t)i*4096);
    seal();saved=h;
}
int main(void)
{
    CHECK(!ceph_smem_map_profile(NULL) && !ceph_smem_map_profile("sweetusb=2"));
    CHECK(ceph_smem_map_profile("sweetusb=2\tcephsmemmap=1")==1);
    const char *bad[]={"cephsmemmap","cephsmemmap=0","cephsmemmap=01","cephsmemmap=1x",
        "cephsmemmap-extra=1","cephsmemmap=1 cephsmemmap=1","cephsoc=0 cephsmemmap=1"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i)CHECK(ceph_smem_map_profile(bad[i])==-1);
    fixture();CHECK(!ceph_smem_map_valid(&h) && ceph_smem_map_diagnostic_pass(&h));
    for(unsigned byte=0;byte<sizeof(h);++byte) {
        ((uint8_t *)&h)[byte]^=1;CHECK(ceph_smem_map_valid(&h));h=saved;
    }
    for(unsigned page=0;page<512;++page) {
        h.par[page]^=1;seal();CHECK(!ceph_smem_map_valid(&h) && !ceph_smem_map_diagnostic_pass(&h));h=saved;
    }
    for(unsigned mode=0;mode<12;++mode) {
        switch(mode) {
            case 0:h.tree_address++;break;
            case 1:h.tree_address=CE_SMEM_MAP_BASE;break;
            case 2:h.tree_bytes=128;break;
            case 3:h.property_offset=UINT64_MAX;break;
            case 4:h.ebs_attempts=4;break;
            case 5:h.close_status=1;break;
            case 6:h.end_ticks=0;break;
            case 7:h.pages=513;break;
            case 8:h.gcd.reserved=1;break;
            case 9:memset(h.probe_source_sha256,0,32);break;
            case 10:h.map_status=1;break;
            default:h.complete=0;break;
        }
        seal();CHECK(ceph_smem_map_valid(&h));h=saved;
    }
    h.probe_status=h.map_status=UINT64_C(0x8000000000000011);h.pages=5;
    memset(h.par+5,0,sizeof(h.par)-5*sizeof(h.par[0]));h.par[4]|=1;seal();
    CHECK(!ceph_smem_map_valid(&h) && !ceph_smem_map_diagnostic_pass(&h));
    h.par[5]=1;seal();CHECK(ceph_smem_map_valid(&h));
    puts("SMEM copied contract: exact profile, 4576 byte corruption guards, 512 PAR faults, partial/bounds PASS; no hardware");
    return 0;
}
