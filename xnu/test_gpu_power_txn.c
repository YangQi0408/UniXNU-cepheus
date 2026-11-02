#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cepheus_gpu_power_txn.h"
struct mock {
    struct ceph_power_input in;uint32_t reg[CE_PWR_REG_COUNT];
    unsigned call,fail,finishes,kept,writes,votes,delays;int timeout,live;
    uint32_t vote_addr[2];unsigned vote_index[2];
};
static int step(struct mock *m) {return ++m->call==m->fail ? -9 : 0;}
static int prepare(void *p,struct ceph_power_input *in) {struct mock *m=p;int e=step(m);if(e)return e;m->live=1;*in=m->in;return 0;}
static int valid(void *p) {struct mock *m=p;assert(m->live);return step(m);}
static int readreg(void *p,enum ceph_power_reg r,uint32_t *v) {struct mock *m=p;int e=step(m);if(e)return e;*v=m->reg[r];return 0;}
static int writereg(void *p,enum ceph_power_reg r,uint32_t v)
{
    struct mock *m=p;assert(r!=CE_PWR_GX && r!=CE_PWR_CM3_RESET && r!=CE_PWR_CX_STATUS);
    ++m->writes;m->reg[r]=v; /* even a failed transport can write */
    int e=step(m);if(e)return e;
    if(!m->timeout) {
        if(r==CE_PWR_CX && !(v&1)) {
            m->reg[CE_PWR_CX_STATUS]|=1u<<31;
            for(unsigned c=CE_PWR_GMU;c<=CE_PWR_DVM;++c)
                if(m->reg[c]&1u) m->reg[c]&=~(1u<<31);
        }
        if(r==CE_PWR_RCG) m->reg[r]&=~1u;
        if(r>=CE_PWR_GMU && r<=CE_PWR_DVM && (v&1)) m->reg[r]&=~(1u<<31);
        if(r==CE_PWR_GMU) m->reg[CE_PWR_RCG]&=~(1u<<31);
    }
    return 0;
}
static int vote(void *p,uint32_t addr,unsigned i)
{
    struct mock *m=p;assert(m->votes<2);m->vote_addr[m->votes]=addr;m->vote_index[m->votes++]=i;return step(m);
}
static int delayus(void *p,unsigned us) {struct mock *m=p;m->delays+=us;assert(us<=2);return step(m);}
static void finish(void *p,int keep) {struct mock *m=p;assert(m->live);m->live=0;m->finishes++;m->kept=(unsigned)keep;}
static struct mock fixture(void)
{
    struct mock m={0};
    m.in.cx=(struct ceph_arc_resource){.address=0x30000,.levels={0,16,48,64},.count=4};
    m.in.mx=m.in.cx;m.in.mx.address=0x30004;m.in.current_cx=m.in.current_mx=1;m.in.parent_hz=300000000;
    m.reg[CE_PWR_CX]=m.reg[CE_PWR_GX]=0x222001;m.reg[CE_PWR_CX_STATUS]=0x5ff4;m.reg[CE_PWR_CFG_AHB]=1;
    m.reg[CE_PWR_RCG]=0x80000000;m.reg[CE_PWR_GMU]=0x80004220;
    for(unsigned r=CE_PWR_CXO;r<=CE_PWR_DVM;++r)m.reg[r]=0x80000001;
    m.reg[CE_PWR_CM3_RESET]=1;
    return m;
}
static struct ceph_power_ops ops(struct mock *m)
{return (struct ceph_power_ops){m,prepare,valid,readreg,writereg,vote,delayus,finish};}
int main(void)
{
    struct mock m=fixture();struct ceph_power_ops b=ops(&m);struct ceph_power_txn t={0};
    assert(!ceph_power_start(&t,&b) && t.state==CE_PWR_READY && m.kept && m.finishes==1);
    assert(m.votes==2 && m.vote_addr[0]==0x30004 && m.vote_addr[1]==0x30000);
    assert(m.vote_index[0]==2 && m.vote_index[1]==2 && m.reg[CE_PWR_RCG_CFG]==0x602);
    unsigned total=m.call;assert(ceph_power_start(&t,&b)==CE_PWR_INVALID && m.call==total);
    for(unsigned f=1;f<=total;++f) {
        m=fixture();m.fail=f;b=ops(&m);t=(struct ceph_power_txn){0};
        assert(ceph_power_start(&t,&b)!=0 && t.state!=CE_PWR_READY);
        if(f==1) assert(!m.finishes && !m.writes && !m.votes);
        else assert(m.finishes==1 && m.kept==(t.effects!=0));
        assert((t.state==CE_PWR_QUARANTINED)==(t.effects!=0));
    }
    m=fixture();m.in.current_cx=3;struct ceph_power_plan plan;
    assert(!ceph_power_plan(&m.in,&plan) && plan.cx_index==3 && plan.mx_index==3);
    m.in.mx.count=3;assert(ceph_power_plan(&m.in,&plan)==CE_PWR_INVALID);
    m=fixture();m.in.parent_hz=0;b=ops(&m);t=(struct ceph_power_txn){0};
    assert(ceph_power_start(&t,&b)!=0 && !t.effects && !m.kept);
    m=fixture();m.timeout=1;b=ops(&m);t=(struct ceph_power_txn){0};
    assert(ceph_power_start(&t,&b)==CE_PWR_TIMEOUT && t.state==CE_PWR_QUARANTINED && m.delays<=501);
    m=fixture();m.reg[CE_PWR_CM3_RESET]=0;b=ops(&m);t=(struct ceph_power_txn){0};
    assert(ceph_power_start(&t,&b)==CE_PWR_BUSY && t.state==CE_PWR_QUARANTINED);
    const enum ceph_power_reg bad[]={CE_PWR_CX,CE_PWR_CX_STATUS,CE_PWR_GX,CE_PWR_CFG_AHB,CE_PWR_GMU,CE_PWR_RCG};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        m=fixture();m.reg[bad[i]]=UINT32_MAX;b=ops(&m);t=(struct ceph_power_txn){0};
        assert(ceph_power_start(&t,&b)!=0 && !t.effects);
    }
    puts("GPU power transaction: monotone MX/CX, bounded polls, every-call failure quarantine PASS (mock; no backend)");
}
