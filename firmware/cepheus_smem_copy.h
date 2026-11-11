/* Access must be authorized by the adapter. No live adapter or caller is provided. */
#ifndef CEPHEUS_SMEM_COPY_H
#define CEPHEUS_SMEM_COPY_H
#include "cepheus_smem_copied_view.h"

enum ceph_smem_copy_error {
    CE_SMEM_COPY_PHASE = -7, CE_SMEM_COPY_MAP = -8,
    CE_SMEM_COPY_READ = -9, CE_SMEM_COPY_CHANGED = -10
};
struct ceph_smem_copy_ops {
    void *context;
    int (*allow)(void *,uint64_t,uint32_t);
    int (*read)(void *,uint64_t,void *,uint32_t);
};

/* Retain both caller-owned copies, including partials on failure. Repeated
 * equality does not authenticate the metadata or exclude foreign writers. */
static inline int ceph_smem_copy(uint8_t output[CE_SMEM_ITEM_BYTES],
    struct ceph_smem_selection *info,uint8_t *first,uint8_t *second,
    uint32_t bytes,uint64_t physical_base,struct ceph_smem_scratch *scratch,
    const struct ceph_smem_copy_ops *ops,int pre_ebs)
{
    if(pre_ebs!=1)return CE_SMEM_COPY_PHASE;
    const void *p[]={output,info,first,second,scratch,ops};
    size_t n[]={CE_SMEM_ITEM_BYTES,sizeof(*info),bytes,bytes,sizeof(*scratch),sizeof(*ops)};
    if(bytes<4304 || bytes>CE_SMEM_MAX_REGION || physical_base>UINT64_MAX-bytes)
        return CE_SMEM_ARGUMENT;
    for(unsigned i=0;i<6;++i) {
        if(!ceph_smem_pointer_span(p[i],n[i]))return CE_SMEM_ARGUMENT;
        for(unsigned j=0;j<i;++j)
            if(ceph_smem_alias(p[i],n[i],p[j],n[j]))return CE_SMEM_ARGUMENT;
        if(physical_base<=UINTPTR_MAX && bytes<=UINTPTR_MAX-physical_base &&
            ceph_smem_alias(p[i],n[i],(const void *)(uintptr_t)physical_base,bytes))
            return CE_SMEM_ARGUMENT;
    }
    struct ceph_smem_copy_ops callbacks=*ops;
    if(!callbacks.allow || !callbacks.read)return CE_SMEM_ARGUMENT;
    for(unsigned pass=0;pass<2;++pass) {
        if(callbacks.allow(callbacks.context,physical_base,bytes))return CE_SMEM_COPY_MAP;
        uint8_t *destination=pass ? second : first;
        for(uint32_t offset=0;offset<bytes;) {
            uint32_t chunk=bytes-offset<4096 ? bytes-offset : 4096;
            if(callbacks.allow(callbacks.context,physical_base+offset,chunk))return CE_SMEM_COPY_MAP;
            if(callbacks.read(callbacks.context,physical_base+offset,destination+offset,chunk))
                return CE_SMEM_COPY_READ;
            offset+=chunk;
        }
        if(callbacks.allow(callbacks.context,physical_base,bytes))return CE_SMEM_COPY_MAP;
    }
    for(uint32_t i=0;i<bytes;++i)if(first[i]!=second[i])return CE_SMEM_COPY_CHANGED;
    return ceph_smem_extract(output,info,first,bytes,physical_base,scratch);
}
#endif
