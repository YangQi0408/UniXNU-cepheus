#ifndef CEPHEUS_GPU_COMPILE_H
#define CEPHEUS_GPU_COMPILE_H
#include "cepheus_hfi_boot.h"
#include "cepheus_gmu_upload.h"
#include "cepheus_smmu_install.h"
#include "cepheus_gpu_power_txn.h"
#include "cepheus_gpu_submit.h"
#include "cepheus_gmu_start.h"
int cepheus_gpu_hfi_boot(struct ceph_hfi_session *,const struct ceph_hfi_boot_config *,uint32_t *);
int cepheus_gpu_upload(struct ceph_gmu_upload *,const uint8_t *,size_t,struct ceph_gmu_block *,unsigned);
int cepheus_gpu_smmu_install(struct ceph_smmu_install *,struct ceph_smmu_ops *,const struct ceph_smmu_plan *,uint32_t);
int cepheus_gpu_power_start(struct ceph_power_txn *,struct ceph_power_ops *);
int cepheus_gpu_submit(struct ceph_submit *,struct ceph_submit_ops *,uint32_t);
int cepheus_gpu_gmu_start(struct ceph_gmu_start *,struct ceph_gmu_start_ops *,const struct ceph_gmu_start_config *);
#endif
