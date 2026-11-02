/* Integrate concrete GPU engines; no platform discovery/ownership fallback.
 * No startup hook invokes this until actual ARC/clock/TBU/SMMU/PDC ownership
 * adapters have been verified. Device writes only occur inside supplied engines.
 * The bundle is address-stable/single-owner throughout this one-shot attempt.
 */
#ifndef CEPHEUS_GMU_BRINGUP_H
#define CEPHEUS_GMU_BRINGUP_H
#include "cepheus_gpu_power_txn.h"
#include "cepheus_gpu_pdc.h"
#include "cepheus_smmu_install.h"
#include "cepheus_gmu_upload.h"
#include "cepheus_gmu_hfi_link.h"
#include "cepheus_hfi_boot.h"
enum ceph_bringup_stage {CE_BR_NEW,CE_BR_MEMORY,CE_BR_POWER,CE_BR_BOUND,CE_BR_SHARED,
    CE_BR_PDC,CE_BR_SMMU,CE_BR_UPLOAD,CE_BR_CM3,CE_BR_HFI,CE_BR_FAILED};
struct ceph_gmu_bringup;
struct ceph_gmu_bringup_backends {
    void *owner;
    /* Resolve exact mapped windows/reservations/plan/config from successful
     * power+CPU workspace. Must make no hardware writes and invent no metadata. */
    int (*bind)(void *,struct ceph_gmu_bringup *);
    struct ceph_power_ops power;
    struct ceph_pdc_ops pdc;
    struct ceph_smmu_ops smmu;
    struct ceph_gmu_start_ops cm3;
    struct ceph_gmu_hfi_transport transport;
    struct ceph_hfi_boot_config hfi;
    struct ceph_pdc_input pdc_input;
    struct ceph_smmu_plan smmu_plan;
    struct ceph_gmu_upload upload;
    void (*failed)(void *,enum ceph_bringup_stage,int); /* retain/dump, NO assumed safe rollback */
};
struct ceph_gmu_bringup {
    enum ceph_bringup_stage stage,failed_stage;
    int error;
    uint64_t firmware;
    struct ceph_gmu_workspace workspace;
    struct ceph_power_txn power;
    struct ceph_pdc_install pdc;
    struct ceph_smmu_install smmu;
    struct ceph_gmu_start cm3;
    struct ceph_gmu_hfi_link link;
    struct ceph_gmu_mapping upload_maps[3];
    struct ceph_gmu_block blocks[32];
    struct ceph_gmu_bringup_backends backend;
    uint32_t fw_version;
};
#ifdef __cplusplus
extern "C" {
#endif
int cepheus_gmu_bringup_prepare(struct ceph_gmu_bringup *,const uint8_t *firmware,size_t bytes);
int cepheus_gmu_bringup_start(struct ceph_gmu_bringup *);
#ifdef __cplusplus
}
#endif
#endif
