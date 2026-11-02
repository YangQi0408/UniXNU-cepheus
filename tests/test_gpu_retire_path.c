/* Executes production workspace/pool/retire orchestration with a synthetic
 * firmware ring and fake hardware registers. Failure scenarios are separate
 * processes so retained buffers remain retained, not forcibly cleaned by test. */
#include <assert.h>
#include <string.h>
#include "cepheus_gmu_workspace.h"
#include "cepheus_gmu_slumber.h"
#include "cepheus_gmu_park.h"
#include "cepheus_smmu_detach.h"
#include "cepheus_gmu_hfi_link.h"
struct fake {
    struct ceph_hfi_queue tx,rx,fw_rx,fw_tx;
    uint32_t tr,tw,rr,rw;
    uint32_t gx,state,reset,halt;
    uint64_t smmu[CE_SM_REG_COUNT],time;
    unsigned phase,mode,notifies,detached;
};
static int okay(void *p){(void)p;return 0;}
static int now(void *p,uint64_t *t){struct fake *f=p;f->time+=10000;*t=f->time;return 0;}
static int notify(void *p){struct fake *f=p;uint32_t cmd[3];unsigned n;
    assert(!ceph_hfi_read(&f->fw_rx,cmd,3,&n));
    if((cmd[0]&255)==1){uint32_t version[]={ceph_hfi_header(1,4,1,0),cmd[0],0,0x20000042};assert(!ceph_hfi_write(&f->fw_tx,version,4));return 0;}
    assert(n==3 && (cmd[0]&255)==33);++f->notifies;
    uint32_t ack[]={ceph_hfi_header(33,3,1,0),cmd[0],f->mode==2?1u:0u};
    assert(!ceph_hfi_write(&f->fw_tx,ack,3));f->state=15;f->gx=0xc0;return 0;}
static int slbegin(void *p){struct fake *f=p;f->phase=1;return f->mode==1?-1:0;}
static int slread(void *p,enum ceph_slumber_reg r,uint32_t *v){struct fake *f=p;
    *v=r==CE_SL_RPMH_POWER_STATE?f->state:r==CE_SL_GX_STATUS?f->gx:0;return 0;}
static int slwrite(void *p,enum ceph_slumber_reg r,uint32_t v){(void)p;assert(r==CE_SL_POWER_COUNTER_ENABLE && !v);return 0;}
static void slend(void *p,int done){struct fake *f=p;if(done)f->phase=2;}
static int pkbegin(void *p){struct fake *f=p;assert(f->phase==2);return f->mode==3?-1:0;}
static int pkread(void *p,enum ceph_gmu_park_reg r,uint32_t *v){struct fake *f=p;
    *v=r==CE_PARK_GX_STATUS?f->gx:r==CE_PARK_SYSRESET?f->reset:
        (r==CE_PARK_GBIF_ACK||r==CE_PARK_GBIF_HALT)?f->halt:1;return 0;}
static int pkwrite(void *p,enum ceph_gmu_park_reg r,uint32_t v){struct fake *f=p;
    if(r==CE_PARK_SYSRESET)f->reset=v;else if(r==CE_PARK_GBIF_HALT)f->halt=v;else assert(0);return 0;}
static void pkend(void *p,int done){struct fake *f=p;if(done)f->phase=3;}
static int dtbegin(void *p,const struct ceph_smmu_plan *plan){struct fake *f=p;assert(f->phase==3 && f->reset==1 && f->halt==2 && plan->cb==3);return 0;}
static int dtread(void *p,enum ceph_smmu_reg r,uint64_t *v){struct fake *f=p;*v=f->smmu[r];return 0;}
static int dtwrite(void *p,enum ceph_smmu_reg r,uint64_t v){struct fake *f=p;f->smmu[r]=v;return f->mode==4?-1:0;}
static int delayus(void *p,unsigned u){(void)p;assert(u==1);return 0;}
static void dtend(void *p,int done){struct fake *f=p;f->detached=(unsigned)done;if(done)f->phase=4;}

int ceph_test_retire_path(unsigned mode)
{
    struct ceph_gmu_workspace w={0};assert(!cepheus_gmu_workspace_prepare(&w));
    struct ceph_smmu_stream streams[8]={0};for(unsigned i=0;i<8;++i)streams[i].s2cr=2u<<16;
    struct ceph_smmu_plan plan;assert(!ceph_smmu_gmu_plan(streams,8,2,3,8,5,4,w.table.pa,0,&plan));
    /* Duplicate handles, wrong root and extra views refuse atomically. */
    uint64_t bad[CE_GPU_WORKSPACE_BUFFERS];for(unsigned i=0;i<CE_GPU_WORKSPACE_BUFFERS;++i)bad[i]=w.handles[i];bad[1]=bad[0];
    struct ceph_gpu_publication publication;
    assert(cepheus_gpu_memory_share_workspace(bad,&plan,&publication)<0);
    struct ceph_gpu_memory_view extra;assert(!cepheus_gpu_memory_view_get(w.handles[0],&extra));
    assert(cepheus_gmu_workspace_share(&w,&plan)<0);assert(!cepheus_gpu_memory_view_put(w.handles[0]));
    assert(!cepheus_gmu_workspace_share(&w,&plan) && w.state==CE_WS_SHARED && w.leased==CE_GPU_WORKSPACE_VIEWS);
    assert(cepheus_gpu_memory_release_cpu(w.handles[0])<0 && cepheus_gpu_memory_view_put(w.handles[0])<0);
    assert(cepheus_gpu_memory_view_get(w.handles[0],&extra)<0);
    struct fake f={.mode=mode};struct ceph_gmu_hfi_link link={0};
    struct ceph_gmu_hfi_transport transport={&f,okay,notify,now,okay,okay};
    assert(!cepheus_gmu_hfi_bind(&link,&w,&transport));
    f.fw_rx=link.tx;f.fw_rx.io=(struct ceph_hfi_ops){&f,okay,okay,okay,okay};
    f.fw_tx=link.rx;f.fw_tx.io=f.fw_rx.io;
    uint32_t version_cmd[2],version_reply[8];unsigned version_count;
    ceph_hfi_fw_version(version_cmd,2,2,0);
    assert(!ceph_hfi_exchange(&link.session,version_cmd,2,version_reply,8,&version_count));
    struct ceph_gmu_slumber_ops sl={&f,slbegin,okay,slread,slwrite,okay,now,okay,slend};
    struct ceph_gmu_park_ops pk={&f,pkbegin,okay,pkread,pkwrite,okay,now,okay,pkend};
    struct ceph_smmu_detach_ops dt={&f,dtbegin,okay,dtread,dtwrite,okay,delayus,dtend};
    f.smmu[CE_SM_SMR]=plan.smr;f.smmu[CE_SM_S2CR]=plan.s2cr_translate;f.smmu[CE_SM_SCTLR]=plan.sctlr;
    f.smmu[CE_SM_TTBR0]=plan.ttbr0;f.smmu[CE_SM_TTBR1]=plan.ttbr1;f.smmu[CE_SM_TCR]=plan.tcr;f.smmu[CE_SM_TCR2]=plan.tcr2;
    f.smmu[CE_SM_MAIR0]=plan.mair0;f.smmu[CE_SM_MAIR1]=plan.mair1;f.smmu[CE_SM_CBAR]=plan.cbar;f.smmu[CE_SM_CBA2R]=plan.cba2r;
    struct ceph_gpu_retire_ops ops={&sl,&pk,&dt,&link.session,&plan,3,1,12,8};
    struct ceph_gpu_publication forged=w.publication;forged.group^=0x100;
    assert(cepheus_gpu_memory_retire_workspace(&forged,&ops)<0 && !f.phase);
    forged=w.publication;forged.handles[1]=forged.handles[0];
    assert(cepheus_gpu_memory_retire_workspace(&forged,&ops)<0 && !f.phase);
    struct ceph_gpu_group_status status;
    uint64_t group=w.publication.group;
    assert(!cepheus_gpu_memory_group_status(group,&status) && status.state==CE_GPU_GROUP_SHARED && status.retained_buffers==CE_GPU_WORKSPACE_BUFFERS);
    int e=cepheus_gmu_workspace_retire(&w,&ops);
    cepheus_gmu_hfi_invalidate(&link);
    assert(!w.table.words && !w.leased && !w.views[2].cpu);
    if(!mode)assert(!e && w.state==CE_WS_RELEASED && f.detached && f.notifies==1 && !w.handles[0]);
    else {
        assert(e && w.state==CE_WS_RETAINED);
        assert(!cepheus_gpu_memory_group_status(group,&status) && status.pool_terminal);
        assert(status.identity.ttbr0==plan.ttbr0 && status.retained_buffers==CE_GPU_WORKSPACE_BUFFERS);
        assert(status.state==(mode==5?CE_GPU_GROUP_DETACHED_RETAINED:CE_GPU_GROUP_UNCERTAIN));
        assert(f.detached==(mode==5));
        uint64_t handle;assert(cepheus_gpu_memory_alloc(4096,&handle)<0);
    }
    if(!mode)assert(cepheus_gpu_memory_group_status(group,&status)<0);
    assert(cepheus_gmu_workspace_retire(&w,&ops)<0);
    return e;
}
