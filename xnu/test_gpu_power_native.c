#define _DEFAULT_SOURCE 1
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "cepheus_gpu_power_mmio.h"
#include "cepheus_gpu_power_native.h"

static volatile uint8_t *windows[3];
static const size_t sizes[]={0x90000,0x9000,0x30000};
static unsigned ceph_gpll0_lock,checks,expire,maps,fail_map,votes,finished,kept,prepare_calls;
static unsigned paused,context,prepare_error,mutate,timeout,fail_vote,bad_input,reenter;
static unsigned cc_inaccessible;
static struct ceph_power_native_owner owner;
static int ceph_scm_info_context(void *unused){(void)unused;return context?-1:0;}
static volatile uint32_t *word(unsigned w,unsigned off)
{return (volatile uint32_t *)(windows[w]+off);}
static void model(void)
{
    if(timeout || cc_inaccessible)return;
    if(!(*word(1,0x106c)&1u))*word(1,0x1540)|=1u<<31;
    uint32_t cmd=*word(1,0x1120);
    *word(1,0x1120)=(cmd&~((1u<<31)|1u))|
        ((cmd&2u) || (*word(1,0x1098)&1u)?0:1u<<31);
    const unsigned offsets[]={0x1098,0x109c,0x1078};
    for(unsigned n=0;n<3;++n)if(*word(1,offsets[n])&1u)*word(1,offsets[n])&=~(1u<<31);
    const unsigned gcc[]={0x71154,0x7100c,0x71018};
    for(unsigned n=0;n<3;++n)if(*word(0,gcc[n])&1u)*word(0,gcc[n])&=~(1u<<31);
    if((*word(1,0x1540)&(1u<<31)) && (*word(1,0x1098)&1u) &&
        (*word(1,0x109c)&1u) && (*word(1,0x1078)&1u))
        assert(!mprotect((void *)windows[2],sizes[2],PROT_READ|PROT_WRITE));
}
static int validate(void *ctx)
{
    assert(ctx==windows);++checks;
    if(expire && checks>=expire) {
        for(unsigned n=0;n<3;++n)assert(!mprotect((void *)windows[n],sizes[n],PROT_NONE));
        return -1;
    }
    if(mutate){mutate=0;owner.prepare=NULL;owner.validate=NULL;owner.vote=NULL;owner.finish=NULL;}
    if(reenter) {
        reenter=0;struct ceph_power_txn rejected={CE_PWR_NEW,123,456};
        assert(cepheus_gpu_power_start_native(&owner,&rejected)==CE_PWR_BUSY);
        assert(rejected.error==123 && rejected.effects==456 && ceph_gpll0_lock);
    }
    model();return 0;
}
static uintptr_t ml_io_map(uint64_t pa,size_t bytes)
{
    const uint64_t expected[]={0x100000,0x2c90000,0x2c6a000};
    assert(maps<3 && pa==expected[maps] && bytes==sizes[maps]);
    ++maps;return fail_map==maps?0:(uintptr_t)windows[maps-1];
}
static void delay(unsigned us){assert(us && us<=2);paused+=us;model();}
#include "cepheus_gpu_power_native_private.h"
static int prepare(void *ctx,struct ceph_power_input *in)
{
    assert(ctx==windows);++prepare_calls;
    if(prepare_error)return CE_PWR_BUSY;
    *in=(struct ceph_power_input){
        .cx={.address=0x30000,.levels={0,16,64,128},.count=4},
        .mx={.address=0x30010,.levels={0,16,128,192},.count=4},
        .current_cx=1,.current_mx=1,.parent_hz=300000000};
    if(bad_input)in->parent_hz=299999999;
    return 0;
}
static int vote(void *ctx,uint32_t address,unsigned index)
{
    assert(ctx==windows && index==2 && address==(votes?0x30000u:0x30010u));
    ++votes;return votes==fail_vote?CE_PWR_IO:0;
}
static void finish(void *ctx,int keep)
{assert(ctx==windows && ceph_gpll0_lock && ceph_power_native.finished);++finished;kept=(unsigned)keep;}
static void reset(void)
{
    for(unsigned n=0;n<3;++n) {
        assert(!mprotect((void *)windows[n],sizes[n],PROT_READ|PROT_WRITE));
        memset((void *)windows[n],0,sizes[n]);
    }
    memset(&ceph_power_native,0,sizeof(ceph_power_native));
    ceph_gpll0_lock=checks=expire=maps=fail_map=votes=finished=kept=prepare_calls=0;
    paused=context=prepare_error=mutate=timeout=fail_vote=bad_input=reenter=0;
    cc_inaccessible=0;
    owner=(struct ceph_power_native_owner){windows,prepare,validate,vote,finish};
    *word(0,0x71004)=1;
    *word(1,0x106c)=*word(1,0x100c)=0x222001;*word(1,0x1540)=0x5ff4;
    *word(1,0x1120)=0x80000000;
    const unsigned cc[]={0x1098,0x109c,0x1078};
    for(unsigned n=0;n<3;++n)*word(1,cc[n])=0x80004220;
    const unsigned gcc[]={0x71154,0x7100c,0x71018};
    for(unsigned n=0;n<3;++n)*word(0,gcc[n])=0x80004220;
    *word(2,0x14000)=1;
    assert(!mprotect((void *)windows[2],sizes[2],PROT_NONE));
}
static int run(struct ceph_power_txn *out){return cepheus_gpu_power_start_native(&owner,out);}
int main(void)
{
    for(unsigned n=0;n<3;++n) {
        windows[n]=mmap(NULL,sizes[n],PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        assert(windows[n]!=MAP_FAILED);
    }
    struct ceph_power_txn out;
    reset();assert(!run(&out) && out.state==CE_PWR_READY && finished==1 && kept && maps==3 && votes==2);
    assert(*word(1,0x1124)==0x602 && *word(2,0x14000)==1 && paused>=3);
    unsigned boundaries=checks;
    out=(struct ceph_power_txn){CE_PWR_NEW,123,456};unsigned before=checks;
    assert(run(&out)==CE_PWR_BUSY && checks==before && finished==1 && out.error==123 && out.effects==456);
    for(unsigned n=1;n<=boundaries;++n) {
        reset();expire=n;assert(run(&out) && out.state!=CE_PWR_READY && checks==n && !ceph_gpll0_lock);
        assert(finished==1 && kept==(out.effects!=0));
        before=checks;assert(run(&out)==CE_PWR_BUSY && checks==before && finished==1);
    }
    for(unsigned n=1;n<=3;++n) {
        reset();fail_map=n;assert(run(&out)==CE_PWR_IO && maps==n && finished==1 && !kept && !votes && !out.effects);
        assert(checks==2*n); /* failed maps still perform the post-map guard */
        reset();fail_map=n;expire=2*n;
        assert(run(&out)==CE_PWR_IO && maps==n && checks==expire && finished==1 && !kept);
    }
    reset();prepare_error=1;assert(run(&out)==CE_PWR_BUSY && prepare_calls==1 && !maps && !finished && !votes);
    reset();context=1;assert(run(&out)==CE_PWR_INVALID && !prepare_calls && !maps);
    reset();ceph_gpll0_lock=1;assert(run(&out)==CE_PWR_BUSY && !prepare_calls && ceph_gpll0_lock);
    reset();owner.finish=NULL;assert(run(&out)==CE_PWR_INVALID && !prepare_calls);
    reset();assert(cepheus_gpu_power_start_native(NULL,&out)==CE_PWR_INVALID);
    assert(cepheus_gpu_power_start_native(&owner,NULL)==CE_PWR_INVALID);
    reset();mutate=1;assert(!run(&out) && finished==1 && kept && !owner.finish);
    reset();reenter=1;assert(!run(&out) && finished==1 && kept && !ceph_gpll0_lock);
    reset();bad_input=1;assert(run(&out)==CE_PWR_INVALID && !votes && finished==1 && !kept);
    for(unsigned n=1;n<=2;++n) {
        reset();fail_vote=n;assert(run(&out)==CE_PWR_IO && votes==n && finished==1 && kept);
        assert(out.state==CE_PWR_QUARANTINED && out.effects==n && !ceph_gpll0_lock);
    }
    reset();*word(0,0x71004)=0x80000000;
    cc_inaccessible=1;
    assert(!mprotect((void *)windows[1],sizes[1],PROT_NONE));
    assert(run(&out)==CE_PWR_IO && !votes && finished==1 && !kept);
    reset();assert(!mprotect((void *)windows[2],sizes[2],PROT_READ|PROT_WRITE));
    *word(2,0x14000)=0;assert(!mprotect((void *)windows[2],sizes[2],PROT_NONE));
    assert(run(&out)==CE_PWR_BUSY && votes==2 && kept && out.state==CE_PWR_QUARANTINED);
    reset();timeout=1;assert(run(&out)==CE_PWR_TIMEOUT && finished==1 && kept && out.state==CE_PWR_QUARANTINED);
    for(unsigned n=0;n<3;++n)assert(!munmap((void *)windows[n],sizes[n]));
    printf("Native power: actual private adapter, fixed maps/delay, %u PROT_NONE owner boundaries, map failure/copied callbacks/terminal retention PASS (host hardware model)\n",boundaries);
}
