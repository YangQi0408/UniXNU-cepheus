#define _DEFAULT_SOURCE 1
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "cepheus_gmu_hfi_link.h"
static uint32_t *table;
static unsigned checks,expire,fail_status,terminal,flip_identity;
static struct ceph_gmu_workspace workspace;
static struct ceph_gpu_group_status status;
int cepheus_gpu_memory_group_status(uint64_t group,struct ceph_gpu_group_status *out)
{if(fail_status || group!=status.identity.group)return -1;*out=status;return 0;}
static int validate(void *unused)
{
    (void)unused;++checks;
    if(expire && checks>=expire){assert(!mprotect(table,20480,PROT_NONE));return -1;}
    if(flip_identity && checks==flip_identity)workspace.publication.ttbr0^=0x1000;
    return 0;
}
static int okay(void *unused){(void)unused;return 0;}
static int now(void *unused,uint64_t *out){(void)unused;*out=1000;return 0;}
static struct ceph_gmu_hfi_transport transport;
static void reset(void)
{
    assert(!mprotect(table,20480,PROT_READ|PROT_WRITE));memset(table,0,20480);
    checks=expire=fail_status=terminal=flip_identity=0;
    memset(&workspace,0,sizeof(workspace));workspace.state=CE_WS_SHARED;
    workspace.leased=CE_GPU_WORKSPACE_VIEWS;workspace.views[2].cpu=table;workspace.views[2].bytes=20480;
    workspace.layout.start.hfi_va=0x60100000;workspace.layout.start.hfi_bytes=20480;
    workspace.publication.group=1;workspace.publication.ttbr0=0x120000000;
    workspace.publication.smr=0x84000005;workspace.publication.cb=3;
    for(unsigned i=0;i<6;++i)workspace.handles[i]=workspace.publication.handles[i]=i+1;
    memset(&status,0,sizeof(status));status.identity=workspace.publication;status.state=CE_GPU_GROUP_SHARED;
    assert(!ceph_hfi_table(table,54,0x60100000,20480));
    transport=(struct ceph_gmu_hfi_transport){NULL,validate,okay,now,okay,okay};
}
int main(void)
{
    table=mmap(NULL,20480,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(table!=MAP_FAILED);
    struct ceph_gmu_hfi_link link;
    reset();memset(&link,0,sizeof(link));assert(!cepheus_gmu_hfi_bind(&link,&workspace,&transport));
    unsigned bind_boundaries=checks;
    for(unsigned n=1;n<=bind_boundaries;++n) {
        reset();expire=n;memset(&link,0,sizeof(link));
        assert(cepheus_gmu_hfi_bind(&link,&workspace,&transport)<0 && !link.bound && checks==n);
    }
    reset();memset(&link,0,sizeof(link));assert(!cepheus_gmu_hfi_bind(&link,&workspace,&transport));
    table[56]=ceph_hfi_header(1,3,0,1);table[57]=123;table[58]=456;table[17]=4;
    checks=0;uint32_t out[4];unsigned count;
    assert(!ceph_hfi_read(&link.tx,out,4,&count) && count==3);unsigned read_boundaries=checks;
    for(unsigned n=1;n<=read_boundaries;++n) {
        reset();memset(&link,0,sizeof(link));assert(!cepheus_gmu_hfi_bind(&link,&workspace,&transport));
        table[56]=ceph_hfi_header(1,3,0,1);table[57]=123;table[58]=456;table[17]=4;
        checks=0;expire=n;for(unsigned i=0;i<4;++i)out[i]=0xdeadbeef;
        assert(ceph_hfi_read(&link.tx,out,4,&count)<0 && !count && link.lease_failed && checks==n);
        for(unsigned i=0;i<4;++i)assert(out[i]==0xdeadbeef);
        expire=0;unsigned before=checks;assert(ceph_hfi_read(&link.tx,out,4,&count)<0 && checks==before);
    }
    reset();memset(&link,0,sizeof(link));assert(!cepheus_gmu_hfi_bind(&link,&workspace,&transport));
    checks=0;uint32_t msg[]={ceph_hfi_header(1,3,0,1),123,456};
    assert(!ceph_hfi_write(&link.tx,msg,3));unsigned write_boundaries=checks;
    for(unsigned n=1;n<=write_boundaries;++n) {
        reset();memset(&link,0,sizeof(link));assert(!cepheus_gmu_hfi_bind(&link,&workspace,&transport));
        checks=0;expire=n;
        assert(ceph_hfi_write(&link.tx,msg,3)<0 && link.lease_failed && checks==n);
    }
    for(unsigned field=0;field<13;++field) {
        reset();memset(&link,0,sizeof(link));assert(!cepheus_gmu_hfi_bind(&link,&workspace,&transport));
        switch(field) {
        case 0:workspace.publication.ttbr0^=0x1000;break;
        case 1:workspace.publication.cb++;break;
        case 2:workspace.publication.handles[1]++;break;
        case 3:status.identity.asid++;break;
        case 4:status.identity.smr^=1;break;
        case 5:status.pool_terminal=1;break;
        case 6:fail_status=1;break;
        case 7:workspace.state=CE_WS_RETAINED;break;
        case 8:workspace.views[2].cpu=table+1024;break;
        case 9:workspace.views[2].bytes++;break;
        case 10:workspace.views[2].pa++;break;
        case 11:workspace.layout.start.hfi_va+=4096;break;
        default:link.tx.words=table+1024;break;
        }
        assert(ceph_hfi_write(&link.tx,msg,3)<0 && link.lease_failed && !table[17]);
    }
    reset();memset(&link,0,sizeof(link));flip_identity=1;
    assert(cepheus_gmu_hfi_bind(&link,&workspace,&transport)<0 && !link.bound);
    for(unsigned n=1;n<=bind_boundaries;++n) {
        reset();memset(&link,0xa5,sizeof(link));link.bound=0;
        struct ceph_gmu_hfi_link before=link;flip_identity=n;
        assert(cepheus_gmu_hfi_bind(&link,&workspace,&transport)<0 && !memcmp(&link,&before,sizeof(link)));
    }
    for(unsigned n=1;n<=write_boundaries;++n) {
        reset();memset(&link,0,sizeof(link));assert(!cepheus_gmu_hfi_bind(&link,&workspace,&transport));
        checks=0;flip_identity=n;
        assert(ceph_hfi_write(&link.tx,msg,3)<0 && link.lease_failed && checks==n);
    }
    assert(!munmap(table,20480));
    printf("Actual HFI link: %u bind/%u read/%u write PROT_NONE owner boundaries, complete publication identity, latched failures and output atomicity PASS (synthetic pool)\n",bind_boundaries,read_boundaries,write_boundaries);
}
