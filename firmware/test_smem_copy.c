#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cepheus_smem_copy.h"

#define CHECK(c) do { if(!(c)) {fprintf(stderr,"failed line %u\n",__LINE__);return 1;} } while(0)
#define BYTES 65536u
#define BASE UINT64_C(0x86000000)
static uint8_t ram[BYTES],first[BYTES],second[BYTES],output[4096];
static struct ceph_smem_scratch scratch;
static struct ceph_smem_selection info;
static unsigned reads,allows,fail_read,fail_allow,change;
static void put(uint8_t *p,uint32_t v)
{ for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(8*i)); }
static int allow(void *ctx,uint64_t address,uint32_t bytes)
{
    (void)ctx;++allows;
    return allows==fail_allow || address<BASE || address>BASE+BYTES || bytes>BASE+BYTES-address;
}
static int read_copy(void *ctx,uint64_t address,void *out,uint32_t bytes)
{
    (void)ctx;++reads;
    if(reads==fail_read)return -1;
    if(reads==change)ram[16384]^=1;
    if(address<BASE || address>BASE+BYTES || bytes>BASE+BYTES-address)return -1;
    memcpy(out,ram+address-BASE,bytes);return 0;
}
static void fixture(void)
{
    memset(ram,0,sizeof(ram));memset(first,0xa5,sizeof(first));memset(second,0xa5,sizeof(second));
    memset(output,0xa5,sizeof(output));memset(&info,0xa5,sizeof(info));
    reads=allows=fail_read=fail_allow=change=0;
    put(ram+92,11u<<16);put(ram+192,1);put(ram+196,32768);put(ram+200,32768);
    put(ram+208+469*16,1);put(ram+212+469*16,16384);put(ram+216+469*16,4096);
    memcpy(ram+16384,"TZ.TEST",7);
}
static int capture(int phase)
{
    const struct ceph_smem_copy_ops ops={NULL,allow,read_copy};
    return ceph_smem_copy(output,&info,first,second,BYTES,BASE,&scratch,&ops,phase);
}
static int unpublished(void)
{
    const uint8_t *p=(const void *)&info;
    for(unsigned i=0;i<sizeof(info);++i)if(p[i]!=0xa5)return 0;
    for(unsigned i=0;i<sizeof(output);++i)if(output[i]!=0xa5)return 0;
    return 1;
}
int main(void)
{
    fixture();CHECK(!capture(1));CHECK(reads==32 && allows==36);
    CHECK(!memcmp(first,second,BYTES) && !memcmp(output,"TZ.TEST",7));
    for(unsigned i=1;i<=32;++i) {
        fixture();fail_read=i;CHECK(capture(1)==CE_SMEM_COPY_READ);
        CHECK(reads==i && unpublished());
    }
    for(unsigned i=1;i<=36;++i) {
        fixture();fail_allow=i;CHECK(capture(1)==CE_SMEM_COPY_MAP);
        CHECK(allows==i && unpublished());
    }
    fixture();change=17;CHECK(capture(1)==CE_SMEM_COPY_CHANGED && unpublished());
    CHECK(first[16384]!=second[16384]);
    fixture();put(ram+192,0);CHECK(capture(1)==CE_SMEM_HEADER && unpublished());
    fixture();CHECK(capture(0)==CE_SMEM_COPY_PHASE && !reads && !allows);
    CHECK(capture(2)==CE_SMEM_COPY_PHASE && !reads && !allows);
    const struct ceph_smem_copy_ops ops={NULL,allow,read_copy};
    CHECK(ceph_smem_copy(output,&info,first,first,BYTES,BASE,&scratch,&ops,1)==CE_SMEM_ARGUMENT);
    CHECK(ceph_smem_copy(first,&info,first,second,BYTES,BASE,&scratch,&ops,1)==CE_SMEM_ARGUMENT);
    CHECK(ceph_smem_copy(output,&info,first,second,BYTES,BASE,&scratch,(void *)first,1)==CE_SMEM_ARGUMENT);
    CHECK(ceph_smem_copy((void *)(uintptr_t)BASE,&info,first,second,BYTES,BASE,&scratch,&ops,1)==CE_SMEM_ARGUMENT);
    CHECK(!reads && !allows && unpublished());
    puts("SMEM copy: both copies retained, 32 read/36 map failures, change, phase and aliases PASS; mocks only");
    return 0;
}
