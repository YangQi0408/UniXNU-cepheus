/* Copied stage-1 diagnostic, never an SMEM access or GPU admission token. */
#ifndef CEPHEUS_SMEM_MAP_HANDOFF_H
#define CEPHEUS_SMEM_MAP_HANDOFF_H
#include <stddef.h>
#include <stdint.h>

#define CE_SMEM_MAP_PROPERTY "cepheus-smem-map"
#define CE_SMEM_MAP_MAGIC UINT64_C(0x3150414d4d454d53)
#define CE_SMEM_MAP_BYTES 4576u
#define CE_SMEM_MAP_PAGES 512u
#define CE_SMEM_MAP_BASE UINT64_C(0x86000000)
#define CE_SMEM_MAP_SPAN UINT64_C(0x200000)

/* Only the exact isolated switch is accepted; unrelated boots stay inactive. */
static inline int ceph_smem_map_profile(const char *line)
{
    unsigned matches=0, ceph=0, exact=0;
    if (!line) return 0;
    for (size_t i=0; line[i];) {
        while (line[i]==' ' || line[i]=='\t') ++i;
        size_t start=i;
        while (line[i] && line[i]!=' ' && line[i]!='\t') ++i;
        size_t n=i-start;
        const char *p=line+start, key[]="cephsmemmap";
        if (n<4 || p[0]!='c' || p[1]!='e' || p[2]!='p' || p[3]!='h') continue;
        ++ceph;
        size_t j=0;
        while (j<sizeof(key)-1 && j<n && p[j]==key[j]) ++j;
        if (j!=sizeof(key)-1) continue;
        ++matches;
        if (n==sizeof(key)+1 && p[j]=='=' && p[j+1]=='1') ++exact;
    }
    return !matches ? 0 : (matches==1 && ceph==1 && exact==1 ? 1 : -1);
}

struct ceph_smem_map_cpu { uint64_t el, cpu, sctlr, tcr, ttbr0, ttbr1, mair, mmfr0; };
struct ceph_smem_map_resource {
    uint16_t hob_type, hob_bytes;
    uint32_t hob_reserved;
    uint8_t owner[16];
    uint32_t type, attributes;
    uint64_t base, bytes;
};
struct ceph_smem_map_gcd {
    uint64_t base, bytes, capabilities, attributes, image, device;
    uint32_t type, reserved;
};
struct ceph_smem_map_handoff {
    uint64_t magic;
    uint32_t version, bytes, complete, ebs_attempts;
    uint64_t begin_ticks, end_ticks, tree_address, tree_bytes;
    uint64_t prepare_status, probe_status, close_status, map_status, checksum;
    uint64_t base, span, property_offset;
    uint32_t hobs, pages, resource_matches, resource_complete;
    uint32_t gcd_complete, cpu_stable, ebs_done, reserved;
    uint8_t probe_source_sha256[32], profile_source_sha256[32], profile_header_sha256[32];
    struct ceph_smem_map_cpu before, after;
    struct ceph_smem_map_resource resource;
    struct ceph_smem_map_gcd gcd;
    uint64_t par[CE_SMEM_MAP_PAGES];
};
_Static_assert(sizeof(struct ceph_smem_map_cpu)==64,"SMEM CPU ABI");
_Static_assert(sizeof(struct ceph_smem_map_resource)==48,"SMEM resource ABI");
_Static_assert(sizeof(struct ceph_smem_map_gcd)==56,"SMEM GCD ABI");
_Static_assert(offsetof(struct ceph_smem_map_handoff,checksum)==88,"SMEM checksum ABI");
_Static_assert(offsetof(struct ceph_smem_map_handoff,before)==248,"SMEM CPU offset");
_Static_assert(offsetof(struct ceph_smem_map_handoff,par)==480,"SMEM PAR offset");
_Static_assert(sizeof(struct ceph_smem_map_handoff)==CE_SMEM_MAP_BYTES,"SMEM handoff ABI");

static inline uint64_t ceph_smem_map_checksum(const struct ceph_smem_map_handoff *h)
{
    const uint8_t *p=(const uint8_t *)h;
    uint64_t sum=UINT64_C(14695981039346656037);
    for (unsigned i=0; i<sizeof(*h); ++i)
        sum=(sum^(i>=88 && i<96 ? 0 : p[i]))*UINT64_C(1099511628211);
    return sum;
}

/* Accessible, aligned copied RAM is required. Failure diagnostics are retained;
 * these checks establish structure, not authenticity, atomicity or bus access. */
static inline int ceph_smem_map_valid(const struct ceph_smem_map_handoff *h)
{
    if (!h || h->magic!=CE_SMEM_MAP_MAGIC || h->version!=1 || h->bytes!=sizeof(*h) ||
        h->complete!=1 || h->ebs_done!=1 || !h->ebs_attempts || h->ebs_attempts>3 ||
        !h->tree_address || (h->tree_address&4095) || !h->tree_bytes || h->tree_bytes>0x1000000 ||
        h->tree_address>UINT64_MAX-h->tree_bytes || (h->property_offset&3) ||
        h->property_offset>h->tree_bytes || sizeof(*h)>h->tree_bytes-h->property_offset ||
        h->base!=CE_SMEM_MAP_BASE || h->span!=CE_SMEM_MAP_SPAN ||
        h->end_ticks<h->begin_ticks || h->pages>CE_SMEM_MAP_PAGES ||
        h->resource_complete>1 || h->gcd_complete>1 || h->cpu_stable>1 ||
        h->reserved || h->gcd.reserved || h->close_status ||
        (!h->prepare_status && h->map_status!=h->probe_status) ||
        h->checksum!=ceph_smem_map_checksum(h)) return -1;
    if (h->tree_address<CE_SMEM_MAP_BASE+CE_SMEM_MAP_SPAN &&
        CE_SMEM_MAP_BASE<h->tree_address+h->tree_bytes) return -1;
    const uint8_t *ids[3]={h->probe_source_sha256,h->profile_source_sha256,h->profile_header_sha256};
    for (unsigned id=0; id<3; ++id) {
        unsigned nonzero=0;
        for (unsigned j=0; j<32; ++j) nonzero|=ids[id][j];
        if (!nonzero) return -1;
    }
    for (unsigned i=h->pages; i<CE_SMEM_MAP_PAGES; ++i) if (h->par[i]) return -1;
    return 0;
}

static inline int ceph_smem_map_cpu_supported(const struct ceph_smem_map_cpu *c)
{
    return c->el==4 && (c->sctlr&5)==5 && (c->mmfr0&15)<=5 &&
        ((c->mmfr0>>28)&15)==0 && ((c->tcr>>14)&3)==0 &&
        (c->tcr&63)>=16 && (c->tcr&63)<=32 &&
        !(c->tcr&((UINT64_C(1)<<7)|(UINT64_C(1)<<37)|(UINT64_C(1)<<39)|
                  (UINT64_C(1)<<40)|(UINT64_C(1)<<59)));
}

/* This result is only the existing stage-1 mapping diagnostic. */
static inline int ceph_smem_map_diagnostic_pass(const struct ceph_smem_map_handoff *h)
{
    if (ceph_smem_map_valid(h) || h->prepare_status || h->probe_status || h->map_status ||
        h->pages!=CE_SMEM_MAP_PAGES || !h->resource_complete || !h->gcd_complete ||
        !h->cpu_stable || !ceph_smem_map_cpu_supported(&h->before)) return 0;
    const uint8_t *a=(const uint8_t *)&h->before, *b=(const uint8_t *)&h->after;
    for (unsigned i=0; i<sizeof(h->before); ++i) if (a[i]!=b[i]) return 0;
    const struct ceph_smem_map_resource *r=&h->resource;
    const struct ceph_smem_map_gcd *d=&h->gcd;
    if (h->resource_matches!=1 || h->hobs<2 || h->hobs>16384 ||
        r->hob_type!=3 || r->hob_bytes!=sizeof(*r) || r->hob_reserved ||
        r->base!=h->base || r->bytes!=h->span || r->type!=5 || r->attributes!=0x800 ||
        d->base!=h->base || d->bytes!=h->span || d->type!=1 || d->image || d->device ||
        (d->attributes&0x2000)) return 0;
    for (unsigned i=0; i<CE_SMEM_MAP_PAGES; ++i) {
        uint64_t par=h->par[i], address=h->base+(uint64_t)i*4096;
        if ((par&1) || (par>>56)!=0x44 || (par&UINT64_C(0x00ff000000000000)) ||
            (par&UINT64_C(0x0000fffffffff000))!=address) return 0;
    }
    return 1;
}
#endif
