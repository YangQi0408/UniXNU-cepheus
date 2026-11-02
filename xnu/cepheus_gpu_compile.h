#ifndef CEPHEUS_GPU_COMPILE_H
#define CEPHEUS_GPU_COMPILE_H
#include "cepheus_hfi_boot.h"
#include "cepheus_gmu_upload.h"
#include "cepheus_smmu_install.h"
#include "cepheus_gpu_power_txn.h"
#include "cepheus_gpu_submit.h"
#include "cepheus_gmu_start.h"
#include "cepheus_gpu_ring.h"
#include "cepheus_sqe.h"
#include "cepheus_gpu_bw.h"
#include "cepheus_gpu_fault.h"
#include "cepheus_gpu_memory.h"
#include "cepheus_gpu_vm.h"
#include "cepheus_gpu_va.h"
#include "cepheus_gpu_pdc.h"
#include "cepheus_gpu_thermal.h"
int cepheus_gpu_hfi_boot(struct ceph_hfi_session *,const struct ceph_hfi_boot_config *,uint32_t *);
int cepheus_gpu_upload(struct ceph_gmu_upload *,const uint8_t *,size_t,struct ceph_gmu_block *,unsigned);
int cepheus_gpu_smmu_install(struct ceph_smmu_install *,struct ceph_smmu_ops *,const struct ceph_smmu_plan *,uint32_t);
int cepheus_gpu_power_start(struct ceph_power_txn *,struct ceph_power_ops *);
int cepheus_gpu_submit(struct ceph_submit *,struct ceph_submit_ops *,uint32_t);
int cepheus_gpu_gmu_start(struct ceph_gmu_start *,struct ceph_gmu_start_ops *,const struct ceph_gmu_start_config *);
int cepheus_gpu_ring_submit(struct ceph_gpu_ring *,uint64_t,uint64_t,uint64_t,uint32_t,uint64_t,uint32_t);
int cepheus_gpu_sqe_copy(const uint8_t *,size_t,uint8_t *,size_t,struct ceph_sqe_image *);
int cepheus_gpu_bandwidth_table(const struct ceph_bw_route *,const struct ceph_bw_level *,const struct ceph_bw_route *,const struct ceph_bw_level *,unsigned,struct ceph_hfi_bw *);
int cepheus_gpu_fault_read(struct ceph_gpu_fault_ops *,struct ceph_gpu_fault_record *);
/* Preferred sealed-snapshot entrypoints: hold immutable view through use. */
int cepheus_gpu_upload_snapshot(struct ceph_gmu_upload *,uint64_t,struct ceph_gmu_block *,unsigned);
int cepheus_gpu_sqe_snapshot_copy(uint64_t,uint8_t *,size_t,struct ceph_sqe_image *);
int cepheus_gpu_vm_build(struct ceph_vm_pool *,const struct ceph_vm_region *,unsigned);
int cepheus_gpu_vm_lookup(struct ceph_vm_pool *,uint64_t,uint64_t *,uint64_t *);
int cepheus_gpu_va_build(const struct ceph_gpu_backing *,struct ceph_gpu_va_plan *);
int cepheus_gpu_pdc_install(struct ceph_pdc_install *,struct ceph_pdc_ops *,const struct ceph_pdc_input *);
int cepheus_gpu_thermal_guard(struct ceph_gpu_thermal *,const struct ceph_gpu_temperature *,uint64_t);
#endif
