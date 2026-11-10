/* Included in sweet_rpmh.c after actual scope and DB definitions. */
#ifndef CEPHEUS_GPU_CONFIG_NATIVE_PRIVATE_H
#define CEPHEUS_GPU_CONFIG_NATIVE_PRIVATE_H
#include "cepheus_gpu_config_native.h"
extern int cepheus_gpu_fuse_sample(uint32_t *);
static struct {
    struct ceph_gpu_config_native_owner owner;
    struct ceph_gpu_config config,repeat;
    uintptr_t db;
    uint32_t bytes,fuse;
    unsigned attempted,acquired,failed,ready;
} ceph_config_native;
static int ceph_config_native_guard(void)
{
    if(!ceph_config_native.acquired || ceph_config_native.failed)return EBUSY;
    if(rh_poisoned || rh_db!=ceph_config_native.db || rh_db_size!=ceph_config_native.bytes ||
        cepheus_gpu_rpmh_check(ceph_config_native.owner.rpmh_token) ||
        ceph_config_native.owner.validate(ceph_config_native.owner.ctx) || rh_poisoned ||
        rh_db!=ceph_config_native.db || rh_db_size!=ceph_config_native.bytes ||
        cepheus_gpu_rpmh_check(ceph_config_native.owner.rpmh_token)) {
        ceph_config_native.failed=1;ceph_config_native.ready=0;return EBUSY;
    }
    return 0;
}
static uint32_t ceph_config_native_read(void *unused,uint32_t off)
{
    (void)unused;
    if((off&3u) || ceph_config_native.bytes<4 || off>ceph_config_native.bytes-4 ||
        ceph_config_native_guard())return UINT32_MAX;
    uint32_t value=rh_read(ceph_config_native.db,off);
    if(ceph_config_native_guard() || value==UINT32_MAX) {
        ceph_config_native.failed=1;return UINT32_MAX;
    }
    return value;
}
static int ceph_config_native_fuse(void)
{
    uint32_t value;
    if(ceph_config_native_guard() || cepheus_gpu_fuse_sample(&value) ||
        ceph_config_native_guard() || value!=ceph_config_native.fuse) {
        ceph_config_native.failed=1;ceph_config_native.ready=0;return EBUSY;
    }
    return 0;
}
/* Compare typed fields, not padding, self-pointers or caller serialization. */
static int ceph_config_native_equal(const struct ceph_gpu_config *a,const struct ceph_gpu_config *b)
{
    if(!ceph_gpu_bin_policy_valid(&a->policy) || !ceph_gpu_bin_policy_valid(&b->policy) ||
        a->policy.bin!=b->policy.bin || a->initial_freq!=b->initial_freq || a->initial_bw!=b->initial_bw ||
        a->pdc.chipid!=b->pdc.chipid || a->pdc.cfg_pa!=b->pdc.cfg_pa || a->pdc.seq_pa!=b->pdc.seq_pa)return EINVAL;
    const struct ceph_arc_resource *aa[]={&a->gfx,&a->cx,&a->mx,&a->pdc.resources[0],&a->pdc.resources[1],&a->pdc.resources[2]};
    const struct ceph_arc_resource *bb[]={&b->gfx,&b->cx,&b->mx,&b->pdc.resources[0],&b->pdc.resources[1],&b->pdc.resources[2]};
    for(unsigned n=0;n<6;++n) {
        if(aa[n]->address!=bb[n]->address || aa[n]->aux_version!=bb[n]->aux_version ||
            aa[n]->count!=bb[n]->count || aa[n]->count>16)return EINVAL;
        for(unsigned i=0;i<2;++i)if(aa[n]->priority[i]!=bb[n]->priority[i])return EINVAL;
        for(unsigned i=0;i<16;++i)if(aa[n]->levels[i]!=bb[n]->levels[i])return EINVAL;
    }
    const struct ceph_hfi_bw *x=&a->bw,*y=&b->bw;
    if(x->levels!=y->levels || x->cnoc_count!=y->cnoc_count || x->ddr_count!=y->ddr_count ||
        x->cnoc_wait!=y->cnoc_wait || x->ddr_wait!=y->ddr_wait)return EINVAL;
    for(unsigned i=0;i<6;++i)if(x->cnoc_addr[i]!=y->cnoc_addr[i])return EINVAL;
    for(unsigned i=0;i<8;++i)if(x->ddr_addr[i]!=y->ddr_addr[i])return EINVAL;
    for(unsigned i=0;i<2;++i)for(unsigned j=0;j<6;++j)if(x->cnoc_data[i][j]!=y->cnoc_data[i][j])return EINVAL;
    for(unsigned i=0;i<16;++i)for(unsigned j=0;j<8;++j)if(x->ddr_data[i][j]!=y->ddr_data[i][j])return EINVAL;
    return 0;
}
int cepheus_gpu_config_build_native(const struct ceph_gpu_config_native_owner *owner,struct ceph_gpu_config *out)
{
    if(!owner || !owner->begin || !owner->validate || !owner->finish || !out)return EINVAL;
    if(ceph_rpmh_scope_context() || __atomic_exchange_n(&rh_lock,1,__ATOMIC_ACQUIRE))return EBUSY;
    int e=EBUSY;
    if(ceph_config_native.attempted)goto done;
    ceph_config_native.attempted=1;ceph_config_native.owner=*owner;
    if(rh_poisoned || !rh_db || rh_db_size<144 || rh_db_size>0x100000 || (rh_db_size&3u) ||
        cepheus_gpu_rpmh_check(owner->rpmh_token))goto done;
    ceph_config_native.db=rh_db;ceph_config_native.bytes=rh_db_size;
    e=ceph_config_native.owner.begin(ceph_config_native.owner.ctx,CE_GPU_CONFIG_FDT_SHA256);
    if(e)goto done;
    ceph_config_native.acquired=1;
    if(ceph_config_native_guard() || cepheus_gpu_fuse_sample(&ceph_config_native.fuse) ||
        ceph_config_native_fuse()) {e=EBUSY;goto finish;}
    struct ceph_arc_db db={NULL,ceph_config_native.bytes,ceph_config_native_read};
    e=ceph_gpu_config_build(&db,0x06040001,ceph_config_native.fuse,&ceph_config_native.config);
    if(ceph_config_native.failed){e=EBUSY;goto finish;}
    if(e || ceph_config_native_fuse()) {if(!e)e=EBUSY;goto finish;}
    e=ceph_gpu_config_build(&db,0x06040001,ceph_config_native.fuse,&ceph_config_native.repeat);
    if(ceph_config_native.failed){e=EBUSY;goto finish;}
    if(!e)e=ceph_config_native_equal(&ceph_config_native.config,&ceph_config_native.repeat);
    if(!e)e=ceph_config_native_fuse();
finish:
    ceph_config_native.owner.finish(ceph_config_native.owner.ctx,e);
    if(!e)e=ceph_config_native_fuse();
    if(!e){ceph_config_native.ready=1;*out=ceph_config_native.config;}
    else {ceph_config_native.failed=1;ceph_config_native.ready=0;}
done:
    __atomic_store_n(&rh_lock,0,__ATOMIC_RELEASE);return e;
}
int cepheus_gpu_config_check_native(const struct ceph_gpu_config *config)
{
    if(!config)return EINVAL;
    if(ceph_rpmh_scope_context() || __atomic_exchange_n(&rh_lock,1,__ATOMIC_ACQUIRE))return EBUSY;
    int e=EBUSY;
    if(ceph_config_native.ready && !ceph_config_native_fuse())
        e=ceph_config_native_equal(config,&ceph_config_native.config);
    __atomic_store_n(&rh_lock,0,__ATOMIC_RELEASE);return e;
}
#endif
