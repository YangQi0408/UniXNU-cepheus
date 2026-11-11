#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "cepheus_smem_copied_view.h"

#define CHECK(c) do { if(!(c)) {fprintf(stderr,"failed line %u\n",__LINE__);return 1;} } while(0)
static void put(uint8_t *p,uint32_t v)
{ for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(8*i)); }
int main(void)
{
    const unsigned bytes=65536;
    uint8_t *view=calloc(1,bytes),output[4096];
    struct ceph_smem_scratch *scratch=calloc(1,sizeof(*scratch));
    struct ceph_smem_selection info;
    CHECK(view && scratch);
    put(view+92,11u<<16);put(view+192,1);put(view+196,32768);put(view+200,32768);
    put(view+208+469*16,1);put(view+212+469*16,16384);put(view+216+469*16,4096);
    memcpy(view+16384,"TZ.TEST",7);
    CHECK(!ceph_smem_extract(output,&info,view,bytes,0x86000000,scratch));
    CHECK(info.offset==16384 && info.bytes==4096 && info.version==11);
    CHECK(!memcmp(output,"TZ.TEST",7));
    memset(output,0xa5,sizeof(output));memset(&info,0xa5,sizeof(info));
    put(view+208+470*16,1);put(view+212+470*16,16384);put(view+216+470*16,4096);
    CHECK(ceph_smem_extract(output,&info,view,bytes,0x86000000,scratch)==CE_SMEM_ALLOCATION);
    CHECK(output[0]==0xa5 && info.offset==0xa5a5a5a5);
    CHECK(ceph_smem_extract(view,&info,view,bytes,0x86000000,scratch)==CE_SMEM_ARGUMENT);
    CHECK(ceph_smem_extract(output,(void *)output,view,bytes,0x86000000,scratch)==CE_SMEM_ARGUMENT);
    CHECK(ceph_smem_extract(output,&info,view,bytes,0x86000000,(void *)view)==CE_SMEM_ARGUMENT);
    long page=sysconf(_SC_PAGESIZE);
    CHECK(page>0);
    uint8_t *guard=mmap(NULL,(size_t)page,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    CHECK(guard!=MAP_FAILED);
    CHECK(ceph_smem_extract(guard,&info,guard,bytes,0x86000000,scratch)==CE_SMEM_ARGUMENT);
    CHECK(ceph_smem_extract(output,&info,guard,207,0x86000000,scratch)==CE_SMEM_ARGUMENT);
    CHECK(ceph_smem_extract(output,&info,view,bytes,UINT64_MAX,scratch)==CE_SMEM_ARGUMENT);
    CHECK(ceph_smem_extract(output,&info,view,bytes,0x86000000,NULL)==CE_SMEM_ARGUMENT);
    munmap(guard,(size_t)page);free(scratch);free(view);
    puts("SMEM copied-view: output publication, aliases and pre-dereference guards PASS");
    return 0;
}
