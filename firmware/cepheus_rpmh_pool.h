/* Accepted AArch64 DXE pool ABI. Signatures alone are not an ownership lease. */
#ifndef CEPHEUS_RPMH_POOL_H
#define CEPHEUS_RPMH_POOL_H
#include "cepheus_rpmh_snapshot_reader.h"

static inline int ceph_rpmh_pool_check(const struct ceph_rpmh_snapshot_ops *ops,
    uint64_t address,uint32_t bytes)
{
    uint8_t head[24],tail[16],prefix[4],again[24];
    if(!ops || !ops->read || address<28 || !bytes || ((address-4)&7u))return -1;
    uint64_t base=address-28;
    if(ops->read(ops->context,base,head,24))return -1;
    uint32_t signature=ceph_rpmh_u32(head);
    uint64_t size=ceph_rpmh_u64(head+16);
    uint64_t minimum=((uint64_t)bytes+4u+7u)&~UINT64_C(7);
    if((signature!=0x30646870 && signature!=0x31646870) ||
        ceph_rpmh_u32(head+8)!=4 || size!=minimum+40 || base>UINT64_MAX-size)return -1;
    if(ops->read(ops->context,address-4,prefix,4) || ceph_rpmh_u32(prefix)!=bytes ||
        ops->read(ops->context,base+size-16,tail,16) ||
        ceph_rpmh_u32(tail)!=0x6c617470 || ceph_rpmh_u64(tail+8)!=size ||
        ops->read(ops->context,base,again,24) || !ceph_rpmh_equal(head,again,24))return -1;
    return 0;
}
#endif
