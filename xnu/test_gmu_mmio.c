#define _DEFAULT_SOURCE 1
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "cepheus_gmu_mmio.h"
static volatile uint32_t *aperture;
static unsigned checks,expire;
static int lease(void *p)
{
    ++checks;
    if(checks==expire){assert(!mprotect((void *)aperture,0x30000,PROT_NONE));return -1;}
    return *(int *)p;
}
int main(void)
{
    aperture=mmap(NULL,0x30000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(aperture!=MAP_FAILED);memset((void *)aperture,0,0x30000);
    int blocked=0;struct ceph_gmu_mmio m={(volatile uint8_t *)aperture,0x30000,
        {0x06040001,0x60000000,20480,0x60100000,4096},0x20000001,&blocked,lease};
    assert(!ceph_gmu_mmio_start_write(&m,CE_GMU_QTBL_ADDR,0x60000000));
    assert(aperture[0x1f985-0x1a800]==0x60000000);
    assert(ceph_gmu_mmio_start_write(&m,CE_GMU_QTBL_ADDR,0x60001000)<0);
    assert(ceph_gmu_mmio_start_write(&m,CE_GMU_HFI_VERSION,2)<0);
    aperture[0x1f8e8-0x1a800]=0x20;
    assert(!ceph_gmu_mmio_start_write(&m,CE_GMU_RPMH_CTRL,0xf31));
    assert(ceph_gmu_mmio_start_write(&m,CE_GMU_RPMH_CTRL,0xf11)<0);
    uint32_t value;assert(!ceph_gmu_mmio_start_read(&m,CE_GMU_RPMH_CTRL,&value) && value==0xf31);
    assert(!ceph_gmu_mmio_oob_write(&m,CE_OOB_H2G_SET,1u<<30));
    assert(ceph_gmu_mmio_oob_write(&m,CE_OOB_G2H_CLEAR,1)<0);
    assert(!ceph_gmu_mmio_hfi_notify(&m) && aperture[0x1f994-0x1a800]==1);
    assert(!ceph_gmu_mmio_fault(&m));aperture[0x23b05-0x1a800]=1;assert(ceph_gmu_mmio_fault(&m)<0);
    assert(!ceph_gmu_mmio_gx_access(&m));
    for(unsigned bit=2;bit<=7;++bit)if((1u<<bit)&0xd4u) {
        aperture[0x1f8d0-0x1a800]=1u<<bit;assert(ceph_gmu_mmio_gx_access(&m)<0);
    }
    aperture[0x1f8d0-0x1a800]=0;aperture[0x23b10-0x1a800]=2;
    assert(ceph_gmu_mmio_gx_access(&m)<0 && aperture[0x23b10-0x1a800]==2);
    blocked=1;assert(ceph_gmu_mmio_start_write(&m,CE_GMU_SYSRESET,0)<0);
    blocked=0;
    for(unsigned n=1;n<=2;++n) {
        assert(!mprotect((void *)aperture,0x30000,PROT_READ|PROT_WRITE));
        checks=0;expire=n;value=0x12345678;
        assert(ceph_gmu_mmio_start_read(&m,CE_GMU_SYSRESET,&value)<0 && checks==n && value==0x12345678);
        assert(!mprotect((void *)aperture,0x30000,PROT_READ|PROT_WRITE));
        checks=0;expire=n;aperture[0x1f800-0x1a800]=1;
        assert(ceph_gmu_mmio_start_write(&m,CE_GMU_SYSRESET,0)<0 && checks==n);
        assert(!mprotect((void *)aperture,0x30000,PROT_READ|PROT_WRITE));
        assert(aperture[0x1f800-0x1a800]==(n==1?1u:0u));
    }
    expire=checks=0;aperture[0x1f800-0x1a800]=UINT32_MAX;value=0x12345678;
    assert(ceph_gmu_mmio_start_read(&m,CE_GMU_SYSRESET,&value)<0 && checks==2 && value==0x12345678);
    struct ceph_gmu_mmio bad=m;bad.base=(volatile uint8_t *)(UINTPTR_MAX-3u);
    assert(ceph_gmu_mmio_start_read(&bad,CE_GMU_SYSRESET,&value)<0);
    m.bytes=0x1000;assert(ceph_gmu_mmio_hfi_notify(&m)<0);
    assert(!munmap((void *)aperture,0x30000));
    puts("GMU MMIO: exact boot/OOB/HFI values, PROT_NONE pre/post-load/store guards, invalid output atomicity and lease gates PASS (host model)");
}
