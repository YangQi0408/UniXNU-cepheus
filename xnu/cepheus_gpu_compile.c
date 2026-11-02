/* Linkable integration wrappers: compile real headers as XNU C, not only host
 * fixtures. These have no startup hook or MMIO backend. API calls are private
 * kernel building blocks and are NEVER exposed as a userspace dispatch table.
 */
#include "cepheus_gpu_compile.h"
#include "cepheus_gpu_firmware.h"
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
