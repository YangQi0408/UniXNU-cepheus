/* Reader callbacks must check RAM ranges before dereferencing, never MMIO. */
#ifndef CEPHEUS_RPMH_SNAPSHOT_READER_H
#define CEPHEUS_RPMH_SNAPSHOT_READER_H
#include "cepheus_rpmh_snapshot.h"

struct ceph_rpmh_snapshot_ops {
    void *context;
    int (*read)(void *,uint64_t,void *,uint32_t);
    int (*pool)(void *,uint64_t,uint32_t);
};
struct ceph_rpmh_snapshot_scratch {
    uint8_t resources[CE_RPMH_RESOURCE_LIMIT][24];
    uint8_t drv[2][CE_RPMH_DRV_LIMIT][72];
    uint64_t pointers[2][CE_RPMH_DRV_LIMIT];
};
static inline uint32_t ceph_rpmh_u32(const uint8_t *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static inline uint64_t ceph_rpmh_u64(const uint8_t *p)
{ return (uint64_t)ceph_rpmh_u32(p)|(uint64_t)ceph_rpmh_u32(p+4)<<32; }
static inline int ceph_rpmh_equal(const uint8_t *a,const uint8_t *b,unsigned n)
{ for(unsigned i=0;i<n;++i)if(a[i]!=b[i])return 0;return 1; }

static inline int ceph_rpmh_snapshot_read(struct ceph_rpmh_snapshot *out,
    struct ceph_rpmh_snapshot_scratch *scratch,const struct ceph_rpmh_snapshot_ops *ops,
    uint64_t globals,const uint32_t addresses[2])
{
    uint8_t root[16],check[72];
    if(!out || !scratch || !ops || !ops->read || !ops->pool || !addresses ||
        addresses[0]==addresses[1] || globals>UINT64_MAX-16)return CE_RPMH_SNAP_BIND;
    out->resource[0]=(struct ceph_rpmh_resource_snapshot){0};
    out->resource[1]=(struct ceph_rpmh_resource_snapshot){0};
    if(ops->read(ops->context,globals,root,16))return CE_RPMH_SNAP_MAP;
    unsigned count=ceph_rpmh_u32(root);
    uint64_t array=ceph_rpmh_u64(root+4);
    out->resource_count=count;
    if(!count || count>CE_RPMH_RESOURCE_LIMIT || !array ||
        array>UINT64_MAX-count*24u)return CE_RPMH_SNAP_ARRAY;
    if(ops->pool(ops->context,array,count*24u))return CE_RPMH_SNAP_POOL;
    if(ops->read(ops->context,array,scratch->resources,count*24u))return CE_RPMH_SNAP_MAP;
    unsigned found[2]={CE_RPMH_RESOURCE_LIMIT,CE_RPMH_RESOURCE_LIMIT};
    for(unsigned i=0;i<count;++i) {
        uint32_t address=ceph_rpmh_u32(scratch->resources[i]);
        if(!address || (i && address<=ceph_rpmh_u32(scratch->resources[i-1])))
            return CE_RPMH_SNAP_ARRAY;
        for(unsigned r=0;r<2;++r)if(address==addresses[r])found[r]=i;
    }
    for(unsigned r=0;r<2;++r) {
        if(found[r]==CE_RPMH_RESOURCE_LIMIT)return CE_RPMH_SNAP_MISSING;
        struct ceph_rpmh_resource_snapshot *v=&out->resource[r];
        v->address=addresses[r];
        const uint8_t *entry=scratch->resources[found[r]];
        v->pdc_present=ceph_rpmh_u64(entry+16)!=0;
        uint64_t next=ceph_rpmh_u64(entry+8);
        if(!next)return CE_RPMH_SNAP_MISSING;
        while(next) {
            unsigned n=v->count;
            if(n==CE_RPMH_DRV_LIMIT)return CE_RPMH_SNAP_CHAIN;
            for(unsigned j=0;j<=r;++j)for(unsigned i=0;i<out->resource[j].count;++i)
                if(next==scratch->pointers[j][i])return CE_RPMH_SNAP_CHAIN;
            if(ops->pool(ops->context,next,72))return CE_RPMH_SNAP_POOL;
            uint8_t *raw=scratch->drv[r][n];
            if(ops->read(ops->context,next,raw,72))return CE_RPMH_SNAP_MAP;
            struct ceph_rpmh_drv_snapshot *d=&v->drv[n];
            d->id=ceph_rpmh_u32(raw);d->state=ceph_rpmh_u32(raw+4);
            d->priority=ceph_rpmh_u32(raw+8);d->shadow_data=ceph_rpmh_u32(raw+12);
            d->shadow_valid=raw[16];
            if(d->state>1 || d->priority>1 || d->shadow_valid>1)return CE_RPMH_SNAP_CHAIN;
            for(unsigned i=0;i<n;++i)if(d->id==v->drv[i].id)return CE_RPMH_SNAP_CHAIN;
            for(unsigned k=0;k<3;++k) {
                const uint8_t *vote=raw+24+12*k;
                d->local[k]=(struct ceph_rpmh_vote){ceph_rpmh_u32(vote),vote[4],ceph_rpmh_u32(vote+8)};
                if(d->local[k].valid>1 || d->local[k].dirty>4)return CE_RPMH_SNAP_CHAIN;
            }
            scratch->pointers[r][n]=next;
            ++v->count;
            next=ceph_rpmh_u64(raw+64);
        }
    }
    /* Re-read full records, not only count/pointer. Repeated equality is not
     * proof of atomicity, foreign-writer exclusion, or a hardware ACK. */
    for(unsigned r=0;r<2;++r)for(unsigned i=0;i<out->resource[r].count;++i) {
        uint64_t pointer=scratch->pointers[r][i];
        if(ops->pool(ops->context,pointer,72))return CE_RPMH_SNAP_POOL;
        if(ops->read(ops->context,pointer,check,72))return CE_RPMH_SNAP_MAP;
        if(!ceph_rpmh_equal(check,scratch->drv[r][i],72))return CE_RPMH_SNAP_CHANGED;
    }
    if(ops->pool(ops->context,array,count*24u))return CE_RPMH_SNAP_POOL;
    for(unsigned i=0;i<count;++i) {
        if(ops->read(ops->context,array+24u*i,check,24))return CE_RPMH_SNAP_MAP;
        if(!ceph_rpmh_equal(check,scratch->resources[i],24))return CE_RPMH_SNAP_CHANGED;
    }
    if(ops->read(ops->context,globals,check,16))return CE_RPMH_SNAP_MAP;
    if(!ceph_rpmh_equal(check,root,16))return CE_RPMH_SNAP_CHANGED;
    return 0;
}
#endif
