/* Linkable integration wrappers: compile real headers as XNU C, not only host
 * fixtures. These have no startup hook or MMIO backend. API calls are private
 * kernel building blocks and are NEVER exposed as a userspace dispatch table.
 */
#include "cepheus_gpu_compile.h"
#include "cepheus_gpu_firmware.h"
int cepheus_gpu_config_build(const struct ceph_arc_db *db,uint32_t chipid,uint32_t fuse,struct ceph_gpu_config *out)
{ return ceph_gpu_config_build(db,chipid,fuse,out); }
int cepheus_gpu_hfi_boot(struct ceph_hfi_session *s,const struct ceph_hfi_boot_config *c,uint32_t *v)
{ return ceph_hfi_boot_a640(s,c,v); }
int cepheus_gpu_upload(struct ceph_gmu_upload *u,const uint8_t *f,size_t n,struct ceph_gmu_block *p,unsigned capacity)
{ return ceph_firmware_verify(CE_FW_A640_GMU,f,n)?-1:ceph_gmu_upload_file(u,f,n,p,capacity); }
int cepheus_gpu_smmu_install(struct ceph_smmu_install *s,struct ceph_smmu_ops *b,const struct ceph_smmu_plan *p,uint32_t actlr)
{ return ceph_smmu_install_context(s,b,p,actlr); }
int cepheus_gpu_power_start(struct ceph_power_txn *t,struct ceph_power_ops *b)
{ return ceph_power_start(t,b); }
int cepheus_gpu_submit(struct ceph_submit *s,struct ceph_submit_ops *b,uint32_t seq)
{ return ceph_submit_once(s,b,seq); }
int cepheus_gpu_gmu_start(struct ceph_gmu_start *s,struct ceph_gmu_start_ops *b,const struct ceph_gmu_start_config *c)
{ return ceph_gmu_start_cold(s,b,c); }
int cepheus_gpu_ring_submit(struct ceph_gpu_ring *r,uint64_t scratch,uint64_t bytes,uint64_t address,uint32_t value,uint64_t fence,uint32_t sequence)
{ return ceph_gpu_ring_scratch(r,scratch,bytes,address,value,fence,sequence); }
int cepheus_gpu_sqe_copy(const uint8_t *f,size_t bytes,uint8_t *d,size_t capacity,struct ceph_sqe_image *out)
{ return ceph_firmware_verify(CE_FW_A630_SQE,f,bytes)?-1:ceph_sqe_copy(f,bytes,d,capacity,out); }
int cepheus_gpu_bandwidth_table(const struct ceph_bw_route *c,const struct ceph_bw_level *cl,const struct ceph_bw_route *d,const struct ceph_bw_level *dl,unsigned count,struct ceph_hfi_bw *out)
{ return ceph_gpu_bw_table(c,cl,d,dl,count,out); }
int cepheus_gpu_fault_read(struct ceph_gpu_fault_ops *b,struct ceph_gpu_fault_record *r)
{ return ceph_gpu_fault_snapshot(b,r); }
int cepheus_gpu_upload_snapshot(struct ceph_gmu_upload *u,uint64_t handle,struct ceph_gmu_block *plan,unsigned capacity)
{
    struct ceph_gpu_firmware_view view;
    if(cepheus_gpu_firmware_get(handle,CE_FW_A640_GMU,&view))return -1;
    int result=cepheus_gpu_upload(u,view.data,view.bytes,plan,capacity);
    if(cepheus_gpu_memory_view_put(handle))return -1;
    return result;
}
int cepheus_gpu_sqe_snapshot_copy(uint64_t handle,uint8_t *destination,size_t capacity,struct ceph_sqe_image *image)
{
    struct ceph_gpu_firmware_view view;
    if(cepheus_gpu_firmware_get(handle,CE_FW_A630_SQE,&view))return -1;
    int result=cepheus_gpu_sqe_copy(view.data,view.bytes,destination,capacity,image);
    if(cepheus_gpu_memory_view_put(handle))return -1;
    return result;
}
int cepheus_gpu_vm_build(struct ceph_vm_pool *pool,const struct ceph_vm_region *regions,unsigned count)
{ return ceph_vm_build(pool,regions,count); }
int cepheus_gpu_vm_lookup(struct ceph_vm_pool *pool,uint64_t va,uint64_t *pa,uint64_t *flags)
{ return ceph_vm_lookup(pool,va,pa,flags); }
int cepheus_gpu_va_build(const struct ceph_gpu_backing *backing,struct ceph_gpu_va_plan *plan)
{ return ceph_gpu_va_plan(backing,plan); }
int cepheus_gpu_pdc_install(struct ceph_pdc_install *s,struct ceph_pdc_ops *b,const struct ceph_pdc_input *in)
{ return ceph_pdc_install_a640v2(s,b,in); }
int cepheus_gpu_thermal_guard(struct ceph_gpu_thermal *t,const struct ceph_gpu_temperature *s,uint64_t now)
{ return ceph_gpu_thermal_check(t,s,now); }
int cepheus_gpu_cp_init_packet(uint32_t *out,unsigned capacity,uint64_t va,size_t bytes)
{ return ceph_cp_me_init(out,capacity,va,bytes); }
int cepheus_gpu_cp_reglist(struct ceph_cp_snapshot_ops *b,uint32_t *out,unsigned words)
{ return ceph_cp_snapshot(b,out,words); }
int cepheus_gpu_smmu_detach(struct ceph_smmu_detach *s,struct ceph_smmu_detach_ops *b,const struct ceph_smmu_plan *p)
{ return ceph_smmu_detach_context(s,b,p); }
int cepheus_gpu_gmu_park(struct ceph_gmu_park *s,struct ceph_gmu_park_ops *b)
{ return ceph_gmu_park_slumbered(s,b); }
int cepheus_gpu_gmu_slumber(struct ceph_gmu_slumber *s,struct ceph_gmu_slumber_ops *b,struct ceph_hfi_session *h,unsigned bw,unsigned freq,unsigned nb,unsigned ng)
{ return ceph_gmu_slumber_request(s,b,h,bw,freq,nb,ng); }
int cepheus_gpu_acd_message(struct ceph_qmp_tx *s,struct ceph_qmp_ops *b,unsigned enable)
{ return ceph_qmp_send_acd(s,b,enable); }
int cepheus_gpu_smmu_geometry(uint32_t id0,uint32_t id1,uint32_t id2,uint32_t bytes,struct ceph_smmu_geometry *out)
{ return ceph_smmu_geometry(id0,id1,id2,bytes,out); }
int cepheus_gpu_smmu_mmio_read(void *m,enum ceph_smmu_reg r,uint64_t *value)
{ return ceph_smmu_mmio_read(m,r,value); }
int cepheus_gpu_smmu_mmio_write(void *m,enum ceph_smmu_reg r,uint64_t value)
{ return ceph_smmu_mmio_write(m,r,value); }
int cepheus_gpu_oob_acquire(struct ceph_gmu_oob *s,struct ceph_oob_ops *b,uint32_t version)
{ return ceph_gmu_oob_acquire(s,b,version); }
int cepheus_gpu_oob_release(struct ceph_gmu_oob *s,struct ceph_oob_ops *b)
{ return ceph_gmu_oob_release(s,b); }
int cepheus_gpu_gmu_read(void *m,enum ceph_gmu_reg r,uint32_t *v)
{ return ceph_gmu_mmio_start_read(m,r,v); }
int cepheus_gpu_gmu_write(void *m,enum ceph_gmu_reg r,uint32_t v)
{ return ceph_gmu_mmio_start_write(m,r,v); }
int cepheus_gpu_hfi_notify(void *m)
{ return ceph_gmu_mmio_hfi_notify(m); }
int cepheus_gpu_gmu_fault(void *m)
{ return ceph_gmu_mmio_fault(m); }
