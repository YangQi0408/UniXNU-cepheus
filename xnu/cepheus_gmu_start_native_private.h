/* Included after GPLL0 native state inside the SWEET translation unit. */
#ifndef CEPHEUS_GMU_START_NATIVE_PRIVATE_H
#define CEPHEUS_GMU_START_NATIVE_PRIVATE_H
static int ceph_qmp_native_cold_record(uint64_t *);
static struct {
    struct ceph_gmu_start_native_owner owner;
    struct ceph_gmu_mmio mmio;
    struct ceph_power_mmio power;
    struct ceph_gmu_start txn;
    struct ceph_gpu_config config;
    uint64_t last_ns,acd_before_cold;
    unsigned attempted,acquired,finished,failed,clock_seen;
} ceph_cm3_native;
static int ceph_cm3_native_owner_check(void)
{
    if(!ceph_cm3_native.acquired || ceph_cm3_native.failed)return -1;
    uint64_t token;
    if(ceph_scm_info_context(NULL) || cepheus_gpu_config_check_native(&ceph_cm3_native.config) ||
        ceph_cm3_native.owner.validate(ceph_cm3_native.owner.ctx) ||
        cepheus_gpu_config_check_native(&ceph_cm3_native.config) ||
        (ceph_cm3_native.acd_before_cold &&
          (ceph_qmp_native_cold_record(&token) || token!=ceph_cm3_native.acd_before_cold))) {
        ceph_cm3_native.failed=1;return -1;
    }
    return 0;
}
static int ceph_cm3_native_lease(void *unused)
{
    (void)unused;
    return ceph_cm3_native.finished?-1:ceph_cm3_native_owner_check();
}
static int ceph_cm3_native_access(void *unused)
{
    (void)unused;
    if(ceph_cm3_native_lease(NULL))return -1;
    if(ceph_power_mmio_gmu_ready(&ceph_cm3_native.power)) {
        ceph_cm3_native.failed=1;return -1;
    }
    return 0;
}
static void ceph_cm3_native_finish(void *unused,int keep)
{
    (void)unused;
    ceph_cm3_native.finished=1;
    ceph_cm3_native.owner.finish(ceph_cm3_native.owner.ctx,keep);
    if(ceph_cm3_native.txn.state==CE_GS_HFI_READY && ceph_cm3_native_owner_check()) {
        ceph_cm3_native.txn.state=CE_GS_QUARANTINED;ceph_cm3_native.txn.error=-1;
    }
}
static int ceph_cm3_native_begin(void *unused,const struct ceph_gmu_start_config *config)
{
    (void)unused;
    int e=ceph_cm3_native.owner.begin(ceph_cm3_native.owner.ctx,config);
    if(e)return e;
    ceph_cm3_native.acquired=1;
    struct ceph_power_mmio *p=&ceph_cm3_native.power;
    p->owner=NULL;p->lease=ceph_cm3_native_lease;
    struct ceph_power_window *windows[]={&p->gcc,&p->cc,&p->gmu};
    const uint64_t pa[]={0x100000,0x2c90000,0x2c6a000};
    const size_t bytes[]={0x90000,0x9000,0x30000};
    for(unsigned n=0;n<3;++n) {
        if(ceph_cm3_native_lease(NULL))goto fail;
        *windows[n]=(struct ceph_power_window){
            (volatile uint8_t *)ml_io_map(pa[n],bytes[n]),pa[n],bytes[n]};
        if(ceph_cm3_native_lease(NULL) || !windows[n]->base)goto fail;
    }
    struct ceph_gmu_mmio *m=&ceph_cm3_native.mmio;
    m->base=p->gmu.base;m->bytes=p->gmu.bytes;m->owner=NULL;m->lease=ceph_cm3_native_access;
    /* Retention must already have been established by the preceding stages;
     * do not hide a write or a firmware upload in this acquisition step. */
    uint32_t retained;
    if(ceph_gmu_mmio_start_read(m,CE_GMU_TCM_RETENTION,&retained) || retained!=1)goto fail;
    return 0;
fail:
    ceph_cm3_native.failed=1;ceph_cm3_native_finish(NULL,0);return -1;
}
static int ceph_cm3_native_read(void *unused,enum ceph_gmu_reg r,uint32_t *out)
{(void)unused;return ceph_gmu_mmio_start_read(&ceph_cm3_native.mmio,r,out);}
static int ceph_cm3_native_write(void *unused,enum ceph_gmu_reg r,uint32_t value)
{(void)unused;return ceph_gmu_mmio_start_write(&ceph_cm3_native.mmio,r,value);}
static int ceph_cm3_native_barrier(void *unused)
{
    if(ceph_cm3_native_access(unused))return -1;
    ceph_gmu_mmio_barrier();return ceph_cm3_native_access(unused);
}
static int ceph_cm3_native_now(void *unused,uint64_t *out)
{
    if(!out || ceph_cm3_native_access(unused))return -1;
    uint64_t value;absolutetime_to_nanoseconds(mach_absolute_time(),&value);
    if(ceph_cm3_native_access(unused) ||
        (ceph_cm3_native.clock_seen && value<ceph_cm3_native.last_ns)) {
        ceph_cm3_native.failed=1;return -1;
    }
    ceph_cm3_native.last_ns=value;ceph_cm3_native.clock_seen=1;
    *out=value;return 0;
}
static int ceph_cm3_native_pause(void *unused)
{
    uint64_t before,after;
    if(ceph_cm3_native_now(unused,&before))return -1;
    delay(1);
    if(ceph_cm3_native_now(unused,&after))return -1;
    if(after-before<1000 || after-before>100000) {
        ceph_cm3_native.failed=1;return -1;
    }
    return 0;
}
int cepheus_gmu_start_cold_native(const struct ceph_gmu_start_native_owner *owner,
    const struct ceph_gmu_start_config *config,const struct ceph_gpu_config *gpu,struct ceph_gmu_start *out)
{
    struct ceph_gmu_start_plan plan;
    if(!owner || !owner->begin || !owner->validate || !owner->finish || !out ||
        ceph_gmu_start_plan(config,&plan) || !gpu || !ceph_gpu_bin_policy_valid(&gpu->policy) ||
        gpu->pdc.chipid!=config->chipid || gpu->initial_freq!=1 || gpu->initial_bw!=3 ||
        ceph_scm_info_context(NULL))return -1;
    if(__atomic_exchange_n(&ceph_gpll0_lock,1,__ATOMIC_ACQUIRE))return -1;
    int e=-1;
    if(ceph_cm3_native.attempted)goto done;
    if(cepheus_gpu_config_check_native(gpu))goto done;
    uint64_t acd_token=0;
    if(gpu->policy.acd && ceph_qmp_native_cold_record(&acd_token))goto done;
    ceph_cm3_native.attempted=1;ceph_cm3_native.owner=*owner;ceph_cm3_native.mmio.config=*config;
    ceph_cm3_native.config=*gpu;
    /* Snapshot only an actual completed QMP transaction before this cold
     * attempt. A later ACD ACK cannot retroactively satisfy bin1 ordering. */
    ceph_cm3_native.acd_before_cold=acd_token;
    struct ceph_gmu_start_ops ops={NULL,ceph_cm3_native_begin,ceph_cm3_native_access,
        ceph_cm3_native_read,ceph_cm3_native_write,ceph_cm3_native_barrier,
        ceph_cm3_native_now,ceph_cm3_native_pause,ceph_cm3_native_finish};
    e=ceph_gmu_start_cold(&ceph_cm3_native.txn,&ops,&ceph_cm3_native.mmio.config);
    if(!e && ceph_cm3_native.txn.error)e=ceph_cm3_native.txn.error;
    *out=ceph_cm3_native.txn;
done:
    __atomic_store_n(&ceph_gpll0_lock,0,__ATOMIC_RELEASE);return e;
}
#endif
