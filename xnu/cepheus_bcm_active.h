/* Private fixed GPU/crypto-route BCM transport. Not a vote/owner source. */
#ifndef CEPHEUS_BCM_ACTIVE_H
#define CEPHEUS_BCM_ACTIVE_H
#include "cepheus_bcm.h"
struct ceph_rpmh_active_lease;
enum ceph_bcm_active_id {
    CE_BA_SH2,CE_BA_SN15,CE_BA_SN3,CE_BA_SH0,CE_BA_MC0,CE_BA_ACV,
    CE_BA_CE0,CE_BA_SN11,CE_BA_SN0,CE_BA_NRESOURCE
};
#define CE_BA_MAX_VOTES 6u
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
    /* Once after any attempted store. The read-only lease carries the selected
     * ACTIVE slot, exact commands/status, store count and terminal state; verify
     * its identity before updating the vote ledger. keep=1 retains every
     * resource on error. keep=0 follows ACK+slot cleanup, not permission to
     * lower/free live votes. */
    void (*finish)(void *,const struct ceph_rpmh_active_lease *,int keep);
    /* Participating-writer reservation, not inherited request evidence.
     * Management/finish callbacks must not recurse into scope APIs. */
    uint64_t rpmh_token;
};
/* Encoded X/Y components, never AB/IB or a prebuilt arbitrary command. Each
 * component stays within14 bits and cannot decrease; each included vote raises
 * at least one component. ACV admits only X=0,Y=0->8. No saturation or retry.
 * Uses existing initialized RPMh mappings; caller updates ledger on success.
 */
int cepheus_gpu_bcm_raise(const struct ceph_bcm_active_vote *,unsigned,const struct ceph_bcm_active_owner *);
/* Fixed crypto route CE0/SN11/SN0/SH0/MC0/ACV. The same encoded-component,
 * live-owner, ACK/retention and shared RPMh/idle rules apply. This entry does
 * not aggregate IB/AB, enable crypto clocks, authenticate PAS or start GPU.
 * GPU-only IDs remain refused here, and crypto-only IDs by the GPU entry. */
int cepheus_zap_bcm_raise(const struct ceph_bcm_active_vote *,unsigned,const struct ceph_bcm_active_owner *);
#endif
