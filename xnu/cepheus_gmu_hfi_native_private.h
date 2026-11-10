/* Included after actual native SMMU/upload/CM3 records in the SWEET unit. */
#ifndef CEPHEUS_GMU_HFI_NATIVE_PRIVATE_H
#define CEPHEUS_GMU_HFI_NATIVE_PRIVATE_H
#include "cepheus_gmu_hfi_native.h"
#include "cepheus_gmu_hfi_link.h"
#include "cepheus_hfi_messages.h"
static struct {
    struct ceph_hfi_native_owner owner;
    struct ceph_gpu_publication publication;
    struct ceph_gmu_workspace workspace;
    struct ceph_gmu_hfi_link link;
    struct ceph_power_mmio power;
    struct ceph_gmu_mmio gmu;
    struct ceph_smmu_mmio smmu;
    struct ceph_hfi_native_result result;
    struct ceph_gpu_config config;
    struct ceph_hfi_boot_native_result boot;
    struct ceph_hfi_start_native_result start;
    uint32_t boot_packet[160];
    uint64_t last_ns;
    unsigned attempted,acquired,pinned,sealed,failed,clock_seen,boot_attempted,boot_ready;
    unsigned dcvs_attempted,start_attempted;
} ceph_hfi_native;
static int ceph_hfi_native_identity(const struct ceph_gpu_publication *p,
    const struct ceph_gpu_publication *q)
{
    if(p->group!=q->group || p->ttbr0!=q->ttbr0 || p->ttbr1!=q->ttbr1 || p->smr!=q->smr ||
        p->cb!=q->cb || p->slot!=q->slot || p->asid!=q->asid)return -1;
    for(unsigned i=0;i<CE_GPU_WORKSPACE_BUFFERS;++i)if(p->handles[i]!=q->handles[i])return -1;
    return 0;
}
static int ceph_hfi_native_config(const struct ceph_gmu_start_config *p,const struct ceph_gmu_start_config *q)
{
    return p->chipid!=q->chipid || p->hfi_va!=q->hfi_va || p->hfi_bytes!=q->hfi_bytes ||
        p->log_va!=q->log_va || p->log_bytes!=q->log_bytes?-1:0;
}
static int ceph_hfi_native_record(void)
{
    return !ceph_cm3_native.attempted || !ceph_cm3_native.acquired || !ceph_cm3_native.finished ||
        ceph_cm3_native.failed || ceph_cm3_native.txn.error || ceph_cm3_native.txn.state!=CE_GS_HFI_READY ||
        (ceph_cm3_native.txn.hfi_version>>28)!=2 ||
        ceph_cm3_native.txn.hfi_version!=ceph_hfi_native.gmu.hfi_version ||
        !ceph_smmu_native.attempted || !ceph_smmu_native.acquired || !ceph_smmu_native.finished ||
        ceph_smmu_native.failed || ceph_smmu_native.pinned || ceph_smmu_native.txn.error ||
        ceph_smmu_native.txn.state!=CE_SM_ENABLED ||
        !ceph_upload_native.attempted || !ceph_upload_native.acquired || !ceph_upload_native.finished ||
        ceph_upload_native.failed || ceph_upload_native.pinned || ceph_upload_native.upload.quarantined ||
        !ceph_upload_native.upload.writes ||
        ceph_hfi_native_identity(&ceph_hfi_native.publication,&ceph_smmu_native.publication) ||
        ceph_hfi_native_identity(&ceph_hfi_native.publication,&ceph_upload_native.publication) ||
        ceph_hfi_native_config(&ceph_hfi_native.gmu.config,&ceph_cm3_native.mmio.config) ||
        ceph_hfi_native_config(&ceph_hfi_native.gmu.config,&ceph_upload_native.gmu.config) ||
        ceph_hfi_native_config(&ceph_hfi_native.gmu.config,&ceph_smmu_native.gmu.config) ||
        ceph_hfi_native.gmu.base!=ceph_cm3_native.mmio.base ||
        ceph_hfi_native.gmu.bytes!=ceph_cm3_native.mmio.bytes ||
        ceph_hfi_native.power.gcc.base!=ceph_cm3_native.power.gcc.base ||
        ceph_hfi_native.power.cc.base!=ceph_cm3_native.power.cc.base ||
        ceph_hfi_native.power.gmu.base!=ceph_cm3_native.power.gmu.base ||
        ceph_hfi_native.smmu.base!=ceph_smmu_native.mmio.base ||
        ceph_hfi_native.smmu.plan.ttbr0!=ceph_hfi_native.publication.ttbr0 ||
        ceph_hfi_native.smmu.plan.ttbr1!=ceph_hfi_native.publication.ttbr1 ||
        ceph_hfi_native.smmu.plan.smr!=ceph_hfi_native.publication.smr ||
        ceph_hfi_native.smmu.plan.cb!=ceph_hfi_native.publication.cb ||
        ceph_hfi_native.smmu.plan.slot!=ceph_hfi_native.publication.slot ||
        ceph_hfi_native.smmu.plan.asid!=ceph_hfi_native.publication.asid?-1:0;
}
static int ceph_hfi_native_lease(void *unused)
{
    (void)unused;
    if(!ceph_hfi_native.acquired || ceph_hfi_native.failed)return -1;
    if(ceph_scm_info_context(NULL) || ceph_hfi_native_record() ||
        cepheus_gpu_rpmh_check(ceph_hfi_native.owner.rpmh_token) ||
        (ceph_hfi_native.pinned && ceph_memory_hfi_validate(&ceph_hfi_native.publication)) ||
        ceph_hfi_native.owner.validate(ceph_hfi_native.owner.ctx) || ceph_hfi_native_record() ||
        (ceph_hfi_native.boot_attempted && cepheus_gpu_config_check_native(&ceph_hfi_native.config)) ||
        (ceph_hfi_native.boot_attempted && ceph_hfi_native.config.policy.acd &&
          (!ceph_qmp_native.attempted || !ceph_qmp_native.acquired || !ceph_qmp_native.finished ||
           ceph_qmp_native.failed || ceph_qmp_native.result.error || !ceph_qmp_native.result.acd.consumed ||
           ceph_qmp_native.result.acd.uncertain || ceph_qmp_native.owner.rpmh_token!=ceph_hfi_native.owner.rpmh_token)) ||
        (ceph_hfi_native.start_attempted &&
          (!ceph_oob_native.acquired || ceph_oob_native.failed || ceph_oob_native.release_attempted ||
           ceph_oob_native.txn.state!=CE_OOB_HELD || ceph_oob_native.txn.error ||
           ceph_oob_native.owner.rpmh_token!=ceph_hfi_native.owner.rpmh_token)) ||
        cepheus_gpu_rpmh_check(ceph_hfi_native.owner.rpmh_token) ||
        (ceph_hfi_native.pinned && ceph_memory_hfi_validate(&ceph_hfi_native.publication))) {
        ceph_hfi_native.failed=1;return -1;
    }
    return 0;
}
static int ceph_hfi_native_power(void *unused)
{
    if(ceph_hfi_native_lease(unused) || ceph_power_mmio_gmu_ready(&ceph_hfi_native.power)) {
        ceph_hfi_native.failed=1;return -1;
    }
    uint32_t memnoc,dvm;
    if(ceph_power_mmio_load(&ceph_hfi_native.power,&ceph_hfi_native.power.gcc,CE_GPU_GCC_MEMNOC,&memnoc) ||
        !ceph_gpu_branch_active(memnoc) ||
        ceph_power_mmio_load(&ceph_hfi_native.power,&ceph_hfi_native.power.gcc,CE_GPU_GCC_DVM,&dvm) ||
        !ceph_gpu_branch_active(dvm)) {ceph_hfi_native.failed=1;return -1;}
    return 0;
}
static int ceph_hfi_native_access(void *unused)
{
    struct ceph_gmu_mmio *g=&ceph_hfi_native.gmu;
    uint32_t reset,result,status,version;
    if(ceph_hfi_native_power(unused) ||
        ceph_gmu_mmio_start_read(g,CE_GMU_SYSRESET,&reset) || reset ||
        ceph_gmu_mmio_start_read(g,CE_GMU_INIT_RESULT,&result) || result!=0xbabeface ||
        ceph_gmu_mmio_start_read(g,CE_GMU_HFI_STATUS,&status) || !(status&1u) ||
        ceph_gmu_mmio_start_read(g,CE_GMU_HFI_VERSION,&version) || version!=g->hfi_version ||
        ceph_gmu_mmio_fault(g))goto failed;
    const enum ceph_smmu_reg regs[]={CE_SM_SMR,CE_SM_S2CR,CE_SM_TTBR0,CE_SM_TTBR1,CE_SM_SCTLR,CE_SM_FSR};
    const struct ceph_smmu_plan *p=&ceph_hfi_native.smmu.plan;
    const uint64_t expected[]={p->smr,p->s2cr_translate,p->ttbr0,p->ttbr1,p->sctlr,0};
    for(unsigned i=0;i<6;++i) {
        uint64_t value;
        if(ceph_smmu_mmio_read(&ceph_hfi_native.smmu,regs[i],&value) || value!=expected[i])goto failed;
    }
    if(ceph_hfi_native.start_attempted && ceph_oob_native_access(NULL))goto failed;
    return ceph_hfi_native_lease(unused);
failed:
    ceph_hfi_native.failed=1;return -1;
}
static int ceph_hfi_native_now(void *unused,uint64_t *out)
{
    if(!out || ceph_hfi_native_lease(unused))return -1;
    uint64_t value;absolutetime_to_nanoseconds(mach_absolute_time(),&value);
    if(ceph_hfi_native_lease(unused) || (ceph_hfi_native.clock_seen && value<ceph_hfi_native.last_ns)) {
        ceph_hfi_native.failed=1;return -1;
    }
    ceph_hfi_native.clock_seen=1;ceph_hfi_native.last_ns=value;*out=value;return 0;
}
static int ceph_hfi_native_pause(void *unused)
{
    uint64_t before,after;
    if(ceph_hfi_native_now(unused,&before))return -1;
    delay(1);
    if(ceph_hfi_native_now(unused,&after) || after-before<1000 || after-before>100000) {
        ceph_hfi_native.failed=1;return -1;
    }
    return 0;
}
static int ceph_hfi_native_notify(void *unused)
{
    if(ceph_hfi_native_access(unused) || ceph_memory_hfi_publish(&ceph_hfi_native.publication) ||
        ceph_hfi_native_access(unused))return -1;
    ++ceph_hfi_native.result.doorbells;
    if(ceph_gmu_mmio_hfi_notify(&ceph_hfi_native.gmu) || ceph_hfi_native_access(unused))return -1;
    uint64_t start,now;
    if(ceph_hfi_native_now(unused,&start))return -1;
    for(unsigned i=0;i<10000;++i) {
        uint32_t info;
        if(ceph_hfi_native_now(unused,&now) || now-start>=100000000ULL ||
            ceph_gmu_mmio_load(&ceph_hfi_native.gmu,0x1f992,&info) || ceph_hfi_native_access(unused))return -1;
        if(info&1u) {
            if(ceph_gmu_mmio_store(&ceph_hfi_native.gmu,0x1f991,1) || ceph_hfi_native_access(unused))return -1;
            return 0;
        }
        if(ceph_hfi_native_pause(unused))return -1;
    }
    ceph_hfi_native.failed=1;return -1;
}
static int ceph_hfi_native_finish(int error,struct ceph_hfi_native_result *out)
{
    if(!error && ceph_hfi_native_access(NULL))error=-1;
    if(ceph_hfi_native.pinned) {
        int result=ceph_hfi_native.sealed?(error?CE_MEMORY_HFI_UNCERTAIN:CE_MEMORY_HFI_RETAINED):CE_MEMORY_HFI_REFUSED;
        if(ceph_memory_hfi_finish(&ceph_hfi_native.publication,result)) {error=-1;ceph_hfi_native.failed=1;}
        else if(!ceph_hfi_native.sealed)ceph_hfi_native.pinned=0;
    }
    ceph_hfi_native.result.error=error;
    ceph_hfi_native.result.retained=ceph_hfi_native.sealed || ceph_hfi_native.pinned;
    if(error)ceph_hfi_native.result.firmware_version=0;
    if(ceph_hfi_native.acquired)ceph_hfi_native.owner.finish(ceph_hfi_native.owner.ctx,
        ceph_hfi_native.result.packet_words,ceph_hfi_native.result.doorbells,ceph_hfi_native.result.retained);
    if(!error && ceph_hfi_native_access(NULL)) {
        error=-1;ceph_hfi_native.result.error=error;ceph_hfi_native.result.firmware_version=0;
        (void)ceph_memory_hfi_finish(&ceph_hfi_native.publication,CE_MEMORY_HFI_UNCERTAIN);
    }
    *out=ceph_hfi_native.result;return error;
}
int cepheus_gmu_hfi_version_native(const struct ceph_hfi_native_owner *owner,
    const struct ceph_gpu_publication *publication,struct ceph_hfi_native_result *out)
{
    if(!owner || !owner->begin || !owner->validate || !owner->finish || !publication || !publication->group ||
        !out || ceph_scm_info_context(NULL))return -1;
    if(__atomic_exchange_n(&ceph_gpll0_lock,1,__ATOMIC_ACQUIRE))return -1;
    int e=-1;
    if(ceph_hfi_native.attempted)goto done;
    ceph_hfi_native.attempted=1;ceph_hfi_native.owner=*owner;ceph_hfi_native.publication=*publication;
    ceph_hfi_native.gmu=ceph_cm3_native.mmio;ceph_hfi_native.gmu.hfi_version=ceph_cm3_native.txn.hfi_version;
    ceph_hfi_native.power=ceph_cm3_native.power;ceph_hfi_native.smmu=ceph_smmu_native.mmio;
    ceph_hfi_native.gmu.owner=ceph_hfi_native.power.owner=ceph_hfi_native.smmu.owner=NULL;
    ceph_hfi_native.gmu.lease=ceph_hfi_native.smmu.lease=ceph_hfi_native_power;
    ceph_hfi_native.power.lease=ceph_hfi_native_lease;
    if(ceph_hfi_native_record() || cepheus_gpu_rpmh_check(ceph_hfi_native.owner.rpmh_token))goto finish;
    e=ceph_hfi_native.owner.begin(ceph_hfi_native.owner.ctx,&ceph_hfi_native.publication);if(e)goto finish;
    ceph_hfi_native.acquired=1;
    struct ceph_gmu_workspace *w=&ceph_hfi_native.workspace;
    if(ceph_hfi_native_lease(NULL) || ceph_memory_hfi_acquire(&ceph_hfi_native.publication,&w->views[2],&w->layout.start)) {e=-1;goto finish;}
    ceph_hfi_native.pinned=1;w->publication=ceph_hfi_native.publication;w->state=CE_WS_SHARED;w->leased=CE_GPU_WORKSPACE_VIEWS;
    for(unsigned i=0;i<CE_GPU_WORKSPACE_BUFFERS;++i)w->handles[i]=w->publication.handles[i];
    if(ceph_hfi_native_config(&w->layout.start,&ceph_hfi_native.gmu.config) || ceph_hfi_native_access(NULL)) {e=-1;goto finish;}
    uint32_t raw,info;
    if(ceph_gmu_mmio_load(&ceph_hfi_native.gmu,0x1f996,&raw) ||
        ceph_gmu_mmio_load(&ceph_hfi_native.gmu,0x1f992,&info) || raw || info ||
        ceph_hfi_native_access(NULL)) {e=-1;goto finish;}
    ceph_hfi_native.sealed=1;
    if(ceph_memory_hfi_seal(&ceph_hfi_native.publication) || cepheus_gpu_rpmh_finish(ceph_hfi_native.owner.rpmh_token,1) ||
        ceph_hfi_native_access(NULL)) {e=-1;goto finish;}
    /* RAM lifetime checks do not sample unneeded hardware per payload word.
     * MMIO is individually power-gated; exchange fault polls check hardware. */
    struct ceph_gmu_hfi_transport transport={NULL,ceph_hfi_native_lease,ceph_hfi_native_notify,
        ceph_hfi_native_now,ceph_hfi_native_pause,ceph_hfi_native_access};
    if(cepheus_gmu_hfi_bind(&ceph_hfi_native.link,w,&transport)) {e=-1;goto finish;}
    uint32_t cmd[2],reply[4];unsigned count;
    ceph_hfi_fw_version(cmd,2,2,0);
    /* Fixed padded packet size, not a count of completed stores or work. */
    ceph_hfi_native.result.packet_words=4;
    e=ceph_hfi_exchange(&ceph_hfi_native.link.session,cmd,2,reply,4,&count);
    if(!e && (count!=4 || (reply[3]>>28)!=2))e=CE_HFI_PROTOCOL;
    if(!e) {
        ceph_hfi_native.result.hfi_version=ceph_hfi_native.gmu.hfi_version;
        ceph_hfi_native.result.firmware_version=reply[3];
    }
finish:
    e=ceph_hfi_native_finish(e,out);
done:
    __atomic_store_n(&ceph_gpll0_lock,0,__ATOMIC_RELEASE);return e;
}
static int ceph_hfi_native_boot_send(unsigned words,unsigned *acked)
{
    uint32_t reply[16];unsigned count;
    if(ceph_hfi_native_access(NULL))return CE_HFI_IO;
    int e=ceph_hfi_exchange(&ceph_hfi_native.link.session,ceph_hfi_native.boot_packet,words,reply,16,&count);
    if(e)return e;
    if(count!=3)return ceph_hfi_session_poison(&ceph_hfi_native.link.session,CE_HFI_PROTOCOL);
    if(acked)++*acked;
    return 0;
}
int cepheus_gmu_hfi_boot_native(const struct ceph_gpu_config *config,struct ceph_hfi_boot_native_result *out)
{
    if(!config || !out || ceph_scm_info_context(NULL))return -1;
    if(__atomic_exchange_n(&ceph_gpll0_lock,1,__ATOMIC_ACQUIRE))return -1;
    int e=-1;
    if(ceph_hfi_native.boot_attempted || !ceph_hfi_native.sealed || ceph_hfi_native.failed ||
        ceph_hfi_native.result.error || !ceph_hfi_native.result.firmware_version ||
        !ceph_hfi_native.link.bound || ceph_hfi_native.link.session.sequence!=1 ||
        ceph_hfi_native.link.session.poisoned || ceph_hfi_native.link.session.busy ||
        cepheus_gpu_config_check_native(config))goto done;
    ceph_hfi_native.config=*config;ceph_hfi_native.boot_attempted=1;
    struct ceph_hfi_boot_config c;
    if(ceph_gpu_config_hfi(&ceph_hfi_native.config,ceph_hfi_native.result.hfi_version,&c) ||
        ceph_hfi_native_access(NULL))goto finish;
    if(c.acd && (!ceph_qmp_native.attempted || !ceph_qmp_native.acquired || !ceph_qmp_native.finished ||
        ceph_qmp_native.failed || ceph_qmp_native.result.error || !ceph_qmp_native.result.acd.consumed ||
        ceph_qmp_native.result.acd.uncertain ||
        ceph_qmp_native.owner.rpmh_token!=ceph_hfi_native.owner.rpmh_token))goto finish;
    uint32_t *packet=ceph_hfi_native.boot_packet;
    if(ceph_hfi_perf(packet,160,c.gx,c.ng,c.cx,c.nc,c.gfx,c.cxl,c.mx)!=59)goto finish;
    e=ceph_hfi_native_boot_send(59,&ceph_hfi_native.boot.acked);if(e)goto finish;
    if(ceph_hfi_bandwidth(packet,160,c.bw)!=160){e=-1;goto finish;}
    e=ceph_hfi_native_boot_send(160,&ceph_hfi_native.boot.acked);if(e)goto finish;
    ceph_hfi_feature(packet,160,1,0,0);
    e=ceph_hfi_native_boot_send(4,&ceph_hfi_native.boot.acked);if(e)goto finish;
    if(c.acd) {
        if(ceph_hfi_acd_table(packet,160,c.acd)!=17){e=-1;goto finish;}
        e=ceph_hfi_native_boot_send(17,&ceph_hfi_native.boot.acked);if(e)goto finish;
        ceph_hfi_feature(packet,160,12,1,0);
        e=ceph_hfi_native_boot_send(4,&ceph_hfi_native.boot.acked);if(e)goto finish;
    }
    if(c.policy->lm) {
        ceph_hfi_feature(packet,160,7,1,c.throttle_mask);
        e=ceph_hfi_native_boot_send(4,&ceph_hfi_native.boot.acked);if(e)goto finish;
    }
    ceph_hfi_core_start(packet,160);
    e=ceph_hfi_native_boot_send(2,&ceph_hfi_native.boot.acked);
finish:
    if(!e && ceph_hfi_native_access(NULL))e=-1;
    ceph_hfi_native.boot.doorbells=ceph_hfi_native.result.doorbells-1;
    ceph_hfi_native.boot.retained=1;
    ceph_hfi_native.owner.finish(ceph_hfi_native.owner.ctx,160,ceph_hfi_native.boot.doorbells,1);
    if(!e && ceph_hfi_native_access(NULL))e=-1;
    if(e) {
        ceph_hfi_native.failed=1;
        (void)ceph_memory_hfi_finish(&ceph_hfi_native.publication,CE_MEMORY_HFI_UNCERTAIN);
    } else {
        ceph_hfi_native.boot.hfi_version=ceph_hfi_native.result.hfi_version;
        ceph_hfi_native.boot.firmware_version=ceph_hfi_native.result.firmware_version;
        ceph_hfi_native.boot_ready=1;
    }
    ceph_hfi_native.boot.error=e;*out=ceph_hfi_native.boot;
done:
    __atomic_store_n(&ceph_gpll0_lock,0,__ATOMIC_RELEASE);return e;
}
static int ceph_hfi_native_start_stage(unsigned start,struct ceph_hfi_start_native_result *out)
{
    if(!out || ceph_scm_info_context(NULL))return -1;
    if(__atomic_exchange_n(&ceph_gpll0_lock,1,__ATOMIC_ACQUIRE))return -1;
    int e=-1;
    if(!ceph_hfi_native.boot_ready || ceph_hfi_native.failed || ceph_hfi_native.boot.error ||
        (start?(!ceph_hfi_native.start.dcvs_acked || ceph_hfi_native.start_attempted):ceph_hfi_native.dcvs_attempted))goto done;
    if(start) {
        if(!ceph_oob_native.acquired || ceph_oob_native.failed || ceph_oob_native.release_attempted ||
            ceph_oob_native.txn.state!=CE_OOB_HELD || ceph_oob_native.txn.error ||
            ceph_oob_native.owner.rpmh_token!=ceph_hfi_native.owner.rpmh_token)goto done;
        ceph_hfi_native.start_attempted=1;
        if(ceph_hfi_start(ceph_hfi_native.boot_packet,160)!=1)goto finish;
    }else {
        ceph_hfi_native.dcvs_attempted=1;
        if(ceph_hfi_dcvs(ceph_hfi_native.boot_packet,160,1,CE_HFI_DCVS_INVALID)!=4)goto finish;
    }
    e=ceph_hfi_native_boot_send(start?1:4,NULL);
finish:
    if(!e && ceph_hfi_native_access(NULL))e=-1;
    ceph_hfi_native.start.doorbells=ceph_hfi_native.result.doorbells-1-ceph_hfi_native.boot.doorbells;
    ceph_hfi_native.start.retained=1;
    ceph_hfi_native.owner.finish(ceph_hfi_native.owner.ctx,4,ceph_hfi_native.start.doorbells,1);
    if(!e && ceph_hfi_native_access(NULL))e=-1;
    if(e) {
        ceph_hfi_native.failed=1;
        (void)ceph_memory_hfi_finish(&ceph_hfi_native.publication,CE_MEMORY_HFI_UNCERTAIN);
    }else if(start)ceph_hfi_native.start.start_acked=1;
    else ceph_hfi_native.start.dcvs_acked=1;
    ceph_hfi_native.start.error=e;*out=ceph_hfi_native.start;
done:
    __atomic_store_n(&ceph_gpll0_lock,0,__ATOMIC_RELEASE);return e;
}
int cepheus_gmu_hfi_dcvs_native(struct ceph_hfi_start_native_result *out)
{return ceph_hfi_native_start_stage(0,out);}
int cepheus_gmu_hfi_start_native(struct ceph_hfi_start_native_result *out)
{return ceph_hfi_native_start_stage(1,out);}
#endif
