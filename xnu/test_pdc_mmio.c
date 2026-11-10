#define _DEFAULT_SOURCE 1
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "cepheus_pdc_mmio.h"
static volatile uint32_t *gmu,*cfg,*seq;
struct host {unsigned call,fail,finished,kept;};
static struct host *revoke_pdc;
static int lease(void *p)
{
    struct host *h=p;
    if(++h->call==h->fail) {
        assert(!mprotect((void *)gmu,0x30000,PROT_NONE));
        assert(!mprotect((void *)cfg,0x10000,PROT_NONE));
        assert(!mprotect((void *)seq,0x10000,PROT_NONE));return -9;
    }
    return 0;
}
static void finish(void *p,int keep){struct host *h=p;h->finished++;h->kept=(unsigned)keep;}
static int gmu_lease(void *p)
{
    (void)p;
    if(revoke_pdc) {
        revoke_pdc->fail=revoke_pdc->call+1;revoke_pdc=NULL;
        assert(!mprotect((void *)cfg,0x10000,PROT_NONE));
        assert(!mprotect((void *)seq,0x10000,PROT_NONE));
    }
    return 0;
}
static void fixture(struct host *h,struct ceph_gmu_mmio *g,struct ceph_pdc_mmio *m)
{
    *h=(struct host){0};revoke_pdc=NULL;
    assert(!mprotect((void *)gmu,0x30000,PROT_READ|PROT_WRITE));
    assert(!mprotect((void *)cfg,0x10000,PROT_READ|PROT_WRITE));
    assert(!mprotect((void *)seq,0x10000,PROT_READ|PROT_WRITE));
    memset((void *)gmu,0,0x30000);memset((void *)cfg,0,0x10000);memset((void *)seq,0,0x10000);
    gmu[0x1f800-0x1a800]=1;gmu[0x1f8d0-0x1a800]=0xc0;
    *g=(struct ceph_gmu_mmio){(volatile uint8_t *)gmu,0x30000,{0x06040001,0x60100000,20480,0x60200000,4096},0x20000001,h,lease};
    *m=(struct ceph_pdc_mmio){.gmu=g,.cfg=cfg,.seq=seq,.cfg_pa=0xb290000,.seq_pa=0xb490000,
        .cfg_bytes=0x10000,.seq_bytes=0x10000,.owner=h,.lease=lease,.finish=finish};
}
int main(void)
{
    gmu=mmap(NULL,0x30000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    cfg=mmap(NULL,0x10000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    seq=mmap(NULL,0x10000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(gmu!=MAP_FAILED && cfg!=MAP_FAILED && seq!=MAP_FAILED);
    struct ceph_pdc_input in={.chipid=0x06040001,.cfg_pa=0xb290000,.seq_pa=0xb490000};
    for(unsigned i=0;i<3;++i)in.resources[i]=(struct ceph_arc_resource){.address=(uint32_t[]){0x30010,0x30000,0x30090}[i],.levels={0,16,48,64},.count=4};
    struct host h;struct ceph_gmu_mmio g;struct ceph_pdc_mmio m;struct ceph_pdc_ops ops;struct ceph_pdc_install s={0};
    fixture(&h,&g,&m);assert(!ceph_pdc_mmio_ops(&m,&ops));assert(!ceph_pdc_install_a640v2(&s,&ops,&in));
    assert(m.cursor==48 && m.terminal && h.finished==1 && h.kept && cfg[0x1140]==0x80000001);
    assert(gmu[0x23580-0x1a800]==0xa7a506a0 && seq[0]==0xfebea1e1);
    unsigned calls=h.call;
    for(unsigned fail=1;fail<=calls;++fail) {
        fixture(&h,&g,&m);h.fail=fail;s=(struct ceph_pdc_install){0};int e=ceph_pdc_mmio_ops(&m,&ops);
        if(!e)e=ceph_pdc_install_a640v2(&s,&ops,&in);
        assert(e && h.call==fail);
        if(s.attempted)assert(m.terminal && h.kept && h.finished==1 && s.quarantined);
        unsigned before=h.call;uint32_t value=0x12345678;
        assert(ceph_pdc_mmio_ready(&m)<0 && h.call==before);
        assert(ceph_pdc_mmio_read(&m,CE_PDC_CFG,0x1140*4,&value)<0 && value==0x12345678);
    }
    fixture(&h,&g,&m);cfg[0x1140]=1;assert(ceph_pdc_mmio_begin(&m,&in)<0 && cfg[0x1140]==1);
    fixture(&h,&g,&m);gmu[0x1f800-0x1a800]=0;assert(ceph_pdc_mmio_ops(&m,&ops)<0);
    fixture(&h,&g,&m);gmu[0x1f8d0-0x1a800]=0;assert(ceph_pdc_mmio_ops(&m,&ops)<0);
    fixture(&h,&g,&m);assert(!ceph_pdc_mmio_begin(&m,&in));uint32_t value;
    assert(ceph_pdc_mmio_write(&m,CE_PDC_CFG,0x1140*4,0x80000001)<0 && !m.cursor);
    assert(ceph_pdc_mmio_write(&m,CE_PDC_RSC,0x23404*4-0x6a000,0)<0);
    assert(ceph_pdc_mmio_read(&m,CE_PDC_CFG,0,&value)<0);
    assert(ceph_pdc_mmio_read(&m,(enum ceph_pdc_window)99,0,&value)<0);
    ceph_pdc_mmio_end(&m,0);assert(h.finished==1 && !h.kept);
    fixture(&h,&g,&m);m.cfg=(volatile uint32_t *)(UINTPTR_MAX-3u);assert(ceph_pdc_mmio_ready(&m)<0 && !h.call);
    fixture(&h,&g,&m);m.seq=(volatile uint32_t *)(UINTPTR_MAX-3u);assert(ceph_pdc_mmio_ready(&m)<0 && !h.call);
    fixture(&h,&g,&m);assert(!ceph_pdc_mmio_begin(&m,&in));cfg[0x1140]=UINT32_MAX;
    value=0x12345678;assert(ceph_pdc_mmio_read(&m,CE_PDC_CFG,0x1140*4,&value)<0 && value==0x12345678);
    unsigned before=h.call;assert(ceph_pdc_mmio_ready(&m)<0 && h.call==before);
    ceph_pdc_mmio_end(&m,0);assert(h.finished==1 && !h.kept);
    fixture(&h,&g,&m);assert(!ceph_pdc_mmio_begin(&m,&in));
    g.lease=gmu_lease;revoke_pdc=&h;value=0x12345678;
    assert(ceph_pdc_mmio_read(&m,CE_PDC_CFG,0x1140*4,&value)<0 && value==0x12345678 && m.failed);
    ceph_pdc_mmio_end(&m,0);assert(h.finished==1 && !h.kept);
    assert(!munmap((void *)gmu,0x30000));assert(!munmap((void *)cfg,0x10000));assert(!munmap((void *)seq,0x10000));
    printf("PDC MMIO: %u PROT_NONE boundaries, 48 ordered stores, cold state, guarded reads/stores, output atomicity and terminal lease-failure retention PASS (host model)\n",calls);
}
