/* GX admission stays separate from the legacy GMU SID4/5 interface. */
#ifndef CEPHEUS_GX_SMMU_INSTALL_H
#define CEPHEUS_GX_SMMU_INSTALL_H
#include "cepheus_gx_smmu_plan.h"
#include "cepheus_smmu_install.h"
static inline int ceph_gx_smmu_install_context(struct ceph_smmu_install *s,
    struct ceph_smmu_ops *b,const struct ceph_gx_smmu_plan *p)
{
    if(!ceph_gx_smmu_plan_canonical(p))return -1;
    return ceph_smmu_install_reserved(s,b,&p->context,p->actlr);
}
#endif
