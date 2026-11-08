/* CPU-only ZAP ELF preparation. Follows the pinned SM8150 peripheral-loader.c
 * relocation/allocation policy. No PA assignment, PAS calls or signature check.
 * Production entrypoint below accepts only a sealed, digest-verified snapshot.
 * The output image is logical-size staging data, NOT a secure allocation.
 */
#ifndef CEPHEUS_ZAP_IMAGE_H
#define CEPHEUS_ZAP_IMAGE_H
#include <stddef.h>
#include <stdint.h>

#define CE_ZAP_MAX_SEGMENTS 32u
struct ceph_zap_segment {uint32_t file_offset,filesz,memsz,image_offset;};
struct ceph_zap_image {
    uint32_t header_bytes,hash_offset,hash_bytes,mdt_bytes;
    uint32_t relocation_base,entry_offset,logical_bytes,allocation_bytes;
    unsigned segments;
    struct ceph_zap_segment load[CE_ZAP_MAX_SEGMENTS];
};
static inline uint32_t ceph_zap_le32(const uint8_t *p)
{return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static inline uint16_t ceph_zap_le16(const uint8_t *p)
{return (uint16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8));}
static inline int ceph_zap_overlap(uint64_t a,uint64_t n,uint64_t b,uint64_t m)
{return n && m && a<b+m && b<a+n;}

static inline int ceph_zap_parse(const uint8_t *elf,size_t bytes,struct ceph_zap_image *out)
{
    if(!elf || !out || bytes<52 || bytes>1048576 || elf[0]!=0x7f ||
        elf[1]!='E' || elf[2]!='L' || elf[3]!='F' || elf[4]!=1 || elf[5]!=1 || elf[6]!=1 ||
        ceph_zap_le16(elf+16)!=2 || ceph_zap_le16(elf+18)!=164 || ceph_zap_le32(elf+20)!=1 ||
        ceph_zap_le32(elf+28)!=52 || ceph_zap_le16(elf+40)!=52 || ceph_zap_le16(elf+42)!=32)return -1;
    unsigned count=ceph_zap_le16(elf+44);
    if(count<3 || count>CE_ZAP_MAX_SEGMENTS || 52+32*count>bytes)return -1;
#ifdef __cplusplus
    struct ceph_zap_image p={};
#else
    struct ceph_zap_image p={0};
#endif
    p.header_bytes=52+32*count;
    uint64_t low=UINT64_MAX,high=0;uint32_t alignment=0;
    for(unsigned i=0;i<count;++i) {
        const uint8_t *h=elf+52+32*i;
        uint32_t type=ceph_zap_le32(h),off=ceph_zap_le32(h+4),pa=ceph_zap_le32(h+12);
        uint32_t size=ceph_zap_le32(h+16),mem=ceph_zap_le32(h+20);
        uint32_t flags=ceph_zap_le32(h+24),align=ceph_zap_le32(h+28);
        uint64_t end=(uint64_t)pa+mem;
        int hash=(flags&0x07000000u)==0x02000000u;
        if(off>bytes || size>bytes-off || end>(1ULL<<32) || (align && (align&(align-1))))return -1;
        /* This profile carries one header segment, one hash segment and only
         * relocatable PT_LOAD data. Refuse silently ignored extra segments. */
        if(!i) {
            if(type || off || size!=p.header_bytes || mem || hash)return -1;
            continue;
        }
        if(hash) {
            if(p.hash_bytes || !size || off<p.header_bytes || (flags&(1u<<27)))return -1;
            p.hash_offset=off;p.hash_bytes=size;continue;
        }
        if(type!=1 || !mem || !(flags&(1u<<27)) || size>mem || (size && off<p.header_bytes))return -1;
        for(unsigned j=0;j<p.segments;++j) {
            const struct ceph_zap_segment *s=&p.load[j];
            if(ceph_zap_overlap(pa,mem,s->image_offset,s->memsz) ||
                ceph_zap_overlap(off,size,s->file_offset,s->filesz))return -1;
        }
        p.load[p.segments++]=(struct ceph_zap_segment){off,size,mem,pa};
        if(pa<low){low=pa;alignment=align;}
        if(end>high)high=end;
    }
    if(!p.segments || !p.hash_bytes || (low&4095u))return -1;
    uint64_t logical=((high+4095u)&~4095ULL)-low;
    uint64_t unit=alignment>=0x400000u?0x400000u:alignment>=0x100000u?0x100000u:4096u;
    uint64_t allocation=(logical+unit-1)&~(unit-1);
    if(!logical || allocation>0x400000u)return -1;
    uint32_t entry=ceph_zap_le32(elf+24);int entry_found=0;
    for(unsigned i=0;i<p.segments;++i) {
        struct ceph_zap_segment *s=&p.load[i];
        if(ceph_zap_overlap(s->file_offset,s->filesz,p.hash_offset,p.hash_bytes))return -1;
        if(entry>=s->image_offset && (uint64_t)entry-s->image_offset<s->memsz)entry_found=1;
        s->image_offset-=(uint32_t)low;
    }
    if(!entry_found)return -1;
    p.relocation_base=(uint32_t)low;p.entry_offset=entry-(uint32_t)low;
    p.logical_bytes=(uint32_t)logical;p.allocation_bytes=(uint32_t)allocation;
    p.mdt_bytes=p.header_bytes+p.hash_bytes;
    *out=p;return 0;
}
static inline int ceph_zap_cpu_range(const void *p,size_t n)
{return p && n && n<=UINTPTR_MAX-(uintptr_t)p;}
static inline int ceph_zap_cpu_overlap(const void *a,size_t n,const void *b,size_t m)
{return ceph_zap_overlap((uintptr_t)a,n,(uintptr_t)b,m);}
static inline void ceph_zap_mdt_copy(uint8_t *dst,const uint8_t *src,size_t bytes)
{
    volatile uint8_t *device=dst;
    for(size_t i=0;i<bytes;++i)device[i]=src[i];
}

/* Reload only the image after PAS mem_setup. The MDT may already belong to
 * secure world and must not be rewritten as a side effect of this operation. */
static inline int ceph_zap_stage_image(const uint8_t *elf,size_t bytes,
    uint8_t *image,size_t capacity,struct ceph_zap_image *out)
{
    struct ceph_zap_image p;
    if(!ceph_zap_cpu_range(elf,bytes) || !ceph_zap_cpu_range(image,capacity) ||
        !ceph_zap_cpu_range(out,sizeof(*out)) ||
        ceph_zap_cpu_overlap(elf,bytes,image,capacity) ||
        ceph_zap_cpu_overlap(out,sizeof(*out),elf,bytes) ||
        ceph_zap_cpu_overlap(out,sizeof(*out),image,capacity) ||
        ceph_zap_parse(elf,bytes,&p) || capacity<p.logical_bytes)return -1;
    for(uint32_t i=0;i<p.logical_bytes;++i)image[i]=0;
    for(unsigned i=0;i<p.segments;++i) {
        const struct ceph_zap_segment *s=&p.load[i];
        for(uint32_t j=0;j<s->filesz;++j)image[s->image_offset+j]=elf[s->file_offset+j];
    }
    *out=p;return 0;
}

/* Caller owns readable, immutable input and writable disjoint CPU buffers.
 * Validate the entire layout and all capacities/aliases before the first store.
 * No untrusted precomputed plan is accepted. Bytes beyond logical lengths stay
 * untouched; allocation_bytes is a requirement for a FUTURE PAS allocator.
 */
static inline int ceph_zap_stage(const uint8_t *elf,size_t bytes,uint8_t *mdt,size_t mdt_capacity,
    uint8_t *image,size_t image_capacity,struct ceph_zap_image *out)
{
    struct ceph_zap_image p;
    if(!ceph_zap_cpu_range(elf,bytes) || !ceph_zap_cpu_range(mdt,mdt_capacity) ||
        !ceph_zap_cpu_range(image,image_capacity) || !ceph_zap_cpu_range(out,sizeof(*out)) ||
        ceph_zap_cpu_overlap(elf,bytes,mdt,mdt_capacity) || ceph_zap_cpu_overlap(elf,bytes,image,image_capacity) ||
        ceph_zap_cpu_overlap(mdt,mdt_capacity,image,image_capacity) ||
        ceph_zap_cpu_overlap(out,sizeof(*out),elf,bytes) || ceph_zap_cpu_overlap(out,sizeof(*out),mdt,mdt_capacity) ||
        ceph_zap_cpu_overlap(out,sizeof(*out),image,image_capacity) || ceph_zap_parse(elf,bytes,&p) ||
        mdt_capacity<p.mdt_bytes || image_capacity<p.logical_bytes)return -1;
    ceph_zap_mdt_copy(mdt,elf,p.header_bytes);
    ceph_zap_mdt_copy(mdt+p.header_bytes,elf+p.hash_offset,p.hash_bytes);
    for(uint32_t i=0;i<p.logical_bytes;++i)image[i]=0;
    for(unsigned i=0;i<p.segments;++i) {
        const struct ceph_zap_segment *s=&p.load[i];
        for(uint32_t j=0;j<s->filesz;++j)image[s->image_offset+j]=elf[s->file_offset+j];
    }
    *out=p;return 0;
}
#ifdef __cplusplus
extern "C" {
#endif
/* Hold the sealed source lease through verification and both CPU copies.
 * Outputs are usable only on success; this never marks a ZAP instance loaded.
 */
int cepheus_gpu_zap_snapshot_prepare(uint64_t,uint8_t *,size_t,uint8_t *,size_t,struct ceph_zap_image *);
#ifdef __cplusplus
}
#endif
#endif
