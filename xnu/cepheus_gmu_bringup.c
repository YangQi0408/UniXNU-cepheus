#include "cepheus_gmu_bringup.h"
#include "cepheus_gpu_compile.h"

int cepheus_gmu_bringup_thermal_check(struct ceph_gmu_bringup *s)
{
    if(!s || !s->thermal.active || !s->backend.temperature || !s->backend.now_ns)return CE_THERMAL_INVALID;
    if(s->thermal.failure)return CE_THERMAL_LATCHED;
    struct ceph_gpu_temperature sample;uint64_t now;
    if(s->backend.temperature(s->backend.owner,&sample) || s->backend.now_ns(s->backend.owner,&now)) {
        s->thermal.failure=CE_THERMAL_INVALID;return CE_THERMAL_INVALID;
    }
    return ceph_gpu_thermal_check(&s->thermal,&sample,now);
}
static int hfi_validate(void *p)
{
    struct ceph_gmu_bringup *s=p;
    /* A latched thermal stop must not prevent the bounded PREPARE_SLUMBER
     * needed to stop hardware. Link separately checks RETIRING group and id33.
     * Only formal retirement enters this state; no ordinary requests admitted. */
    if(s->stage!=CE_BR_RETIRING && cepheus_gmu_bringup_thermal_check(s))return -1;
    return s->backend.transport.validate(s->backend.transport.ctx);
}
static int hfi_doorbell(void *p){struct ceph_gmu_bringup *s=p;return s->backend.transport.doorbell(s->backend.transport.ctx);}
static int hfi_now(void *p,uint64_t *n){struct ceph_gmu_bringup *s=p;return s->backend.transport.now_ns(s->backend.transport.ctx,n);}
static int hfi_pause(void *p){struct ceph_gmu_bringup *s=p;return s->backend.transport.pause(s->backend.transport.ctx);}
static int hfi_fault(void *p){struct ceph_gmu_bringup *s=p;return s->backend.transport.fault(s->backend.transport.ctx);}

static void revoke_upload_aliases(struct ceph_gmu_bringup *s)
{
    for(unsigned i=0;i<3;++i)s->upload_maps[i]=(struct ceph_gmu_mapping){0};
    struct ceph_gmu_upload *u=&s->backend.upload;
    u->maps=NULL;u->map_count=0;u->itcm=u->dtcm=NULL;u->itcm_words=u->dtcm_words=0;
}

/* Compare serialized policy, not pointer identity or struct padding. bind may
 * fill MMIO adapters, but must not change what preflight validated before power
 * side effects. Buffers are bounded scratch reused one at a time (XNU stack). */
static int policy_unchanged(const struct ceph_gmu_bringup_backends *b,const uint32_t perf[59],
    const uint32_t bw[160],const uint32_t acd[17],const struct ceph_pdc_plan *pdc,
    const struct ceph_gpu_bin_policy *policy,unsigned min_minor)
{
    const struct ceph_hfi_boot_config *h=&b->hfi;
    if(!ceph_gpu_bin_policy_valid(h->policy) || h->policy->bin!=policy->bin ||
        h->ng!=policy->gx_levels || h->throttle_mask!=policy->throttle_mask ||
        (!!h->acd)!=policy->acd || h->min_fw_minor!=min_minor)return -1;
    uint32_t words[160];
    if(ceph_hfi_perf(words,160,h->gx,h->ng,h->cx,h->nc,h->gfx,h->cxl,h->mx)!=59)return -1;
    for(unsigned i=0;i<59;++i)if(words[i]!=perf[i])return -1;
    if(ceph_hfi_bandwidth(words,160,h->bw)!=160)return -1;
    for(unsigned i=0;i<160;++i)if(words[i]!=bw[i])return -1;
    if(h->acd) {
        if(ceph_hfi_acd_table(words,160,h->acd)!=17)return -1;
        for(unsigned i=0;i<17;++i)if(words[i]!=acd[i])return -1;
    }
    struct ceph_pdc_plan plan;if(ceph_pdc_plan_a640v2(&b->pdc_input,&plan) || plan.count!=pdc->count)return -1;
    for(unsigned i=0;i<plan.count;++i)
        if(plan.writes[i].window!=pdc->writes[i].window || plan.writes[i].offset!=pdc->writes[i].offset ||
            plan.writes[i].value!=pdc->writes[i].value)return -1;
    return 0;
}

int cepheus_gmu_bringup_prepare(struct ceph_gmu_bringup *s,const uint8_t *firmware,size_t bytes)
{
    if(!s || s->stage!=CE_BR_NEW || s->firmware || s->workspace.state!=CE_WS_NEW)return -1;
    if(cepheus_gpu_firmware_snapshot(0,firmware,bytes,&s->firmware))return -1;
    if(cepheus_gmu_workspace_prepare(&s->workspace)) {
        /* complete() can fail even before publication. Keep the only handle
         * for diagnostics if backing was retained; never report it released. */
        if(!cepheus_gpu_memory_release_cpu(s->firmware))s->firmware=0;
        s->stage=CE_BR_FAILED;s->error=-1;return -1;
    }
    s->stage=CE_BR_MEMORY;return 0;
}

int cepheus_gmu_bringup_start(struct ceph_gmu_bringup *s)
{
    if(!s || s->stage!=CE_BR_MEMORY || !s->firmware || !s->backend.bind || !s->backend.failed ||
        !s->backend.temperature || !s->backend.now_ns || !s->backend.trial_ns || s->backend.trial_ns>30000000000ULL ||
        !s->backend.transport.validate || !s->backend.transport.doorbell || !s->backend.transport.now_ns ||
        !s->backend.transport.pause || !s->backend.transport.fault)return -1;
    struct ceph_gmu_bringup_backends *b=&s->backend;
    int e;
    /* Reject incomplete immutable policy before any power side effects. bind
     * supplies mappings, not an opportunity to invent OPP/BW/thermal policy. */
    uint32_t perf[59],bw[160],acd[17];struct ceph_pdc_plan pdc_plan;
    if(!ceph_gpu_bin_policy_valid(b->hfi.policy) || b->hfi.ng!=b->hfi.policy->gx_levels ||
        b->hfi.throttle_mask!=b->hfi.policy->throttle_mask || (!!b->hfi.acd)!=b->hfi.policy->acd ||
        ceph_hfi_perf(perf,59,b->hfi.gx,b->hfi.ng,b->hfi.cx,b->hfi.nc,b->hfi.gfx,b->hfi.cxl,b->hfi.mx)!=59 ||
        ceph_hfi_bandwidth(bw,160,b->hfi.bw)!=160 ||
        (b->hfi.acd && ceph_hfi_acd_table(acd,17,b->hfi.acd)!=17) ||
        ceph_pdc_plan_a640v2(&b->pdc_input,&pdc_plan))return -1;
    if(b->hfi.policy->acd && (!b->acd.begin || !b->acd.validate || !b->acd.read32 ||
        !b->acd.write32 || !b->acd.barrier || !b->acd.notify || !b->acd.now_ns || !b->acd.pause || !b->acd.end))return -1;
    const struct ceph_gpu_bin_policy policy=*b->hfi.policy;unsigned min_minor=b->hfi.min_fw_minor;
    s->stage=CE_BR_THERMAL;uint64_t now;
    e=b->now_ns(b->owner,&now);if(e)goto failed;
    e=ceph_gpu_thermal_start(&s->thermal,now,b->trial_ns);if(e)goto failed;
    e=cepheus_gmu_bringup_thermal_check(s);if(e)goto failed;
    s->stage=CE_BR_POWER;e=ceph_power_start(&s->power,&b->power);if(e)goto failed;
    e=cepheus_gmu_bringup_thermal_check(s);if(e)goto failed;
    s->stage=CE_BR_BOUND;e=b->bind(b->owner,s);if(e)goto failed;
    e=policy_unchanged(b,perf,bw,acd,&pdc_plan,&policy,min_minor);if(e)goto failed;
    e=cepheus_gmu_bringup_thermal_check(s);if(e)goto failed;
    /* Bind cannot swap/forge the prepared root; exact resource identities are
     * checked again by the publication pool and by each concrete engine. */
    if(!ceph_smmu_plan_canonical(&b->smmu_plan) || (b->smmu_plan.smr&0x7fffu)!=5 ||
        b->smmu_plan.ttbr0!=(s->workspace.table.pa|((uint64_t)b->smmu_plan.asid<<48)) ||
        b->pdc_input.chipid!=0x06040001 || !b->upload.itcm || !b->upload.dtcm ||
        !ceph_gpu_bin_policy_valid(b->hfi.policy)) {e=-1;goto failed;}
    s->upload_maps[0]=(struct ceph_gmu_mapping){0x4000,0x3c000,s->workspace.views[0].cpu,CE_GMU_ICACHE};
    s->upload_maps[1]=(struct ceph_gmu_mapping){0x44000,0x3c000,s->workspace.views[1].cpu,CE_GMU_DCACHE};
    /* Persist is not part of image payload; it is zero-initialized and mapped
     * at0x60000000 for firmware's write-buffer dummy access. */
    s->upload_maps[2]=(struct ceph_gmu_mapping){0x60000000,4096,s->workspace.views[4].cpu,CE_GMU_KERNEL};
    b->upload.maps=s->upload_maps;b->upload.map_count=3;
    /* GENERAL_7 enables TCM retention before PDC scripts and firmware upload,
     * as required by a6xx_gmu_fw_start cold boot. Do not leave it as an implicit
     * undocumented obligation inside CM3 begin(). */
    uint32_t reset;
    if(!b->cm3.validate || !b->cm3.read || !b->cm3.write || !b->cm3.barrier ||
        b->cm3.validate(b->cm3.ctx) || b->cm3.read(b->cm3.ctx,CE_GMU_SYSRESET,&reset) || reset!=1) {e=-1;goto failed;}
    struct ceph_gmu_start retention={0};
    e=ceph_gmu_start_write(&retention,&b->cm3,CE_GMU_TCM_RETENTION,1,1);
    s->retention_writes=retention.effects;if(e)goto failed;
    if(b->hfi.policy->acd) {e=ceph_qmp_send_acd(&s->acd,&b->acd,1);if(e)goto failed;}
    e=cepheus_gmu_bringup_thermal_check(s);if(e)goto failed;
    s->stage=CE_BR_SHARED;e=cepheus_gmu_workspace_share(&s->workspace,&b->smmu_plan);if(e)goto failed;
    s->stage=CE_BR_PDC;e=ceph_pdc_install_a640v2(&s->pdc,&b->pdc,&b->pdc_input);if(e)goto failed;
    e=cepheus_gmu_bringup_thermal_check(s);if(e)goto failed;
    s->stage=CE_BR_SMMU;e=ceph_smmu_install_context(&s->smmu,&b->smmu,&b->smmu_plan,0x303);if(e)goto failed;
    e=cepheus_gmu_bringup_thermal_check(s);if(e)goto failed;
    s->stage=CE_BR_UPLOAD;e=cepheus_gpu_upload_snapshot(&b->upload,s->firmware,s->blocks,32);if(e)goto failed;
    e=cepheus_gmu_bringup_thermal_check(s);if(e)goto failed;
    /* Bind pristine HFI table before CM3 starts modifying it. */
    struct ceph_gmu_hfi_transport transport={s,hfi_validate,hfi_doorbell,hfi_now,hfi_pause,hfi_fault};
    e=cepheus_gmu_hfi_bind(&s->link,&s->workspace,&transport);if(e)goto failed;
    s->stage=CE_BR_CM3;e=ceph_gmu_start_cold(&s->cm3,&b->cm3,&s->workspace.layout.start);if(e)goto failed;
    b->hfi.hfi_version=s->cm3.hfi_version;
    s->stage=CE_BR_HFI;e=ceph_hfi_boot_a640(&s->link.session,&b->hfi,&s->fw_version);if(e)goto failed;
    e=cepheus_gmu_bringup_thermal_check(s);if(e)goto failed;
    /* Image is host-only sealed copy, never DMA-mapped; release after upload.
     * Workspace remains shared and pinned until protocol retirement. */
    e=cepheus_gpu_memory_release_cpu(s->firmware);
    if(e)goto failed;
    s->firmware=0;s->error=0;return 0;
failed:
    s->failed_stage=s->stage;s->stage=CE_BR_FAILED;s->error=e;
    if(s->link.bound)cepheus_gmu_hfi_invalidate(&s->link);
    if(s->workspace.state==CE_WS_SHARED) {
        (void)cepheus_gpu_memory_abandon_workspace(&s->workspace.publication);
        s->workspace.state=CE_WS_RETAINED;
        /* Record handles/group, but never leave accessible aliases advertised
         * as a ready workspace after a failed device-visible transition. */
        s->workspace.table=(struct ceph_vm_pool){0};
        s->workspace.layout=(struct ceph_gpu_va_plan){0};s->workspace.leased=0;
        for(unsigned i=0;i<CE_GPU_WORKSPACE_BUFFERS;++i)s->workspace.views[i]=(struct ceph_gpu_memory_view){0};
    }
    /* The uploader has copies of workspace CPU VAs as well as TCM aliases.
     * Quarantine retains backing, not permission for a second upload attempt.
     * Leave accounting/error state intact but revoke all advertised aliases. */
    revoke_upload_aliases(s);
    b->upload.quarantined=1;
    b->failed(b->owner,s->failed_stage,e);
    return e;
}

int cepheus_gmu_bringup_retire(struct ceph_gmu_bringup *s,struct ceph_gpu_retire_ops *b)
{
    if(!s || s->stage!=CE_BR_HFI || s->firmware || s->workspace.state!=CE_WS_SHARED ||
        !s->link.bound || !b || b->hfi!=&s->link.session || b->plan!=&s->backend.smmu_plan ||
        !s->backend.hfi.bw || b->bw_levels!=s->backend.hfi.bw->levels || b->gx_levels!=s->backend.hfi.ng)return -1;
    s->stage=CE_BR_RETIRING;
    int e=cepheus_gmu_workspace_retire(&s->workspace,b);
    /* Invalid retirement input refused before admission leaves the session
     * untouched. An attempted protocol failure instead quarantines the group. */
    if(s->workspace.state==CE_WS_SHARED){s->stage=CE_BR_HFI;return e?e:-1;}
    cepheus_gmu_hfi_invalidate(&s->link);revoke_upload_aliases(s);
    s->error=e;
    if(e) {
        s->failed_stage=CE_BR_HFI;s->stage=CE_BR_FAILED;s->backend.upload.quarantined=1;
        s->backend.failed(s->backend.owner,s->failed_stage,e);
    } else s->stage=CE_BR_RETIRED;
    return e;
}
