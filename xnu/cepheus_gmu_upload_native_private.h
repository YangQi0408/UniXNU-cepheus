/* Included after GPLL0 state in the actual SWEET translation unit. */
#ifndef CEPHEUS_GMU_UPLOAD_NATIVE_PRIVATE_H
#define CEPHEUS_GMU_UPLOAD_NATIVE_PRIVATE_H
static struct {
    struct ceph_gmu_upload_native_owner owner;
    struct ceph_gpu_publication publication;
    struct ceph_power_mmio power;
    struct ceph_gmu_mmio gmu;
    struct ceph_gmu_upload_mmio mmio;
    struct ceph_gmu_upload upload;
    struct ceph_gmu_mapping maps[2];
    struct ceph_gmu_block blocks[32];
    unsigned attempted,acquired,pinned,finished,failed;
} ceph_upload_native;
static int ceph_upload_native_lease(void *unused)
{
    (void)unused;
    if(!ceph_upload_native.acquired || ceph_upload_native.finished || ceph_upload_native.failed)return -1;
    if(ceph_scm_info_context(NULL) || ceph_upload_native.owner.validate(ceph_upload_native.owner.ctx) ||
        (ceph_upload_native.pinned && ceph_memory_upload_validate(&ceph_upload_native.publication))) {
        ceph_upload_native.failed=1;return -1;
    }
    return 0;
}
static int ceph_upload_native_access(void *unused)
{
    uint32_t memnoc,dvm;
    if(ceph_upload_native_lease(unused) || ceph_power_mmio_gmu_ready(&ceph_upload_native.power) ||
        ceph_power_mmio_load(&ceph_upload_native.power,&ceph_upload_native.power.gcc,CE_GPU_GCC_MEMNOC,&memnoc) ||
        !ceph_gpu_branch_active(memnoc) ||
        ceph_power_mmio_load(&ceph_upload_native.power,&ceph_upload_native.power.gcc,CE_GPU_GCC_DVM,&dvm) ||
        !ceph_gpu_branch_active(dvm) || ceph_upload_native_lease(unused)) {
        ceph_upload_native.failed=1;return -1;
    }
    return 0;
}
static void ceph_upload_native_end(void *unused,int uncertain)
{(void)unused;if(uncertain)ceph_upload_native.failed=1;}
static int ceph_upload_native_publish(void *unused)
{
    (void)unused;
    if(ceph_gmu_upload_mmio_reset(&ceph_upload_native.mmio))return -1;
    if(ceph_memory_upload_publish(&ceph_upload_native.publication)) {
        ceph_upload_native.failed=1;return -1;
    }
    return ceph_gmu_upload_mmio_publish(&ceph_upload_native.mmio);
}
static int ceph_upload_native_finish(int e,struct ceph_gmu_upload_native_result *out)
{
    struct ceph_gmu_upload *u=&ceph_upload_native.upload;
    uint32_t reset,retention;
    if(!e && (ceph_gmu_mmio_start_read(&ceph_upload_native.gmu,CE_GMU_SYSRESET,&reset) || reset!=1 ||
        ceph_gmu_mmio_start_read(&ceph_upload_native.gmu,CE_GMU_TCM_RETENTION,&retention) || retention!=1 ||
        ceph_gmu_mmio_fault(&ceph_upload_native.gmu) ||
        ceph_memory_upload_publish(&ceph_upload_native.publication) || ceph_upload_native_access(NULL))) {
        ceph_upload_native.failed=1;e=-1;
    }
    int uncertain=e && u->writes,keep=u->writes!=0;
    if(ceph_upload_native.pinned) {
        enum ceph_memory_upload_result result=uncertain?CE_MEMORY_UPLOAD_UNCERTAIN:
            e?CE_MEMORY_UPLOAD_REFUSED:CE_MEMORY_UPLOAD_COPIED;
        if(ceph_memory_upload_finish(&ceph_upload_native.publication,result)) {
            ceph_upload_native.failed=1;uncertain=keep=1;e=-1;
        } else ceph_upload_native.pinned=0;
    }
    ceph_upload_native.finished=1;
    if(ceph_upload_native.acquired)ceph_upload_native.owner.finish(ceph_upload_native.owner.ctx,keep);
    *out=(struct ceph_gmu_upload_native_result){u->writes,e,uncertain};return e;
}
int cepheus_gmu_upload_native(const struct ceph_gmu_upload_native_owner *owner,uint64_t snapshot,
    const struct ceph_gpu_publication *publication,struct ceph_gmu_upload_native_result *out)
{
    if(!owner || !owner->begin || !owner->validate || !owner->finish || !snapshot || !publication ||
        !publication->group || (publication->smr&0x7fffu)!=5 || !out || ceph_scm_info_context(NULL))return -1;
    if(__atomic_exchange_n(&ceph_gpll0_lock,1,__ATOMIC_ACQUIRE))return -1;
    int e=-1;
    if(ceph_upload_native.attempted)goto done;
    ceph_upload_native.attempted=1;ceph_upload_native.owner=*owner;
    ceph_upload_native.publication=*publication;
    e=ceph_upload_native.owner.begin(ceph_upload_native.owner.ctx,&ceph_upload_native.publication,snapshot);
    if(e)goto finish;
    ceph_upload_native.acquired=1;
    if(ceph_upload_native_lease(NULL) ||
        ceph_memory_upload_acquire(&ceph_upload_native.publication,&ceph_upload_native.gmu.config,ceph_upload_native.maps)) {
        e=-1;goto finish;
    }
    ceph_upload_native.pinned=1;
    struct ceph_power_mmio *p=&ceph_upload_native.power;p->owner=NULL;p->lease=ceph_upload_native_lease;
    struct ceph_power_window *windows[]={&p->gcc,&p->cc,&p->gmu};
    const uint64_t pa[]={0x100000,0x2c90000,0x2c6a000};
    const size_t bytes[]={0x90000,0x9000,0x30000};
    for(unsigned n=0;n<3;++n) {
        if(ceph_upload_native_lease(NULL)){e=-1;goto finish;}
        uintptr_t base=ml_io_map(pa[n],bytes[n]);
        if(ceph_upload_native_lease(NULL) || !base){e=-1;goto finish;}
        *windows[n]=(struct ceph_power_window){(volatile uint8_t *)base,pa[n],bytes[n]};
    }
    struct ceph_gmu_mmio *g=&ceph_upload_native.gmu;
    g->base=p->gmu.base;g->bytes=p->gmu.bytes;g->owner=NULL;g->lease=ceph_upload_native_access;
    uint32_t retention;
    if(ceph_gmu_mmio_start_read(g,CE_GMU_TCM_RETENTION,&retention) || retention!=1) {e=-1;goto finish;}
    ceph_upload_native.mmio=(struct ceph_gmu_upload_mmio){g,NULL,ceph_upload_native_end,0,0};
    if(ceph_gmu_upload_mmio_bind(&ceph_upload_native.mmio,&ceph_upload_native.upload)){e=-1;goto finish;}
    ceph_upload_native.upload.maps=ceph_upload_native.maps;ceph_upload_native.upload.map_count=2;
    ceph_upload_native.upload.publish=ceph_upload_native_publish;
    e=cepheus_gpu_upload_snapshot(&ceph_upload_native.upload,snapshot,ceph_upload_native.blocks,32);
finish:
    e=ceph_upload_native_finish(e,out);
done:
    __atomic_store_n(&ceph_gpll0_lock,0,__ATOMIC_RELEASE);return e;
}
#endif
