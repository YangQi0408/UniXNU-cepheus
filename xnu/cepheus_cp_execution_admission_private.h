/* Included after the actual CP, GX SMMU, ring and ZAP native records. */
#ifndef CEPHEUS_CP_EXECUTION_ADMISSION_PRIVATE_H
#define CEPHEUS_CP_EXECUTION_ADMISSION_PRIVATE_H
#include "cepheus_cp_execution_admission.h"

static struct {
    struct ceph_cp_execution_admission_owner owner;
    struct ceph_cp_execution_admission record;
    unsigned attempted,admitted,failed,finished;
} ceph_cp_execution_admission;

static int ceph_cp_execution_admission_live_locked(void)
{
    return ceph_gx_smmu_native_check_locked() || ceph_cp_native_check_locked() ||
        ceph_cp_ring_native_check_locked() ||
        ceph_cp_execution_admission.owner.validate(ceph_cp_execution_admission.owner.ctx) ||
        ceph_gx_smmu_native_check_locked() || ceph_cp_native_check_locked() ||
        ceph_cp_ring_native_check_locked()?-1:0;
}

static int ceph_cp_execution_admission_identity_locked(
    const struct ceph_cp_execution_admission *record)
{
    return !record || record->version!=CE_CP_EXECUTION_ADMISSION_VERSION ||
        record->ring_writes!=7 || !record->admission_token || !record->zap_epoch ||
        !record->rpmh_token || !ceph_cp_ring_native.result.configured ||
        ceph_cp_ring_native.result.writes!=record->ring_writes ||
        record->zap.handle!=ceph_zap_native.claim.handle ||
        record->zap_epoch!=ceph_zap_native.epoch ||
        ceph_zap_memory_authenticated_unlocked(&record->zap) ||
        !ceph_cp_execution_admission_same_cp(&record->cp,&ceph_gx_smmu_native.publication) ||
        !ceph_cp_execution_admission_same_smmu(&record->smmu,&ceph_gx_smmu_native.plan) ||
        !ceph_cp_execution_admission_same_ring(&record->ring,&ceph_cp_ring_native.plan) ||
        record->rpmh_token!=ceph_cp_ring_native.owner.rpmh_token?-1:0;
}

static int ceph_cp_execution_admission_output(const void *owner,
    const struct ceph_cp_execution_admission *out)
{
    if(!owner || !out || ((uintptr_t)out&7u))return -1;
    const void *bases[]={owner,&ceph_cp_execution_admission,&ceph_gx_smmu_native,
        &ceph_cp_native,&ceph_cp_ring_native,&ceph_zap_native};
    const size_t bytes[]={sizeof(struct ceph_cp_execution_admission_owner),
        sizeof(ceph_cp_execution_admission),sizeof(ceph_gx_smmu_native),
        sizeof(ceph_cp_native),sizeof(ceph_cp_ring_native),sizeof(ceph_zap_native)};
    for(unsigned n=0;n<6;++n)
        if(ceph_cp_execution_admission_overlap(out,sizeof(*out),bases[n],bytes[n]))return -1;
    return 0;
}

static int ceph_cp_execution_admission_record_locked(
    const struct ceph_cp_execution_admission *record)
{
    if(!record || record->version!=CE_CP_EXECUTION_ADMISSION_VERSION ||
        ceph_zap_native.phase!=CE_ZP_LIVE ||
        ceph_zap_memory_authenticated_unlocked(&ceph_zap_native.claim) ||
        ceph_cp_execution_admission_identity_locked(record) ||
        ceph_cp_execution_admission_live_locked())return -1;
    return 0;
}

int cepheus_gpu_cp_execution_admit(const struct ceph_cp_execution_admission_owner *owner,
    uint64_t epoch,struct ceph_cp_execution_admission *out)
{
    if(!owner || !owner->validate || !owner->finish || !epoch || !out ||
        ceph_cp_execution_admission_output(owner,out) || ceph_scm_info_context(NULL))return -1;
    lck_mtx_lock(&ceph_scm_info_lock);
    int e=-1;
    if(ceph_cp_execution_admission.attempted)goto done;
    ceph_cp_execution_admission.owner=*owner;
    ceph_cp_execution_admission.attempted=1;
    int held=!__atomic_exchange_n(&ceph_gpll0_lock,1,__ATOMIC_ACQUIRE);
    if(!held)goto finish;
    if(epoch!=ceph_zap_native.epoch || ceph_zap_native.phase!=CE_ZP_LIVE ||
        ceph_zap_memory_authenticated_unlocked(&ceph_zap_native.claim) ||
        !ceph_cp_ring_native.attempted || !ceph_cp_ring_native.acquired ||
        ceph_cp_ring_native.failed || !ceph_cp_ring_native.result.configured ||
        ceph_cp_ring_native.result.writes!=7 || !ceph_gx_smmu_native.acquired ||
        !ceph_gx_smmu_native.finished || ceph_gx_smmu_native.failed ||
        ceph_gx_smmu_native.uncertain || ceph_gx_smmu_native.pinned ||
        ceph_gx_smmu_native.txn.state!=CE_SM_ENABLED || ceph_gx_smmu_native.txn.effects!=15 ||
        !ceph_cp_native.sealed || ceph_cp_native.failed ||
        ceph_cp_native.result.writes!=45 || ceph_cp_native.result.reads!=66 ||
        ceph_cp_execution_admission_live_locked() ||
        ceph_cp_execution_admission_plan(&ceph_gx_smmu_native.publication,
            &ceph_gx_smmu_native.plan,&ceph_cp_ring_native.plan,&ceph_cp_ring_native.result,
            epoch,&ceph_zap_native.claim,ceph_cp_ring_native.owner.rpmh_token,
            (uint64_t)(uintptr_t)owner, &ceph_cp_execution_admission.record))goto finish;
    ceph_cp_execution_admission.admitted=1;*out=ceph_cp_execution_admission.record;e=0;
finish:
    if(held)__atomic_store_n(&ceph_gpll0_lock,0,__ATOMIC_RELEASE);
    ceph_cp_execution_admission.owner.finish(ceph_cp_execution_admission.owner.ctx,1);
    ceph_cp_execution_admission.finished=1;
    if(e)ceph_cp_execution_admission.failed=1;
done:
    lck_mtx_unlock(&ceph_scm_info_lock);return e;
}

int cepheus_gpu_cp_execution_check(uint64_t token,
    const struct ceph_cp_execution_admission *record)
{
    if(!token || !record || ceph_scm_info_context(NULL))return -1;
    lck_mtx_lock(&ceph_scm_info_lock);int e=-1;
    if(!__atomic_exchange_n(&ceph_gpll0_lock,1,__ATOMIC_ACQUIRE)) {
        if(ceph_cp_execution_admission.admitted && !ceph_cp_execution_admission.failed &&
            token==ceph_cp_execution_admission.record.admission_token) {
            if(token!=record->admission_token)goto unlock;
            if(ceph_cp_execution_admission_record_locked(record)) {
                ceph_cp_execution_admission.failed=1;goto unlock;
            }
            e=0;
        }
unlock:
        __atomic_store_n(&ceph_gpll0_lock,0,__ATOMIC_RELEASE);
    }
    lck_mtx_unlock(&ceph_scm_info_lock);return e;
}
#endif
