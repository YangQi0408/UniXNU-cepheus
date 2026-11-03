/* Whole production bringup chain with fake hardware and REAL local firmware.
 * No firmware embedded in source. Unavailable fixture -> explicit skip.
 * Each fault mode runs in a fresh process: don't free potentially live buffers.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cepheus_gmu_bringup.h"
#include "cepheus_gpu_config.h"
#include "gpu_config_fixture.h"
struct hw {
    struct ceph_gmu_bringup *s;unsigned mode,phase,votes,commands,failed;
    uint32_t power[CE_PWR_REG_COUNT],gmu[CE_GMU_REG_COUNT],pdc[3][0xc000];
    uint64_t smmu[CE_SM_REG_COUNT],time;
    uint32_t itcm[4096],dtcm[4096],qmp[64];unsigned qmp_notified;struct ceph_hfi_queue fw_rx,fw_tx;
};
static int guard(void *p){struct hw *h=p;assert(h->s);return 0;}
static int now(void *p,uint64_t *n){struct hw *h=p;h->time+=10000;*n=h->time;return 0;}
static int delayus(void *p,unsigned n){struct hw *h=p;h->time+=n*1000;return 0;}
static void end(void *p,int keep){struct hw *h=p;assert(h->s);(void)keep;}
static int power_prepare(void *p,struct ceph_power_input *i){struct hw *h=p;if(h->mode==1)return -11;
    i->cx=(struct ceph_arc_resource){.address=0x30000,.levels={0,16,48,64,128,192,224,256,320},.count=9};
    i->mx=i->cx;i->mx.address=0x30004;i->current_cx=i->current_mx=1;i->parent_hz=300000000;h->phase=1;return 0;}
static int power_read(void *p,enum ceph_power_reg r,uint32_t *v){*v=((struct hw *)p)->power[r];return 0;}
static int power_write(void *p,enum ceph_power_reg r,uint32_t v){struct hw *h=p;assert(h->votes==2);h->power[r]=v;
    if(r==CE_PWR_CX && !(v&1)){h->power[CE_PWR_CX_STATUS]=1u<<31;for(unsigned c=CE_PWR_GMU;c<=CE_PWR_DVM;++c)if(h->power[c]&1)h->power[c]&=~(1u<<31);}
    if(r==CE_PWR_RCG){h->power[r]&=~1u;if((v&2)||(h->power[CE_PWR_GMU]&1))h->power[r]&=~(1u<<31);else h->power[r]|=1u<<31;}
    if(r>=CE_PWR_GMU && r<=CE_PWR_DVM && v&1)h->power[r]&=~(1u<<31);
    if(r==CE_PWR_GMU)h->power[CE_PWR_RCG]&=~(1u<<31);
    return 0;}
static int vote(void *p,uint32_t address,unsigned v){struct hw *h=p;assert(v==2 && address==(h->votes?0x30000u:0x30004u));h->votes++;return 0;}
static int pdc_begin(void *p,const struct ceph_pdc_input *i){struct hw *h=p;assert(h->s->workspace.state==CE_WS_SHARED && i->cfg_pa==0xb290000 && h->gmu[CE_GMU_TCM_RETENTION]==1);return h->mode==3?-13:0;}
static int pdc_read(void *p,enum ceph_pdc_window w,uint32_t o,uint32_t *v){*v=((struct hw *)p)->pdc[w][o/4];return 0;}
static int pdc_write(void *p,enum ceph_pdc_window w,uint32_t o,uint32_t v){struct hw *h=p;h->pdc[w][o/4]=v;return 0;}
static int smmu_begin(void *p,const struct ceph_smmu_plan *plan,uint32_t actlr){struct hw *h=p;assert(h->s->pdc.attempted==48 && actlr==0x303 && plan->cb==3);return h->mode==4?-14:0;}
static int smmu_read(void *p,enum ceph_smmu_reg r,uint64_t *v){*v=((struct hw *)p)->smmu[r];return 0;}
static int smmu_write(void *p,enum ceph_smmu_reg r,uint64_t v){struct hw *h=p;h->smmu[r]=v;if(r==CE_SM_TLBSYNC)h->smmu[CE_SM_TLBSTATUS]=0;return 0;}
static int upload_begin(void *p){struct hw *h=p;assert(h->s->smmu.state==CE_SM_ENABLED);return h->mode==5?-15:0;}
static int reset_held(void *p){struct hw *h=p;return h->gmu[CE_GMU_SYSRESET]==1?0:-1;}
static int cm3_begin(void *p,const struct ceph_gmu_start_config *c){struct hw *h=p;
    assert(h->s->backend.upload.writes && c->hfi_va==0x60100000 && c->log_va==0x60200000);return h->mode==6?-16:0;}
static int cm3_read(void *p,enum ceph_gmu_reg r,uint32_t *v){*v=((struct hw *)p)->gmu[r];return 0;}
static int cm3_write(void *p,enum ceph_gmu_reg r,uint32_t v){struct hw *h=p;h->gmu[r]=v;
    if(r==CE_GMU_SYSRESET && !v)h->gmu[CE_GMU_INIT_RESULT]=0xbabeface;
    if(r==CE_GMU_HFI_INIT)h->gmu[CE_GMU_HFI_STATUS]=1;
    return 0;}
static int notify(void *p){struct hw *h=p;assert(h->s->cm3.state==CE_GS_HFI_READY);uint32_t cmd[255];unsigned count;
    h->fw_rx=h->s->link.tx;h->fw_tx=h->s->link.rx;
    h->fw_rx.io=(struct ceph_hfi_ops){h,guard,guard,guard,guard};h->fw_tx.io=h->fw_rx.io;
    assert(!ceph_hfi_read(&h->fw_rx,cmd,255,&count));
    const unsigned ids0[]={1,4,3,11,14},ids1[]={1,4,3,11,7,11,11,14};
    const unsigned *ids=h->mode==8?ids1:ids0;unsigned total=h->mode==8?8:5;
    assert(h->commands<total && (cmd[0]&255)==ids[h->commands]);h->commands++;
    uint32_t expected[160];int words=0;const struct ceph_hfi_boot_config *cfg=&h->s->backend.hfi;
    switch(cmd[0]&255) {
    case 4: words=ceph_hfi_perf(expected,160,cfg->gx,cfg->ng,cfg->cx,cfg->nc,cfg->gfx,cfg->cxl,cfg->mx);break;
    case 3: words=ceph_hfi_bandwidth(expected,160,cfg->bw);assert(cmd[1]==12 && cmd[2]==3 && cmd[3]==3);break;
    case 7: words=ceph_hfi_acd_table(expected,160,cfg->acd);assert(cmd[5]==2687328253u && cmd[16]==21521);break;
    }
    if(words){assert(words==(int)count);assert(!memcmp(cmd+1,expected+1,(count-1)*4));}
    unsigned n=(cmd[0]&255)==1?4:3;uint32_t ack[]={ceph_hfi_header(cmd[0]&255,n,1,0),cmd[0],h->mode==7?1u:0u,0x20000042};
    return ceph_hfi_write(&h->fw_tx,ack,n);}
static void failed(void *p,enum ceph_bringup_stage stage,int e){struct hw *h=p;assert(e && stage>=CE_BR_POWER);h->failed=1;}
static int qbegin(void *p,struct ceph_qmp_layout *l){(void)p;*l=(struct ceph_qmp_layout){64,128};return 0;}
static int qread(void *p,uint32_t off,uint32_t *v){struct hw *h=p;*v=h->qmp[off/4];return 0;}
static int qwrite(void *p,uint32_t off,uint32_t v){struct hw *h=p;h->qmp[off/4]=v;return 0;}
static int qnotify(void *p){struct hw *h=p;assert(h->qmp[16]==32 && !memcmp(h->qmp+17,"{class: gpu, res: acd, value: 1}",32));h->qmp[16]=0;h->qmp_notified=1;return 0;}
static int bind(void *p,struct ceph_gmu_bringup *s){struct hw *h=p;assert(h->s==s && s->power.state==CE_PWR_READY);if(h->mode==2)return -12;
    struct ceph_smmu_stream streams[16]={0};for(unsigned i=0;i<16;++i)streams[i].s2cr=2u<<16;
    assert(!ceph_smmu_gmu_plan(streams,16,2,3,8,5,4,s->workspace.table.pa,0,&s->backend.smmu_plan));
    s->backend.upload=(struct ceph_gmu_upload){h->itcm,h->dtcm,4096,4096,NULL,0,h,upload_begin,reset_held,guard,end,0,0};return 0;}
int ceph_test_bringup(const char *path,unsigned mode)
{
    static struct ceph_gmu_bringup s;static struct hw h;h.s=&s;h.mode=mode;
    FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long size=ftell(f);assert(size==37680);rewind(f);
    uint8_t *data=malloc((size_t)size);assert(data);assert(fread(data,1,(size_t)size,f)==(size_t)size);fclose(f);
    assert(!cepheus_gmu_bringup_prepare(&s,data,(size_t)size));free(data);
    struct ceph_gmu_bringup_backends *b=&s.backend;
    b->owner=&h;b->bind=bind;b->failed=failed;
    b->power=(struct ceph_power_ops){&h,power_prepare,guard,power_read,power_write,vote,delayus,end};
    b->pdc=(struct ceph_pdc_ops){&h,pdc_begin,guard,pdc_read,pdc_write,guard,end};
    b->smmu=(struct ceph_smmu_ops){&h,smmu_begin,guard,smmu_read,smmu_write,guard,guard,delayus,end};
    b->cm3=(struct ceph_gmu_start_ops){&h,cm3_begin,guard,cm3_read,cm3_write,guard,now,guard,end};
    b->transport=(struct ceph_gmu_hfi_transport){&h,guard,notify,now,guard,guard};
    config_fixture();struct ceph_arc_db db={config_db,sizeof(config_db),config_read};
    struct ceph_gpu_config config;
    assert(!ceph_gpu_config_build(&db,0x06040001,mode==8?0x20000000:0,&config));
    assert(!ceph_gpu_config_hfi(&config,0x20000001,&b->hfi));
    b->acd=(struct ceph_qmp_ops){&h,qbegin,guard,qread,qwrite,guard,qnotify,now,guard,end};
    b->pdc_input=config.pdc;
    h.power[CE_PWR_CX]=h.power[CE_PWR_GX]=0x222001;h.power[CE_PWR_CX_STATUS]=0x5ff4;h.power[CE_PWR_CFG_AHB]=1;
    h.power[CE_PWR_RCG]=0x80000000;h.power[CE_PWR_GMU]=0x80004220;h.power[CE_PWR_CM3_RESET]=1;
    for(unsigned i=CE_PWR_CXO;i<=CE_PWR_DVM;++i)h.power[i]=0x80000001;
    h.smmu[CE_SM_S2CR]=2u<<16;h.gmu[CE_GMU_SYSRESET]=1;h.gmu[CE_GMU_HFI_VERSION]=0x20000001;
    int e=cepheus_gmu_bringup_start(&s);
    if(!mode || mode==8)assert(!e && !h.failed && s.stage==CE_BR_HFI && s.fw_version==0x20000042 && h.commands==(mode==8?8u:5u) && !s.firmware && h.qmp_notified==(mode==8));
    else {
        assert(e && h.failed && s.stage==CE_BR_FAILED);
        if(mode>=3) {
            struct ceph_gpu_group_status status;
            assert(s.workspace.state==CE_WS_RETAINED && !s.workspace.table.words && !s.workspace.views[0].cpu);
            assert(!cepheus_gpu_memory_group_status(s.workspace.publication.group,&status));
            assert(status.state==CE_GPU_GROUP_UNCERTAIN && status.pool_terminal && status.retained_buffers==CE_GPU_WORKSPACE_BUFFERS);
        }
    }
    printf("Whole GMU chain mode=%u: sealed file/workspace -> power -> PDC -> SMMU -> upload -> CM3 -> actual HFI rings PASS (emulated hardware)\n",mode);
    return e;
}
