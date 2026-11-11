/* Copied RAM only. This parser never maps memory, calls SMEM or changes GPU gates. */
#ifndef CEPHEUS_SMEM_COPIED_VIEW_H
#define CEPHEUS_SMEM_COPIED_VIEW_H
#include <stdint.h>
#include <stddef.h>

#define CE_SMEM_ITEM 469u
#define CE_SMEM_ITEM_BYTES 4096u
#define CE_SMEM_MAX_REGION (16u * 1024u * 1024u)
#define CE_SMEM_MAX_PARTITIONS 84u
enum ceph_smem_error {
    CE_SMEM_ARGUMENT = -1, CE_SMEM_HEADER = -2, CE_SMEM_PARTITION = -3,
    CE_SMEM_ALLOCATION = -4, CE_SMEM_MISSING = -5, CE_SMEM_SIZE = -6
};
struct ceph_smem_selection {
    uint32_t version, item_count, offset, bytes, allocations;
};
struct ceph_smem_span { uint32_t offset, bytes; };
struct ceph_smem_partition {
    uint32_t offset, bytes, uncached, cached, line;
    uint16_t host0, host1;
};
struct ceph_smem_scratch {
    struct ceph_smem_partition partition[CE_SMEM_MAX_PARTITIONS];
    struct ceph_smem_span allocation[512];
    uint8_t seen[8192];
};

static inline uint16_t ceph_smem_u16(const uint8_t *p)
{ return (uint16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8); }
static inline uint32_t ceph_smem_u32(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static inline int ceph_smem_bounds(uint32_t offset,uint32_t bytes,uint32_t low,uint32_t high)
{ return low <= offset && offset <= high && bytes <= high - offset; }
static inline int ceph_smem_span_overlap(uint32_t a,uint32_t n,uint32_t b,uint32_t m)
{ return n && m && (uint64_t)a < (uint64_t)b + m && (uint64_t)b < (uint64_t)a + n; }
static inline int ceph_smem_pointer_span(const void *p,size_t n)
{ return p && n && (uintptr_t)p <= UINTPTR_MAX - n; }
static inline int ceph_smem_alias(const void *p,size_t n,const void *q,size_t m)
{ return (uintptr_t)p < (uintptr_t)q + m && (uintptr_t)q < (uintptr_t)p + n; }

static inline int ceph_smem_private(const uint8_t *view,uint32_t header,
    uint32_t low,uint32_t high,int cached,uint32_t count,
    struct ceph_smem_scratch *scratch,struct ceph_smem_selection *selected,
    uint32_t *length,uint32_t *padding)
{
    if(!ceph_smem_bounds(header,16,low,high))return CE_SMEM_ALLOCATION;
    const uint8_t *p=view+header;
    uint32_t item=ceph_smem_u16(p+2),size=ceph_smem_u32(p+4);
    uint32_t pad=ceph_smem_u16(p+8),headpad=ceph_smem_u16(p+10),data;
    if(ceph_smem_u16(p)!=0xa5a5 || item>=count || size<=pad ||
        ceph_smem_u32(p+12) || (scratch->seen[item/8] & (1u<<(item%8))))
        return CE_SMEM_ALLOCATION;
    scratch->seen[item/8] |= (uint8_t)(1u<<(item%8));
    if(cached) {
        if(headpad || size>header-low)return CE_SMEM_ALLOCATION;
        data=header-size;
    } else {
        if(headpad>high-header-16)return CE_SMEM_ALLOCATION;
        data=header+16+headpad;
        if(!ceph_smem_bounds(data,size,low,high))return CE_SMEM_ALLOCATION;
    }
    ++selected->allocations;
    if(item==CE_SMEM_ITEM) {selected->offset=data;selected->bytes=size-pad;}
    *length=size;*padding=headpad;
    return 0;
}

/* Outputs are published only on success; scratch is caller-owned and disposable.
 * Input must already be an accessible immutable copy, never a live SMEM pointer. */
static inline int ceph_smem_extract(uint8_t output[CE_SMEM_ITEM_BYTES],
    struct ceph_smem_selection *info,const uint8_t *view,uint32_t bytes,
    uint64_t physical_base,struct ceph_smem_scratch *scratch)
{
    if(bytes<208u+4096u || bytes>CE_SMEM_MAX_REGION ||
        physical_base>UINT64_MAX-bytes ||
        !ceph_smem_pointer_span(view,bytes) ||
        !ceph_smem_pointer_span(output,CE_SMEM_ITEM_BYTES) ||
        !ceph_smem_pointer_span(info,sizeof(*info)) ||
        !ceph_smem_pointer_span(scratch,sizeof(*scratch)))return CE_SMEM_ARGUMENT;
    if(ceph_smem_alias(view,bytes,scratch,sizeof(*scratch)) ||
        ceph_smem_alias(view,bytes,output,CE_SMEM_ITEM_BYTES) ||
        ceph_smem_alias(view,bytes,info,sizeof(*info)) ||
        ceph_smem_alias(scratch,sizeof(*scratch),output,CE_SMEM_ITEM_BYTES) ||
        ceph_smem_alias(scratch,sizeof(*scratch),info,sizeof(*info)) ||
        ceph_smem_alias(output,CE_SMEM_ITEM_BYTES,info,sizeof(*info)))return CE_SMEM_ARGUMENT;
    struct ceph_smem_selection result={ceph_smem_u32(view+92)>>16,512,0,0,0};
    uint32_t free=ceph_smem_u32(view+196),available=ceph_smem_u32(view+200);
    if(ceph_smem_u32(view+192)!=1 || ceph_smem_u32(view+204) ||
        free>bytes || available>bytes-free || (result.version!=11 && result.version!=12))
        return CE_SMEM_HEADER;
    uint32_t lower=result.version==11 ? 8400u : 208u,footer=bytes-4096u;
    uint32_t npart=0,entries=0,limit=bytes;
    int table=ceph_smem_u32(view+footer)==0x434f5424;
    if(table) {
        limit=footer;entries=ceph_smem_u32(view+footer+8);
        if(ceph_smem_u32(view+footer+4)!=1 || entries>CE_SMEM_MAX_PARTITIONS)
            return CE_SMEM_PARTITION;
        for(uint32_t i=0;i<entries;++i) {
            const uint8_t *p=view+footer+32+i*48;
            uint32_t offset=ceph_smem_u32(p),size=ceph_smem_u32(p+4);
            if(!offset && !size)continue;
            if(size<32 || (offset&7) || !ceph_smem_bounds(offset,size,lower,footer))
                return CE_SMEM_PARTITION;
            uint16_t host0=ceph_smem_u16(p+12),host1=ceph_smem_u16(p+14);
            for(uint32_t j=0;j<npart;++j) {
                const struct ceph_smem_partition *q=&scratch->partition[j];
                if((host0==q->host0 && host1==q->host1) ||
                    (host0==q->host1 && host1==q->host0) ||
                    ceph_smem_span_overlap(offset,size,q->offset,q->bytes))
                    return CE_SMEM_PARTITION;
            }
            const uint8_t *h=view+offset;
            uint32_t uncached=ceph_smem_u32(h+12),cached=ceph_smem_u32(h+16);
            if(ceph_smem_u32(h)!=0x54525024 || ceph_smem_u16(h+4)!=host0 ||
                ceph_smem_u16(h+6)!=host1 || ceph_smem_u32(h+8)!=size ||
                uncached<32 || uncached>cached || cached>size)return CE_SMEM_PARTITION;
            scratch->partition[npart++]=(struct ceph_smem_partition){
                offset,size,uncached,cached,ceph_smem_u32(p+16),host0,host1};
        }
        if(result.version==12) {
            const uint8_t *p=view+footer+32+entries*48;
            result.item_count=ceph_smem_u16(p+16);
            if(ceph_smem_u32(p)!=0x49494953 || ceph_smem_u32(p+4)!=bytes ||
                ceph_smem_u32(p+8)!=physical_base || ceph_smem_u32(p+12) ||
                result.item_count<=CE_SMEM_ITEM)return CE_SMEM_PARTITION;
        }
    } else if(result.version==12)return CE_SMEM_PARTITION;
    if(result.version==11) {
        if(free<8400 || free>limit)return CE_SMEM_HEADER;
        uint32_t spans=0;
        for(uint32_t item=0;item<512;++item) {
            const uint8_t *p=view+208+item*16;
            uint32_t allocated=ceph_smem_u32(p),offset=ceph_smem_u32(p+4);
            uint32_t size=ceph_smem_u32(p+8),aux=ceph_smem_u32(p+12)&0xfffffffcu;
            if(!allocated)continue;
            if(allocated!=1 || !size || (aux && aux!=physical_base))return CE_SMEM_ALLOCATION;
            if(item<8) {
                if(!ceph_smem_bounds(offset,size,0,8400))return CE_SMEM_ALLOCATION;
            } else {
                if((offset&7) || (size&7) || !ceph_smem_bounds(offset,size,8400,free))
                    return CE_SMEM_ALLOCATION;
                for(uint32_t j=0;j<npart;++j)
                    if(ceph_smem_span_overlap(offset,size,scratch->partition[j].offset,
                        scratch->partition[j].bytes))return CE_SMEM_ALLOCATION;
            }
            for(uint32_t j=0;j<spans;++j)
                if(ceph_smem_span_overlap(offset,size,scratch->allocation[j].offset,
                    scratch->allocation[j].bytes))return CE_SMEM_ALLOCATION;
            scratch->allocation[spans++]=(struct ceph_smem_span){offset,size};
            ++result.allocations;
            if(item==CE_SMEM_ITEM){result.offset=offset;result.bytes=size;}
        }
    } else {
        const struct ceph_smem_partition *global=NULL;
        for(uint32_t j=0;j<npart;++j)if(scratch->partition[j].host0==0xfffe &&
            scratch->partition[j].host1==0xfffe)global=&scratch->partition[j];
        if(!global)return CE_SMEM_PARTITION;
        for(uint32_t j=0;j<sizeof(scratch->seen);++j)scratch->seen[j]=0;
        uint32_t start=global->offset,end=start+global->bytes;
        uint32_t uncached=start+global->uncached,cached=start+global->cached;
        uint32_t cursor=start+32;
        while(cursor<uncached) {
            uint32_t length,padding;
            int error=ceph_smem_private(view,cursor,start+32,uncached,0,
                result.item_count,scratch,&result,&length,&padding);
            if(error)return error;
            cursor+=16+padding+length;
        }
        if(cursor!=uncached)return CE_SMEM_ALLOCATION;
        if(cached!=end) {
            uint32_t line=global->line;
            if(line<32 || line>4096 || (line&(line-1)) || global->bytes%line)
                return CE_SMEM_ALLOCATION;
            int64_t reverse=(int64_t)end-line;
            while(reverse>cached) {
                uint32_t length,padding;
                int error=ceph_smem_private(view,(uint32_t)reverse,cached,end,1,
                    result.item_count,scratch,&result,&length,&padding);
                if(error)return error;
                reverse-=(int64_t)length+line;
            }
            if(reverse+line!=cached)return CE_SMEM_ALLOCATION;
        }
    }
    if(!result.bytes)return CE_SMEM_MISSING;
    if(result.bytes!=CE_SMEM_ITEM_BYTES)return CE_SMEM_SIZE;
    if(!ceph_smem_bounds(result.offset,CE_SMEM_ITEM_BYTES,0,bytes))return CE_SMEM_ALLOCATION;
    for(uint32_t i=0;i<CE_SMEM_ITEM_BYTES;++i)output[i]=view[result.offset+i];
    *info=result;
    return 0;
}
#endif
