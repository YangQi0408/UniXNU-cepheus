#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cepheus_cp_execution_admission.h"
#include "cepheus_zap_pas.h"
#include "cepheus_smmu_install.h"

static int ceph_scm_info_lock,ceph_gpll0_lock,scm_context;
static int gx_live_fail,cp_live_fail,ring_live_fail,zap_auth_fail;
static unsigned validates,finishes,finish_keep;
static struct ceph_cp_execution_admission finish_record;
static uint64_t zap_handle;

static struct {
    unsigned attempted,acquired,failed,finished,pinned,uncertain,sealed;
    struct ceph_cp_publication publication;
    struct ceph_gx_smmu_plan plan;
    struct ceph_smmu_install txn;
} ceph_gx_smmu_native;
static struct {unsigned failed,sealed;
    struct {unsigned writes,reads;} result;} ceph_cp_native;
static struct {
    unsigned attempted,acquired,failed;
    struct ceph_cp_ring_native_result result;
    struct ceph_cp_ring_plan plan;
    struct {uint64_t rpmh_token;} owner;
} ceph_cp_ring_native;
static struct ceph_zap_pas ceph_zap_native;

static void lck_mtx_lock(int *lock){assert(lock && !*lock);*lock=1;}
static void lck_mtx_unlock(int *lock){assert(lock && *lock);*lock=0;}
static int ceph_scm_info_context(void *ctx){(void)ctx;return scm_context?-1:0;}
static int ceph_gx_smmu_native_check_locked(void){return gx_live_fail?-1:0;}
static int ceph_cp_native_check_locked(void){return cp_live_fail?-1:0;}
static int ceph_cp_ring_native_check_locked(void){return ring_live_fail?-1:0;}
int ceph_zap_memory_authenticated_unlocked(const struct ceph_zap_claim *claim)
{return !zap_auth_fail && claim && claim->handle==zap_handle?0:-1;}

#include "cepheus_cp_execution_admission_private.h"

static int owner_validate(void *ctx)
{
    assert(ctx==(void *)&ceph_gx_smmu_native && ceph_gpll0_lock);
    ++validates;assert(validates);return 0;
}
static void owner_finish(void *ctx,const struct ceph_cp_execution_admission *record,int keep)
{
    assert(ctx==(void *)&ceph_gx_smmu_native && record);
    assert(record->version==0 || record->version==CE_CP_EXECUTION_ADMISSION_VERSION);
    assert(record->version!=CE_CP_EXECUTION_ADMISSION_VERSION ||
        (record->ring_writes==7 && record->zap_epoch && record->admission_token &&
         record->rpmh_token));
    ++finishes;finish_keep=(unsigned)keep;finish_record=*record;
}

static void setup(void)
{
    memset(&ceph_gx_smmu_native,0,sizeof(ceph_gx_smmu_native));
    memset(&ceph_cp_native,0,sizeof(ceph_cp_native));
    memset(&ceph_cp_ring_native,0,sizeof(ceph_cp_ring_native));
    memset(&ceph_zap_native,0,sizeof(ceph_zap_native));
    memset(&ceph_cp_execution_admission,0,sizeof(ceph_cp_execution_admission));
    ceph_scm_info_lock=ceph_gpll0_lock=scm_context=0;
    gx_live_fail=cp_live_fail=ring_live_fail=zap_auth_fail=0;
    validates=finishes=finish_keep=0;memset(&finish_record,0,sizeof(finish_record));

    struct ceph_smmu_geometry g;
    assert(!ceph_smmu_geometry((1u<<30)|(1u<<27)|(11u<<9)|16,(2u<<28)|8,0x1222,0x10000,&g));
    struct ceph_smmu_stream streams[16]={0};
    for(unsigned n=0;n<16;++n)streams[n].s2cr=2u<<16;
    assert(!ceph_gx_smmu_plan(streams,16,&g,0,1,2,0x110000000ULL,0,&ceph_gx_smmu_native.plan));

    const uint64_t va[]={0xfc000000,0xfc010000,0xfc020000,0xfc030000,0x100000000ULL};
    const size_t bytes[]={32768,4096,32768,4096,4096};
    struct ceph_cp_publication *cp=&ceph_gx_smmu_native.publication;
    const struct ceph_smmu_plan *p=&ceph_gx_smmu_native.plan.context;
    cp->memory=(struct ceph_gpu_publication){.group=0x101,.ttbr0=p->ttbr0,.ttbr1=p->ttbr1,
        .smr=p->smr,.cb=p->cb,.slot=p->slot,.asid=p->asid};
    for(unsigned n=0;n<CE_CP_BUFFERS;++n)cp->memory.handles[n]=0x100+n;
    cp->table_used=32;
    for(unsigned n=0;n<CE_CP_ROOT;++n)cp->regions[n]=(struct ceph_vm_region){va[n],
        0x20000000+n*0x100000,bytes[n],ceph_cp_workspace_permissions(n)};
    const struct ceph_cp_ring_input in={va[CE_CP_RING],va[CE_CP_RPTR],va[CE_CP_SQE],
        bytes[CE_CP_RING],bytes[CE_CP_RPTR],bytes[CE_CP_SQE]};
    assert(!ceph_cp_ring_plan(&in,&ceph_cp_ring_native.plan));

    ceph_gx_smmu_native.attempted=ceph_gx_smmu_native.acquired=ceph_gx_smmu_native.finished=1;
    ceph_gx_smmu_native.txn=(struct ceph_smmu_install){CE_SM_ENABLED,15,0};
    ceph_cp_native.failed=0;ceph_cp_native.sealed=1;
    ceph_cp_native.result.writes=45;ceph_cp_native.result.reads=66;
    ceph_cp_ring_native.attempted=ceph_cp_ring_native.acquired=1;
    ceph_cp_ring_native.result=(struct ceph_cp_ring_native_result){7,1};
    ceph_cp_ring_native.owner.rpmh_token=1;
    zap_handle=0x4000000000000010;
    ceph_zap_native.phase=CE_ZP_LIVE;ceph_zap_native.epoch=0x640001;
    ceph_zap_native.claim.handle=zap_handle;
}

static void reset_admission(void)
{
    memset(&ceph_cp_execution_admission,0,sizeof(ceph_cp_execution_admission));
    ceph_gpll0_lock=0;validates=finishes=finish_keep=0;
}

int main(void)
{
    setup();
    struct ceph_cp_execution_admission_owner owner={(void *)&ceph_gx_smmu_native,
        owner_validate,owner_finish};
    uint64_t token=(uint64_t)(uintptr_t)&owner;
    struct ceph_cp_execution_admission record={0};

    assert(cepheus_gpu_cp_execution_admit(&owner,ceph_zap_native.epoch,
        (struct ceph_cp_execution_admission *)&ceph_gx_smmu_native)<0);
    assert(!ceph_cp_execution_admission.attempted && !finishes);
    struct ceph_cp_execution_admission baseline={0};
    memset(&baseline,0xa5,sizeof(baseline));
    reset_admission();
    assert(!cepheus_gpu_cp_execution_admit(&owner,ceph_zap_native.epoch,&record));
    assert(ceph_cp_execution_admission.attempted && ceph_cp_execution_admission.admitted &&
        ceph_cp_execution_admission.finished && !ceph_cp_execution_admission.failed &&
        finishes==1 && finish_keep==1 && !ceph_gpll0_lock && !ceph_scm_info_lock);
    assert(finish_record.version==CE_CP_EXECUTION_ADMISSION_VERSION &&
        finish_record.ring_writes==7 && finish_record.zap_epoch==ceph_zap_native.epoch &&
        finish_record.admission_token==token && finish_record.rpmh_token==1);
    assert(!cepheus_gpu_cp_execution_check(token,&record));
    assert(cepheus_gpu_cp_execution_check(token+1,&record)<0 && !ceph_cp_execution_admission.failed);
    assert(!cepheus_gpu_cp_execution_check(token,&record));
    assert(cepheus_gpu_cp_execution_admit(&owner,ceph_zap_native.epoch,&baseline)<0 &&
        baseline.version==0xa5a5a5a5);

    reset_admission();
    assert(!cepheus_gpu_cp_execution_admit(&owner,ceph_zap_native.epoch,&record));
    struct ceph_cp_execution_admission bad=record;
    bad.ring.writes[0].value^=1;
    assert(cepheus_gpu_cp_execution_check(token,&bad)<0 && ceph_cp_execution_admission.failed);
    assert(cepheus_gpu_cp_execution_check(token,&record)<0);

    reset_admission();
    assert(!cepheus_gpu_cp_execution_admit(&owner,ceph_zap_native.epoch,&record));
    gx_live_fail=1;
    assert(cepheus_gpu_cp_execution_check(token,&record)<0 && ceph_cp_execution_admission.failed);
    gx_live_fail=0;assert(cepheus_gpu_cp_execution_check(token,&record)<0);

    reset_admission();
    assert(!cepheus_gpu_cp_execution_admit(&owner,ceph_zap_native.epoch,&record));
    zap_auth_fail=1;
    assert(cepheus_gpu_cp_execution_check(token,&record)<0 && ceph_cp_execution_admission.failed);
    zap_auth_fail=0;assert(cepheus_gpu_cp_execution_check(token,&record)<0);

    reset_admission();ceph_gpll0_lock=1;
    assert(cepheus_gpu_cp_execution_admit(&owner,ceph_zap_native.epoch,&record)<0 &&
        ceph_cp_execution_admission.attempted && ceph_cp_execution_admission.failed &&
        finishes==1 && ceph_gpll0_lock && finish_record.version==0);
    ceph_gpll0_lock=0;assert(cepheus_gpu_cp_execution_admit(&owner,ceph_zap_native.epoch,&record)<0 &&
        finishes==1);

    reset_admission();scm_context=1;
    assert(cepheus_gpu_cp_execution_admit(&owner,ceph_zap_native.epoch,&record)<0 &&
        !ceph_cp_execution_admission.attempted);
    scm_context=0;

    reset_admission();zap_auth_fail=1;
    assert(cepheus_gpu_cp_execution_admit(&owner,ceph_zap_native.epoch,&record)<0 &&
        finishes==1 && finish_keep==1 && finish_record.version==0);
    assert(ceph_cp_execution_admission.failed && !ceph_cp_execution_admission.admitted);
    zap_auth_fail=0;

    puts("CP execution admission native wrapper: lock order, one attempt, output aliases, owner retention and sticky identity/live/ZAP failure PASS");
}
