#define _DEFAULT_SOURCE 1
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "cepheus_hfi.h"
static uint32_t *page;
static unsigned checks,expire,publishes,notifies,header_mutation,queue_mutation,callback_mutation;
static struct ceph_hfi_queue queue;
static int validate(void *unused)
{
    (void)unused;++checks;
    if(expire && checks>=expire) {
        assert(!mprotect(page,4096,PROT_NONE));return -1;
    }
    if(header_mutation && checks==header_mutation)page[0]^=1;
    if(queue_mutation && checks==queue_mutation)queue.words=page+128;
    return 0;
}
static int okay(void *unused){(void)unused;return 0;}
static int publish(void *unused){(void)unused;++publishes;if(callback_mutation)queue.words=page+128;return 0;}
static int notify(void *unused){(void)unused;++notifies;return 0;}
static void reset(void)
{
    assert(!mprotect(page,4096,PROT_READ|PROT_WRITE));memset(page,0,4096);
    checks=expire=publishes=notifies=header_mutation=queue_mutation=callback_mutation=0;
    queue=(struct ceph_hfi_queue){page,page+32,page+33,16,2,
        {NULL,okay,okay,publish,notify,validate},0};
}
int main(void)
{
    page=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(page!=MAP_FAILED);
    uint32_t msg[]={ceph_hfi_header(1,3,0,1),0x12345678,UINT32_MAX};
    unsigned r,w,count;uint32_t out[4];
    reset();assert(!ceph_hfi_write(&queue,msg,3) && page[33]==4 && page[3]==0xfafafafa);
    unsigned write_boundaries=checks;assert(publishes==1 && notifies==1);
    for(unsigned n=1;n<=write_boundaries;++n) {
        reset();expire=n;assert(ceph_hfi_write(&queue,msg,3)<0 && checks==n && queue.poisoned);
        expire=0;unsigned before=checks;assert(ceph_hfi_write(&queue,msg,3)==CE_HFI_POISONED && checks==before);
    }
    reset();memcpy(page,msg,sizeof(msg));page[33]=4;
    assert(!ceph_hfi_read(&queue,out,4,&count) && count==3 && out[2]==UINT32_MAX && page[32]==4);
    unsigned read_boundaries=checks;assert(publishes==1 && !notifies);
    for(unsigned n=1;n<=read_boundaries;++n) {
        reset();memcpy(page,msg,sizeof(msg));page[33]=4;expire=n;
        for(unsigned i=0;i<4;++i)out[i]=0xdeadbeef;
        assert(ceph_hfi_read(&queue,out,4,&count)<0 && !count && checks==n && queue.poisoned);
        for(unsigned i=0;i<4;++i)assert(out[i]==0xdeadbeef);
    }
    reset();assert(!ceph_hfi_indices(&queue,&r,&w));unsigned index_boundaries=checks;
    for(unsigned n=1;n<=index_boundaries;++n) {
        reset();expire=n;r=123;w=456;
        assert(ceph_hfi_indices(&queue,&r,&w)<0 && r==123 && w==456 && checks==n);
    }
    reset();queue.io.validate=NULL;assert(ceph_hfi_write(&queue,msg,3)==CE_HFI_INVALID && !checks);
    reset();queue.read_index=queue.write_index;assert(ceph_hfi_indices(&queue,&r,&w)==CE_HFI_INVALID && !checks);
    reset();queue.read_index=page;assert(ceph_hfi_indices(&queue,&r,&w)==CE_HFI_INVALID && !checks);
    reset();queue.words=(uint32_t *)(UINTPTR_MAX-3);assert(ceph_hfi_write(&queue,msg,3)==CE_HFI_INVALID && !checks);
    reset();r=123;w=456;page[33]=UINT32_MAX;
    assert(ceph_hfi_indices(&queue,&r,&w)==CE_HFI_INVALID && r==123 && w==456);
    reset();memcpy(page,msg,sizeof(msg));page[33]=4;header_mutation=11;
    for(unsigned i=0;i<4;++i)out[i]=0xdeadbeef;
    assert(ceph_hfi_read(&queue,out,4,&count)==CE_HFI_IO && !count && queue.poisoned && !page[32]);
    for(unsigned i=0;i<4;++i)assert(out[i]==0xdeadbeef);
    reset();uint32_t value=0xdeadbeef;
    assert(ceph_hfi_load(&queue,page+16,&value)==CE_HFI_INVALID && value==0xdeadbeef);
    assert(ceph_hfi_store(&queue,page+16,1)==CE_HFI_INVALID && !page[16]);
    assert(ceph_hfi_callback(&queue,NULL)==CE_HFI_INVALID);
    assert(ceph_hfi_indices(&queue,(unsigned *)(page+32),&w)==CE_HFI_INVALID && !page[32]);
    assert(ceph_hfi_write(&queue,page+32,3)==CE_HFI_INVALID && !page[33]);
    assert(ceph_hfi_write(&queue,(uint32_t *)(UINTPTR_MAX-3),3)==CE_HFI_INVALID);
    assert(ceph_hfi_read(&queue,out,4,(unsigned *)(page+32))==CE_HFI_INVALID && !page[32]);
    assert(ceph_hfi_read(&queue,out,4,(unsigned *)out)==CE_HFI_INVALID);
    reset();queue_mutation=1;
    assert(ceph_hfi_write(&queue,msg,3)==CE_HFI_IO && queue.poisoned && !page[33]);
    reset();callback_mutation=1;
    assert(ceph_hfi_write(&queue,msg,3)==CE_HFI_IO && queue.poisoned && !notifies);
    reset();page[32]=page[33]=12;assert(!ceph_hfi_write(&queue,msg,3) && !page[33]);
    assert(!ceph_hfi_read(&queue,out,4,&count) && count==3 && !page[32]);
    assert(!munmap(page,4096));
    printf("HFI per-word ownership: %u write/%u read/%u index PROT_NONE boundaries, atomic output, sticky loss and wrap/padding PASS (shared RAM)\n",write_boundaries,read_boundaries,index_boundaries);
}
