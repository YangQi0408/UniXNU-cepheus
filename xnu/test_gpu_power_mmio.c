#define _DEFAULT_SOURCE 1
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "cepheus_gpu_power_mmio.h"

struct fixture {
    struct ceph_power_mmio mmio;
    unsigned checks,expire_at,writes,votes,kept,finished,reset_reads;
    int live,gmu_access,timeout;
};
static int lease(void *p)
{
    struct fixture *f=p;
    ++f->checks;
    return !f->live || (f->expire_at && f->checks>=f->expire_at) ? -1:0;
}
static volatile uint32_t *regptr(struct fixture *f,enum ceph_power_reg reg)
{
    /* Independent offsets: catches adapter enum/aperture mixups. */
    static const uint32_t offsets[]={0x106c,0x1540,0x100c,0x71004,0x1120,
        0x1124,0x1098,0x109c,0x71154,0x7100c,0x1078,0x71018,0x14000};
    assert(reg>=0 && reg<CE_PWR_REG_COUNT);
    volatile uint8_t *base=reg==CE_PWR_CM3_RESET?f->mmio.gmu.base:
        reg==CE_PWR_CFG_AHB || reg==CE_PWR_DDR || reg==CE_PWR_MEMNOC ||
        reg==CE_PWR_DVM?f->mmio.gcc.base:f->mmio.cc.base;
    return (volatile uint32_t *)(base+offsets[reg]);
}
static void protect_gmu(struct fixture *f,int accessible)
{
    assert(!mprotect((void *)f->mmio.gmu.base,f->mmio.gmu.bytes,
        accessible?PROT_READ|PROT_WRITE:PROT_NONE));
    f->gmu_access=accessible;
}
static void reset(struct fixture *f)
{
    protect_gmu(f,1);
    memset((void *)f->mmio.gcc.base,0,f->mmio.gcc.bytes);
    memset((void *)f->mmio.cc.base,0,f->mmio.cc.bytes);
    memset((void *)f->mmio.gmu.base,0,f->mmio.gmu.bytes);
    f->checks=f->expire_at=f->writes=f->votes=f->kept=f->finished=f->reset_reads=0;
    f->live=1;f->timeout=0;
    *regptr(f,CE_PWR_CX)=*regptr(f,CE_PWR_GX)=0x222001;
    *regptr(f,CE_PWR_CX_STATUS)=0x5ff4;
    *regptr(f,CE_PWR_CFG_AHB)=1;
    *regptr(f,CE_PWR_RCG)=0x80000000;
    const enum ceph_power_reg branches[]={CE_PWR_GMU,CE_PWR_CXO,CE_PWR_DDR,
        CE_PWR_MEMNOC,CE_PWR_AHB,CE_PWR_DVM};
    for(unsigned i=0;i<sizeof(branches)/sizeof(branches[0]);++i)
        *regptr(f,branches[i])=0x80004220;
    *regptr(f,CE_PWR_CM3_RESET)=1;
    protect_gmu(f,0);
}
static void init(struct fixture *f)
{
    memset(f,0,sizeof(*f));
    struct ceph_power_window *windows[]={&f->mmio.gcc,&f->mmio.cc,&f->mmio.gmu};
    const uint64_t pa[]={0x100000,0x2c90000,0x2c6a000};
    const size_t bytes[]={0x90000,0x9000,0x30000};
    for(unsigned i=0;i<3;++i) {
        void *base=mmap(NULL,bytes[i],PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        assert(base!=MAP_FAILED);
        *windows[i]=(struct ceph_power_window){base,pa[i],bytes[i]};
    }
    f->mmio.owner=f;f->mmio.lease=lease;reset(f);
}
static void destroy(struct fixture *f)
{
    assert(!munmap((void *)f->mmio.gcc.base,f->mmio.gcc.bytes));
    assert(!munmap((void *)f->mmio.cc.base,f->mmio.cc.bytes));
    assert(!munmap((void *)f->mmio.gmu.base,f->mmio.gmu.bytes));
}
static int prepare(void *p,struct ceph_power_input *in)
{
    struct fixture *f=p;
    if(lease(f))return -1;
    *in=(struct ceph_power_input){
        .cx={.address=0x30000,.levels={0,16,64,128},.count=4},
        .mx={.address=0x30010,.levels={0,16,128,192},.count=4},
        .current_cx=1,.current_mx=1,.parent_hz=300000000};
    return 0;
}
static int readreg(void *p,enum ceph_power_reg reg,uint32_t *value)
{
    struct fixture *f=p;
    if(reg==CE_PWR_CM3_RESET) {
        assert(f->gmu_access); /* old pre-power CM3 read fails here */
        ++f->reset_reads;
    }
    return ceph_power_mmio_read(&f->mmio,reg,value);
}
static int writereg(void *p,enum ceph_power_reg reg,uint32_t value)
{
    struct fixture *f=p;
    int e=ceph_power_mmio_write(&f->mmio,reg,value);
    if(e)return e;
    ++f->writes;
    assert(*regptr(f,reg)==value);
    if(f->timeout)return 0;
    /* Hardware ACK model follows the actual volatile store, never replaces it. */
    if(reg==CE_PWR_CX && !(value&1u))*regptr(f,CE_PWR_CX_STATUS)|=1u<<31;
    if(reg==CE_PWR_RCG) {
        *regptr(f,reg)&=~1u;
        if((value&2u) || (*regptr(f,CE_PWR_GMU)&1u))*regptr(f,reg)&=~(1u<<31);
        else *regptr(f,reg)|=1u<<31;
    }
    if(reg==CE_PWR_GMU || reg==CE_PWR_CXO || reg==CE_PWR_DDR ||
        reg==CE_PWR_MEMNOC || reg==CE_PWR_AHB || reg==CE_PWR_DVM)
        *regptr(f,reg)&=~(1u<<31);
    if(reg==CE_PWR_GMU)*regptr(f,CE_PWR_RCG)&=~(1u<<31);
    if(!ceph_power_mmio_gmu_ready(&f->mmio))protect_gmu(f,1);
    return 0;
}
static int vote(void *p,uint32_t address,unsigned index)
{
    struct fixture *f=p;
    assert(index==2 && address==(f->votes?0x30000u:0x30010u));
    ++f->votes;return 0;
}
static int delayus(void *p,unsigned us){(void)p;assert(us && us<=2);return 0;}
static void finish(void *p,int keep)
{struct fixture *f=p;f->finished++;f->kept=(unsigned)keep;f->live=0;}
static struct ceph_power_ops ops(struct fixture *f)
{return (struct ceph_power_ops){f,prepare,lease,readreg,writereg,vote,delayus,finish};}

int main(void)
{
    struct fixture f;init(&f);uint32_t v=0xa5a5a5a5;
    /* Inaccessible apertures make missing power gates a process fault. */
    assert(ceph_power_mmio_read(&f.mmio,CE_PWR_CM3_RESET,&v)<0 && v==0xa5a5a5a5);
    *regptr(&f,CE_PWR_CFG_AHB)=0x80000000;
    assert(!mprotect((void *)f.mmio.cc.base,f.mmio.cc.bytes,PROT_NONE));
    for(unsigned r=0;r<CE_PWR_REG_COUNT;++r) {
        if(r==CE_PWR_CFG_AHB || r==CE_PWR_DDR || r==CE_PWR_MEMNOC || r==CE_PWR_DVM)continue;
        assert(ceph_power_mmio_read(&f.mmio,(enum ceph_power_reg)r,&v)<0);
        assert(ceph_power_mmio_write(&f.mmio,(enum ceph_power_reg)r,1)<0);
    }
    assert(!mprotect((void *)f.mmio.cc.base,f.mmio.cc.bytes,PROT_READ|PROT_WRITE));
    reset(&f);
    struct ceph_power_ops b=ops(&f);struct ceph_power_txn t={0};
    assert(!ceph_power_start(&t,&b) && t.state==CE_PWR_READY && f.kept && f.finished==1);
    assert(f.reset_reads==1 && f.votes==2 && f.writes && *regptr(&f,CE_PWR_RCG_CFG)==0x602);
    unsigned checks=f.checks;
    for(unsigned i=1;i<=checks;++i) {
        reset(&f);f.expire_at=i;b=ops(&f);t=(struct ceph_power_txn){0};
        assert(ceph_power_start(&t,&b) && t.state!=CE_PWR_READY);
        assert(f.kept==(t.effects!=0));
        assert((t.state==CE_PWR_QUARANTINED)==(t.effects!=0));
    }
    reset(&f);f.timeout=1;b=ops(&f);t=(struct ceph_power_txn){0};
    assert(ceph_power_start(&t,&b)==CE_PWR_TIMEOUT && f.kept && !f.reset_reads);
    reset(&f);protect_gmu(&f,1);*regptr(&f,CE_PWR_CM3_RESET)=0;protect_gmu(&f,0);
    b=ops(&f);t=(struct ceph_power_txn){0};
    assert(ceph_power_start(&t,&b)==CE_PWR_BUSY && t.state==CE_PWR_QUARANTINED && f.kept);

    reset(&f);b=ops(&f);t=(struct ceph_power_txn){0};assert(!ceph_power_start(&t,&b));f.live=1;
    /* Each individual missing prerequisite blocks CM3 without accessing it. */
    protect_gmu(&f,0);
    const enum ceph_power_reg gates[]={CE_PWR_CFG_AHB,CE_PWR_CX_STATUS,CE_PWR_GMU,
        CE_PWR_CXO,CE_PWR_AHB,CE_PWR_RCG};
    for(unsigned i=0;i<sizeof(gates)/sizeof(gates[0]);++i) {
        uint32_t saved=*regptr(&f,gates[i]);
        *regptr(&f,gates[i])=gates[i]==CE_PWR_CX_STATUS?0:0x80000000;
        assert(ceph_power_mmio_read(&f.mmio,CE_PWR_CM3_RESET,&v)<0);
        *regptr(&f,gates[i])=UINT32_MAX;
        assert(ceph_power_mmio_read(&f.mmio,CE_PWR_CM3_RESET,&v)<0);
        *regptr(&f,gates[i])=saved;
    }
    *regptr(&f,CE_PWR_RCG)=1;
    assert(ceph_power_mmio_read(&f.mmio,CE_PWR_CM3_RESET,&v)<0);
    *regptr(&f,CE_PWR_RCG)=0;protect_gmu(&f,1);
    for(unsigned r=0;r<CE_PWR_REG_COUNT;++r) {
        uint32_t saved=*regptr(&f,(enum ceph_power_reg)r);
        assert(!ceph_power_mmio_read(&f.mmio,(enum ceph_power_reg)r,&v) && v==saved);
        *regptr(&f,(enum ceph_power_reg)r)=UINT32_MAX;v=0x12345678;
        assert(ceph_power_mmio_read(&f.mmio,(enum ceph_power_reg)r,&v)<0 && v==0x12345678);
        *regptr(&f,(enum ceph_power_reg)r)=saved;
        assert(ceph_power_mmio_write(&f.mmio,(enum ceph_power_reg)r,saved^(1u<<24))<0);
        assert(*regptr(&f,(enum ceph_power_reg)r)==saved);
    }
    const enum ceph_power_reg readonly[]={CE_PWR_GX,CE_PWR_CX_STATUS,CE_PWR_CM3_RESET,CE_PWR_CFG_AHB};
    for(unsigned i=0;i<sizeof(readonly)/sizeof(readonly[0]);++i)
        assert(ceph_power_mmio_write(&f.mmio,readonly[i],*regptr(&f,readonly[i]))<0);
    assert(ceph_power_mmio_write(&f.mmio,CE_PWR_CX,*regptr(&f,CE_PWR_CX)|1)<0);
    assert(ceph_power_mmio_write(&f.mmio,CE_PWR_GMU,*regptr(&f,CE_PWR_GMU)&~1u)<0);
    assert(ceph_power_mmio_write(&f.mmio,CE_PWR_RCG_CFG,0x602)<0);
    assert(ceph_power_mmio_write(&f.mmio,CE_PWR_RCG,1)<0);
    assert(ceph_power_mmio_read(NULL,CE_PWR_CX,&v)<0);
    assert(ceph_power_mmio_read(&f.mmio,CE_PWR_CX,NULL)<0);
    assert(ceph_power_mmio_read(&f.mmio,CE_PWR_REG_COUNT,&v)<0);
    assert(ceph_power_mmio_write(&f.mmio,CE_PWR_REG_COUNT,0)<0);
    struct ceph_power_mmio bad=f.mmio;bad.gcc.pa+=4;
    assert(ceph_power_mmio_read(&bad,CE_PWR_CX,&v)<0);
    bad=f.mmio;bad.cc.bytes-=4;assert(ceph_power_mmio_read(&bad,CE_PWR_CX,&v)<0);
    bad=f.mmio;bad.gmu.base++;assert(ceph_power_mmio_read(&bad,CE_PWR_CX,&v)<0);
    bad=f.mmio;bad.gmu.base=(volatile uint8_t *)(UINTPTR_MAX-3u);
    assert(ceph_power_mmio_read(&bad,CE_PWR_CX,&v)<0);
    bad=f.mmio;bad.lease=NULL;assert(ceph_power_mmio_read(&bad,CE_PWR_CX,&v)<0);
    destroy(&f);
    printf("Power MMIO: protected off-domain apertures, actual volatile transactions, %u lease failures, write restrictions PASS (host model)\n",checks);
}
