/* Trusted C/C++ pool bridge for the separate GX CP group. No hardware API. */
#ifndef CEPHEUS_CP_MEMORY_PRIVATE_H
#define CEPHEUS_CP_MEMORY_PRIVATE_H
#include "cepheus_cp_workspace.h"
#include "cepheus_gpu_firmware.h"
#include "cepheus_cp_init.h"
struct ceph_cp_memory_view {
    struct ceph_cp_publication publication;
    struct ceph_gpu_memory_view views[CE_CP_BUFFERS];
};
static inline int ceph_cp_memory_output(const struct ceph_gpu_memory_view views[CE_CP_BUFFERS],
    const void *out,size_t bytes,const void *identity,size_t identity_bytes)
{
    uintptr_t dst=(uintptr_t)out,self=(uintptr_t)identity;
    if(!dst || (dst&7u) || !bytes || bytes>UINTPTR_MAX-dst || identity_bytes>UINTPTR_MAX-self ||
        (dst<self+identity_bytes && self<dst+bytes))return -1;
    for(unsigned i=0;i<CE_CP_BUFFERS;++i) {
        uintptr_t base=(uintptr_t)views[i].cpu;
        if(views[i].bytes>UINTPTR_MAX-base ||
            (dst<base+views[i].bytes && base<dst+bytes))return -1;
    }
    return 0;
}
/* Exact actual backing, fixed SQE bytes, restore structure and all PTEs.
 * Caller owns pool lock/writer exclusion. CPU VAs are not made read-only. */
static inline int ceph_cp_memory_layout(const struct ceph_gpu_memory_view views[CE_CP_BUFFERS],
    const struct ceph_cp_publication *p)
{
    if(!views || !p || p->memory.smr!=0x84010000 || p->memory.cb>=8 || p->memory.slot>=128 ||
        !p->memory.asid || p->memory.asid>255 || !p->table_used || p->table_used>32)return -1;
    const size_t minimum[]={32768,4096,32768,4096,4096,131072};
    for(unsigned i=0;i<CE_CP_BUFFERS;++i) {
        const struct ceph_gpu_memory_view *v=&views[i];
        uintptr_t base=(uintptr_t)v->cpu;
        if(!base || (base&4095u) || v->bytes<minimum[i] ||
            v->bytes>((minimum[i]+16383)&~(size_t)16383) || v->bytes>UINTPTR_MAX-base ||
            !v->pa || (v->pa&4095u) || v->pa>=(1ULL<<40) || v->bytes>(1ULL<<40)-v->pa)return -1;
        for(unsigned j=0;j<i;++j) {
            uintptr_t other=(uintptr_t)views[j].cpu;
            if((base<other+views[j].bytes && other<base+v->bytes) ||
                (v->pa<views[j].pa+views[j].bytes && views[j].pa<v->pa+v->bytes))return -1;
        }
        if(i<CE_CP_ROOT && (p->regions[i].pa!=v->pa || p->regions[i].bytes!=minimum[i] ||
            p->regions[i].va<0x300000 || p->regions[i].permissions!=ceph_cp_workspace_permissions(i)))return -1;
    }
    if(p->memory.ttbr0!=(views[CE_CP_ROOT].pa|((uint64_t)p->memory.asid<<48)) ||
        p->memory.ttbr1!=((uint64_t)p->memory.asid<<48))return -1;
    struct ceph_vm_pool table={(uint64_t *)views[CE_CP_ROOT].cpu,views[CE_CP_ROOT].pa,32,p->table_used};
    if(ceph_vm39_matches(&table,p->regions,CE_CP_ROOT))return -1;
    static const uint8_t expected[32]={0xec,0x4c,0x62,0xfc,0x9d,0x38,0x44,0x33,0x7d,0x54,0xfc,0x6a,0x04,0xfe,0x01,0x82,
        0x9e,0xff,0xf3,0x77,0x9f,0xe0,0x3b,0xcc,0xe9,0x6e,0xe8,0x7d,0x2d,0x52,0x3c,0xe0};
    uint8_t digest[32];
    if(ceph_firmware_sha256((const uint8_t *)views[CE_CP_SQE].cpu,32300,digest))return -1;
    for(unsigned i=0;i<32;++i)if(digest[i]!=expected[i])return -1;
    const volatile uint8_t *sqe=(const volatile uint8_t *)views[CE_CP_SQE].cpu;
    for(size_t i=32300;i<views[CE_CP_SQE].bytes;++i)if(sqe[i])return -1;
    const volatile uint32_t *restore=(const volatile uint32_t *)views[CE_CP_RESTORE].cpu;
    if(restore[0] || restore[1] || restore[2] || restore[3]!=(132u|(76u<<16)))return -1;
    for(unsigned i=0;i<66;++i) {
        if(restore[4+2*i]!=ceph_cp_restore_offsets[i])return -1;
        uint32_t value=restore[5+2*i],word;
        if(i==4 && (value&11u)!=11u)return -1;
        if(i>=5 && i<37 && (ceph_cp_protect_word(i-5,&word) || value!=word))return -1;
        if(i>=38 && i<48 && !(value&1u))return -1;
    }
    if(ceph_firmware_sha256((const uint8_t *)views[CE_CP_RESTORE].cpu,CE_CP_REGLIST_WORDS*4,digest))return -1;
    for(unsigned i=0;i<32;++i)if(digest[i]!=p->restore_sha256[i])return -1;
    const volatile uint8_t *tail=(const volatile uint8_t *)views[CE_CP_RESTORE].cpu;
    for(size_t i=CE_CP_REGLIST_WORDS*4;i<views[CE_CP_RESTORE].bytes;++i)if(tail[i])return -1;
    return 0;
}
#ifdef __cplusplus
extern "C" {
#endif
int ceph_memory_cp_seal(const uint64_t[CE_CP_BUFFERS],const struct ceph_gpu_memory_view[CE_CP_BUFFERS],
    const struct ceph_vm_region[CE_CP_ROOT],unsigned,const struct ceph_gx_smmu_plan *,struct ceph_cp_publication *);
/* Temporary pin -> exposure barrier -> installed result. REFUSED is legal
 * only before publish; uncertainty keeps all backing and poisons the pool. */
int ceph_memory_cp_acquire(const struct ceph_cp_publication *,struct ceph_cp_memory_view *);
int ceph_memory_cp_validate(const struct ceph_cp_publication *);
int ceph_memory_cp_publish(const struct ceph_cp_publication *);
enum {CE_MEMORY_CP_REFUSED,CE_MEMORY_CP_INSTALLED,CE_MEMORY_CP_UNCERTAIN};
int ceph_memory_cp_finish(const struct ceph_cp_publication *,int);
#ifdef __cplusplus
}
#endif
#endif
