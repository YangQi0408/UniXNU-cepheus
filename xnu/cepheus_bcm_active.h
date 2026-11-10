/* Private fixed GPU-route BCM transport. Not an inherited-vote/owner source. */
#ifndef CEPHEUS_BCM_ACTIVE_H
#define CEPHEUS_BCM_ACTIVE_H
#include "cepheus_bcm.h"
enum ceph_bcm_active_id {CE_BA_SH2,CE_BA_SN15,CE_BA_SN3,CE_BA_SH0,CE_BA_MC0,CE_BA_ACV,CE_BA_NRESOURCE};
struct ceph_bcm_active_vote {
    enum ceph_bcm_active_id id;
    uint32_t current_x,current_y,target_x,target_y;
};
struct ceph_bcm_active_owner {
    void *ctx;
    /* Actual exclusion of apps-DRV/VCD writers and deep idle, live RSC/DB
     * mappings, stable command DB, no unknown pending VCD commit; not TCS idle.
     * Non-sleeping callbacks/context must survive uncertainty/retention.
     */
    int (*validate)(void *);
    /* Bind the real inherited/ACKed apps request and exact actual metadata. */
    int (*current)(void *,enum ceph_bcm_active_id,const struct ceph_bcm_resource *,uint32_t,uint32_t);
    /* Once after any attempted store; keep=1 retains every resource on error.
     * keep=0 follows ACK+slot cleanup, not permission to lower/free live votes.
     */
    void (*finish)(void *,unsigned attempted_stores,int keep);
};
/* Encoded X/Y components, never AB/IB or a prebuilt arbitrary command. Each
 * component stays within14 bits and cannot decrease; each included vote raises
 * at least one component. ACV admits only X=0,Y=0->8. No saturation or retry.
 * Uses existing initialized RPMh mappings; caller updates ledger on success.
 */
int cepheus_gpu_bcm_raise(const struct ceph_bcm_active_vote *,unsigned,const struct ceph_bcm_active_owner *);
#endif
