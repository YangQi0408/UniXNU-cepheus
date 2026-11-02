#include <assert.h>
#include <stdio.h>
#include "cepheus_gmu_hfi_link.h"
#include "cepheus_hfi_messages.h"
#include "cepheus_smmu_plan.h"
struct firmware {struct ceph_hfi_queue rx,tx;uint64_t now;int fail;};
static int okay(void *p){(void)p;return 0;}
static int validate(void *p){return ((struct firmware *)p)->fail;}
static int tick(void *p,uint64_t *t){struct firmware *f=p;f->now+=10000;*t=f->now;return 0;}
static int notify(void *p){struct firmware *f=p;uint32_t cmd[8];unsigned count;
    assert(!ceph_hfi_read(&f->rx,cmd,8,&count) && count==2 && (cmd[0]&255)==1);
    uint32_t ack[]={ceph_hfi_header(1,4,1,0),cmd[0],0,0x20000042};return ceph_hfi_write(&f->tx,ack,4);}
int ceph_test_hfi_link(void)
{
    struct ceph_gmu_workspace w={0};assert(!cepheus_gmu_workspace_prepare(&w));
    struct ceph_smmu_stream streams[8]={0};for(unsigned i=0;i<8;++i)streams[i].s2cr=2u<<16;
    struct ceph_smmu_plan p;assert(!ceph_smmu_gmu_plan(streams,8,2,3,8,5,4,w.table.pa,0,&p));
    struct ceph_gmu_hfi_link l={0};struct firmware fw={0};
    struct ceph_gmu_hfi_transport b={&fw,validate,notify,tick,okay,okay};
    assert(cepheus_gmu_hfi_bind(&l,&w,&b)<0);assert(!cepheus_gmu_workspace_share(&w,&p));
    uint32_t *table=w.views[2].cpu;table[6+3]=1;assert(cepheus_gmu_hfi_bind(&l,&w,&b)<0);table[6+3]=1024;
    assert(!cepheus_gmu_hfi_bind(&l,&w,&b));
    fw.rx=l.tx;fw.rx.io=(struct ceph_hfi_ops){&fw,okay,okay,okay,okay};
    fw.tx=l.rx;fw.tx.io=fw.rx.io;
    uint32_t cmd[2],reply[8];unsigned count;ceph_hfi_fw_version(cmd,2,2,0);
    assert(!ceph_hfi_exchange(&l.session,cmd,2,reply,8,&count) && count==4 && reply[3]==0x20000042);
    assert(table[16]==4 && table[17]==4 && table[28]==4 && table[29]==4); /* actual shared header indices */
    fw.fail=1;assert(ceph_hfi_exchange(&l.session,cmd,2,reply,8,&count)<0 && l.session.poisoned);
    cepheus_gmu_hfi_invalidate(&l);assert(!l.bound && !l.tx.words && !l.rx.read_index);
    puts("Production HFI link: actual ABI header indices, shared NC workspace, wire exchange and lease-loss refusal PASS");
    return 0; /* intentionally shared, test process exit retains modeled resource */
}
