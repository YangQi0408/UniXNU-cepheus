#define _DEFAULT_SOURCE 1
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "cepheus_smmu_mmio.h"
static volatile uint64_t *aperture;
static unsigned checks,expire;
static int lease(void *p)
{
    ++checks;
    if(checks==expire){assert(!mprotect((void *)aperture,0x10000,PROT_NONE));return -1;}
    return *(int *)p;
}
int main(void)
{
    aperture=mmap(NULL,0x10000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(aperture!=MAP_FAILED);memset((void *)aperture,0,0x10000);
    struct ceph_smmu_stream streams[16]={0};for(unsigned i=0;i<16;++i)streams[i].s2cr=2u<<16;
    struct ceph_smmu_mmio m={0};int busy=0;m.base=(volatile uint8_t *)aperture;m.mapped_bytes=0x10000;m.actlr=0x303;m.owner=&busy;m.lease=lease;
    assert(!ceph_smmu_geometry((1u<<30)|(1u<<27)|(11u<<9)|16,(2u<<28)|8,0x1022,0x10000,&m.geometry));
    assert(!ceph_smmu_gmu_plan(streams,16,2,3,8,5,4,0x110000000ULL,0,&m.plan));
    uint32_t off;unsigned width;uint64_t value;
    assert(!ceph_smmu_mmio_offset(&m,CE_SM_TTBR0,&off,&width) && off==0xb020 && width==8);
    assert(!ceph_smmu_mmio_write(&m,CE_SM_TTBR0,m.plan.ttbr0));
    assert(!ceph_smmu_mmio_read(&m,CE_SM_TTBR0,&value) && value==m.plan.ttbr0);
    assert(ceph_smmu_mmio_write(&m,CE_SM_TTBR0,m.plan.ttbr0+4096)<0);
    assert(!ceph_smmu_mmio_write(&m,CE_SM_SMR,m.plan.smr));assert(((uint32_t *)aperture)[0x808/4]==m.plan.smr);
    assert(ceph_smmu_mmio_write(&m,CE_SM_FSR,0xffffffff)<0 && ceph_smmu_mmio_write(&m,CE_SM_REG_COUNT,0)<0);
    assert(ceph_smmu_mmio_read(&m,CE_SM_TLBIASID,&value)<0);
    busy=1;assert(ceph_smmu_mmio_write(&m,CE_SM_S2CR,3)<0);busy=0;
    m.plan.cb=8;assert(ceph_smmu_mmio_read(&m,CE_SM_SCTLR,&value)<0);m.plan.cb=3;
    m.geometry.s2_contexts=4;assert(ceph_smmu_mmio_read(&m,CE_SM_SCTLR,&value)<0);m.geometry.s2_contexts=0;
    for(unsigned wide=0;wide<2;++wide)for(unsigned n=1;n<=2;++n) {
        enum ceph_smmu_reg r=wide?CE_SM_TTBR0:CE_SM_SMR;
        uint64_t expected=wide?m.plan.ttbr0:m.plan.smr;
        assert(!mprotect((void *)aperture,0x10000,PROT_READ|PROT_WRITE));
        checks=0;expire=n;value=123;
        assert(ceph_smmu_mmio_read(&m,r,&value)<0 && checks==n && value==123);
        assert(!mprotect((void *)aperture,0x10000,PROT_READ|PROT_WRITE));
        checks=0;expire=n;assert(ceph_smmu_mmio_write(&m,r,expected)<0 && checks==n);
    }
    assert(!mprotect((void *)aperture,0x10000,PROT_READ|PROT_WRITE));expire=checks=0;
    aperture[0xb020/8]=UINT64_MAX;value=123;
    assert(ceph_smmu_mmio_read(&m,CE_SM_TTBR0,&value)<0 && checks==2 && value==123);
    ((volatile uint32_t *)aperture)[0x808/4]=UINT32_MAX;checks=0;
    assert(ceph_smmu_mmio_read(&m,CE_SM_SMR,&value)<0 && checks==2 && value==123);
    struct ceph_smmu_mmio bad=m;bad.base=(volatile uint8_t *)(UINTPTR_MAX-7u);checks=0;
    assert(ceph_smmu_mmio_read(&bad,CE_SM_SMR,&value)<0 && !checks);
    m.mapped_bytes=0x8000;assert(ceph_smmu_mmio_read(&m,CE_SM_SCTLR,&value)<0);
    assert(!munmap((void *)aperture,0x10000));
    puts("SMMU MMIO: named offsets/values, 32/64bit PROT_NONE pre/post guards, invalid output atomicity and overflow refusal PASS (host model)");
}
