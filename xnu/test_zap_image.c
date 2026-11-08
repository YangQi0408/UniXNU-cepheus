#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cepheus_zap_image.h"

static void u16(uint8_t *p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void u32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(8*i));}
static void phdr(uint8_t *p,uint32_t type,uint32_t off,uint32_t pa,uint32_t file,
    uint32_t mem,uint32_t flags,uint32_t align)
{
    uint32_t fields[]={type,off,pa,pa,file,mem,flags,align};
    for(unsigned i=0;i<8;++i)u32(p+4*i,fields[i]);
}
static void fixture(uint8_t *elf)
{
    memset(elf,0,0x3040);memcpy(elf,"\x7f" "ELF\x01\x01\x01",7);
    u16(elf+16,2);u16(elf+18,164);u32(elf+20,1);u32(elf+24,0x5004);
    u32(elf+28,52);u16(elf+40,52);u16(elf+42,32);u16(elf+44,4);
    phdr(elf+52,0,0,0,180,0,0x07000000,0);
    phdr(elf+84,0,0x1000,0x8000,16,4096,0x02000000,4096);
    phdr(elf+116,1,0x2000,0x5000,64,128,0x08000007,0x100000);
    phdr(elf+148,1,0x3000,0x7000,64,96,0x08000007,4096);
    memset(elf+0x1000,0xa1,16);memset(elf+0x2000,0xb2,64);memset(elf+0x3000,0xc3,64);
}
static void unchanged(const uint8_t *p,size_t n,uint8_t expected)
{for(size_t i=0;i<n;++i)assert(p[i]==expected);}

int main(void)
{
    uint8_t elf[0x3040],mdt[256],image[0x3010];struct ceph_zap_image p;
    fixture(elf);memset(mdt,0x5a,sizeof(mdt));memset(image,0x5a,sizeof(image));
    assert(!ceph_zap_stage(elf,sizeof(elf),mdt,sizeof(mdt),image,sizeof(image),&p));
    assert(p.segments==2 && p.header_bytes==180 && p.mdt_bytes==196 && p.entry_offset==4);
    assert(p.logical_bytes==0x3000 && p.allocation_bytes==0x100000 && p.relocation_base==0x5000);
    assert(!memcmp(mdt,elf,180));unchanged(mdt+180,16,0xa1);unchanged(mdt+196,60,0x5a);
    unchanged(image,64,0xb2);unchanged(image+64,0x2000-64,0);
    unchanged(image+0x2000,64,0xc3);unchanged(image+0x2040,0x3000-0x2040,0);
    unchanged(image+0x3000,16,0x5a);
    memset(mdt,0x7a,sizeof(mdt));memset(image,0x5a,sizeof(image));
    assert(!ceph_zap_stage_image(elf,sizeof(elf),image,sizeof(image),&p));
    unchanged(mdt,sizeof(mdt),0x7a);unchanged(image,64,0xb2);
    unchanged(image+64,0x2000-64,0);unchanged(image+0x2000,64,0xc3);
    unchanged(image+0x3000,16,0x5a);
    /* Header order is not load-address order. Allocation follows lowest PA. */
    uint8_t swap[32];memcpy(swap,elf+116,32);memcpy(elf+116,elf+148,32);memcpy(elf+148,swap,32);
    assert(!ceph_zap_parse(elf,sizeof(elf),&p) && p.allocation_bytes==0x100000);
    assert(p.load[0].image_offset==0x2000 && p.load[1].image_offset==0);

    const struct {unsigned at;uint32_t value;} mutations[]={
        {28,0},{20,2},{24,0x6000},{52+16,179},{84+4,0xfffffff0},{84+16,0xffffffff},
        {84+24,0},{84+24,0x0a000000},{116,0},{116+24,7},{116+16,129},{116+28,3},
        {116+12,0xfffffff0},{116+12,0x5001},{148+12,0x5040},{148+4,0x2010},
        {148+4,0x1000},{148+12,0x500000},{116+4,100},{148+24,0x02000000}
    };
    for(unsigned i=0;i<sizeof(mutations)/sizeof(mutations[0]);++i) {
        fixture(elf);u32(elf+mutations[i].at,mutations[i].value);
        memset(mdt,0x5a,sizeof(mdt));memset(image,0x5a,sizeof(image));memset(&p,0x5a,sizeof(p));
        assert(ceph_zap_stage(elf,sizeof(elf),mdt,sizeof(mdt),image,sizeof(image),&p)<0);
        assert(ceph_zap_stage_image(elf,sizeof(elf),image,sizeof(image),&p)<0);
        unchanged(mdt,sizeof(mdt),0x5a);unchanged(image,sizeof(image),0x5a);
        unchanged((const uint8_t *)&p,sizeof(p),0x5a);
    }
    fixture(elf);u16(elf+44,33);assert(ceph_zap_parse(elf,sizeof(elf),&p)<0);
    fixture(elf);u16(elf+18,183);assert(ceph_zap_parse(elf,sizeof(elf),&p)<0);
    fixture(elf);elf[5]=2;assert(ceph_zap_parse(elf,sizeof(elf),&p)<0);
    fixture(elf);const size_t lengths[]={0,51,179,0x303f};
    for(unsigned i=0;i<sizeof(lengths)/sizeof(lengths[0]);++i)assert(ceph_zap_parse(elf,lengths[i],&p)<0);
    assert(ceph_zap_stage(elf,sizeof(elf),mdt,195,image,sizeof(image),&p)<0);
    assert(ceph_zap_stage(elf,sizeof(elf),mdt,sizeof(mdt),image,0x2fff,&p)<0);
    assert(ceph_zap_stage(elf,sizeof(elf),elf,256,image,sizeof(image),&p)<0);
    assert(ceph_zap_stage(elf,sizeof(elf),mdt,sizeof(mdt),elf,sizeof(elf),&p)<0);
    assert(ceph_zap_stage(elf,sizeof(elf),image,256,image+128,0x3000,&p)<0);
    assert(ceph_zap_stage(elf,sizeof(elf),mdt,sizeof(mdt),image,sizeof(image),(struct ceph_zap_image *)image)<0);
    assert(ceph_zap_stage(elf,sizeof(elf),NULL,256,image,sizeof(image),&p)<0);
    assert(ceph_zap_stage(elf,sizeof(elf),mdt,SIZE_MAX,image,sizeof(image),&p)<0);
    assert(ceph_zap_stage_image(elf,sizeof(elf),image,0x2fff,&p)<0);
    assert(ceph_zap_stage_image(elf,sizeof(elf),elf,sizeof(elf),&p)<0);
    assert(ceph_zap_stage_image(elf,sizeof(elf),image,sizeof(image),(struct ceph_zap_image *)image)<0);
    assert(ceph_zap_stage_image(elf,sizeof(elf),image,SIZE_MAX,&p)<0);
    assert(ceph_zap_stage_image(elf,sizeof(elf),NULL,4096,&p)<0);
    /* A zero-file BSS load must still create a zeroed memory extent. */
    u32(elf+116+16,0);assert(!ceph_zap_stage(elf,sizeof(elf),mdt,sizeof(mdt),image,sizeof(image),&p));
    unchanged(image,0x2000,0);
    fixture(elf);u32(elf+116+28,0x400000);
    assert(!ceph_zap_parse(elf,sizeof(elf),&p) && p.allocation_bytes==0x400000);
    /* Last ELF32 page: round the end in 64 bits, never wrap to zero. */
    fixture(elf);u32(elf+24,0xfffff004);u32(elf+116+12,0xfffff000);u32(elf+148+12,0xfffff800);
    assert(!ceph_zap_parse(elf,sizeof(elf),&p) && p.logical_bytes==4096 && p.entry_offset==4);
    puts("ZAP staging: relocation, MDT, BSS/gaps, bounds, aliases and no partial writes PASS (CPU only)");
}
