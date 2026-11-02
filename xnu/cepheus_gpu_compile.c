/* Linkable integration wrappers: compile real headers as XNU C, not only host
 * fixtures. These have no startup hook or MMIO backend. API calls are private
 * kernel building blocks and are NEVER exposed as a userspace dispatch table.
 */
#include "cepheus_gpu_compile.h"
int cepheus_gpu_hfi_boot(struct ceph_hfi_session *s,const struct ceph_hfi_boot_config *c,uint32_t *v)
{ return ceph_hfi_boot_a640(s,c,v); }
int cepheus_gpu_upload(struct ceph_gmu_upload *u,const uint8_t *f,size_t n,struct ceph_gmu_block *p,unsigned capacity)
{ return ceph_gmu_upload_file(u,f,n,p,capacity); }
int cepheus_gpu_smmu_install(struct ceph_smmu_install *s,struct ceph_smmu_ops *b,const struct ceph_smmu_plan *p,uint32_t actlr)
{ return ceph_smmu_install_context(s,b,p,actlr); }
int cepheus_gpu_power_start(struct ceph_power_txn *t,struct ceph_power_ops *b)
{ return ceph_power_start(t,b); }
int cepheus_gpu_submit(struct ceph_submit *s,struct ceph_submit_ops *b,uint32_t seq)
{ return ceph_submit_once(s,b,seq); }
int cepheus_gpu_gmu_start(struct ceph_gmu_start *s,struct ceph_gmu_start_ops *b,const struct ceph_gmu_start_config *c)
{ return ceph_gmu_start_cold(s,b,c); }
