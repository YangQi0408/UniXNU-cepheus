/* Participating XNU apps-DRV exclusion only, never an inherited-vote owner. */
#ifndef CEPHEUS_RPMH_SCOPE_H
#define CEPHEUS_RPMH_SCOPE_H
#include <stdint.h>
/* Hold requires a real scoped CPU-idle token and already initialized mappings.
 * Token 1 is one-shot per boot. No MMIO/SMC/request is performed by these APIs.
 * Any attempted GPU transport store permanently retains the reservation.
 * Global/secure/hardware writers and current requests remain unproven. */
int cepheus_gpu_rpmh_hold(uint64_t idle_token,uint64_t *token);
int cepheus_gpu_rpmh_check(uint64_t token);
int cepheus_gpu_rpmh_finish(uint64_t token,int keep);
#endif
