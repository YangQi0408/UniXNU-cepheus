#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cepheus_smem_copied_view.h"

static uint64_t u64(const uint8_t *p)
{ return ceph_smem_u32(p) | (uint64_t)ceph_smem_u32(p+4)<<32; }
int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    FILE *input=fopen(argv[1],"rb");
    uint8_t *view=malloc(CE_SMEM_MAX_REGION),out[4096],expected[4096],header[36];
    struct ceph_smem_scratch *scratch=malloc(sizeof(*scratch));
    struct ceph_smem_selection info;
    if(!input || !view || !scratch)return 2;
    unsigned cases=0;
    for(;;) {
        size_t got=fread(header,1,sizeof(header),input);
        if(!got && feof(input))break;
        if(got!=sizeof(header))return 2;
        uint32_t bytes=ceph_smem_u32(header),accepted=ceph_smem_u32(header+12);
        if(bytes>CE_SMEM_MAX_REGION || accepted>1 || fread(view,1,bytes,input)!=bytes)return 2;
        memset(out,0xa5,sizeof(out));memset(&info,0xa5,sizeof(info));
        int error=ceph_smem_extract(out,&info,view,bytes,u64(header+4),scratch);
        if(accepted) {
            if(fread(expected,1,sizeof(expected),input)!=sizeof(expected) || error ||
                memcmp(out,expected,sizeof(out)) || info.version!=ceph_smem_u32(header+16) ||
                info.item_count!=ceph_smem_u32(header+20) || info.offset!=ceph_smem_u32(header+24) ||
                info.bytes!=ceph_smem_u32(header+28) || info.allocations!=ceph_smem_u32(header+32))return 1;
        } else {
            if(!error)return 1;
            const uint8_t *p=(const void *)&info;
            for(unsigned i=0;i<sizeof(info);++i)if(p[i]!=0xa5)return 1;
            for(unsigned i=0;i<sizeof(out);++i)if(out[i]!=0xa5)return 1;
        }
        ++cases;
    }
    if(ferror(input))return 2;
    fclose(input);free(view);free(scratch);
    printf("SMEM full differential corpus: %u cases ASan/UBSan PASS\n",cases);
    return 0;
}
