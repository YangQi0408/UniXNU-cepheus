/* Software requests only: no hardware ACK, ownership, or PAS admission. */
#ifndef CEPHEUS_RPMH_SNAPSHOT_H
#define CEPHEUS_RPMH_SNAPSHOT_H
#include <stddef.h>
#include <stdint.h>

#define CE_RPMH_SNAPSHOT_PROPERTY "cepheus-rpmh-snapshot"
#define CE_RPMH_SNAPSHOT_MAGIC UINT64_C(0x3150414e53485052)
#define CE_RPMH_RESOURCE_LIMIT 512u
#define CE_RPMH_DRV_LIMIT 8u
#define CE_RPMH_SNAPSHOT_BYTES 1016u

enum { CE_RPMH_SNAP_OK=0, CE_RPMH_SNAP_BIND=-1, CE_RPMH_SNAP_MAP=-2,
    CE_RPMH_SNAP_POOL=-3, CE_RPMH_SNAP_ARRAY=-4, CE_RPMH_SNAP_CHAIN=-5,
    CE_RPMH_SNAP_CHANGED=-6, CE_RPMH_SNAP_MISSING=-7 };
struct ceph_rpmh_vote { uint32_t data, valid, dirty; };
struct ceph_rpmh_drv_snapshot {
    uint32_t id, state, priority, shadow_data, shadow_valid;
    struct ceph_rpmh_vote local[3];
};
struct ceph_rpmh_resource_snapshot {
    uint32_t address, count, pdc_present, reserved;
    struct ceph_rpmh_drv_snapshot drv[CE_RPMH_DRV_LIMIT];
};
struct ceph_rpmh_snapshot {
    uint64_t magic;
    uint32_t version, bytes, complete;
    int32_t error;
    uint32_t identity_mask, resource_count, ebs_attempts, ebs_done;
    uint32_t phase, reserved;
    uint64_t current_el, mpidr, begin_ticks, end_ticks, checksum;
    struct ceph_rpmh_resource_snapshot resource[2];
};
_Static_assert(sizeof(struct ceph_rpmh_drv_snapshot)==56,"RPMh DRV diagnostic ABI");
_Static_assert(sizeof(struct ceph_rpmh_snapshot)==CE_RPMH_SNAPSHOT_BYTES,"RPMh diagnostic ABI");
_Static_assert(offsetof(struct ceph_rpmh_snapshot,checksum)==80,"RPMh checksum ABI");

static inline int ceph_rpmh_snapshot_profile(const char *line)
{
    unsigned profiles=0, ceph=0, exact=0;
    if(!line)return 0;
    for(unsigned i=0;line[i];) {
        while(line[i]==' ' || line[i]=='\t')++i;
        unsigned start=i;
        while(line[i] && line[i]!=' ' && line[i]!='\t')++i;
        unsigned n=i-start;
        const char *p=line+start, key[]="cephrpmhsnap";
        if(n<4 || p[0]!='c' || p[1]!='e' || p[2]!='p' || p[3]!='h')continue;
        ++ceph;
        unsigned j=0;
        while(j<sizeof(key)-1 && j<n && p[j]==key[j])++j;
        if(j!=sizeof(key)-1)continue;
        ++profiles;
        if(n==sizeof(key)+1 && p[j]=='=' && p[j+1]=='1')++exact;
    }
    return !profiles?0:(profiles==1 && ceph==1 && exact==1?1:-1);
}
static inline uint64_t ceph_rpmh_snapshot_checksum(const struct ceph_rpmh_snapshot *s)
{
    const uint8_t *p=(const uint8_t *)s;
    uint64_t sum=UINT64_C(14695981039346656037);
    for(unsigned i=0;i<sizeof(*s);++i)
        sum=(sum^(i>=80 && i<88?0:p[i]))*UINT64_C(1099511628211);
    return sum;
}
static inline int ceph_rpmh_snapshot_valid(const struct ceph_rpmh_snapshot *s)
{
    if(!s || s->magic!=CE_RPMH_SNAPSHOT_MAGIC || s->version!=1 || s->bytes!=sizeof(*s) ||
        s->complete!=1 || s->error>0 || s->error<CE_RPMH_SNAP_MISSING ||
        (s->identity_mask&~15u) || s->resource_count>CE_RPMH_RESOURCE_LIMIT ||
        s->current_el!=4 || s->ebs_done!=1 || !s->ebs_attempts || s->ebs_attempts>3 ||
        s->phase!=1 || s->reserved || s->checksum!=ceph_rpmh_snapshot_checksum(s))return -1;
    if(s->error) {
        const uint8_t *p=(const uint8_t *)s->resource;
        for(unsigned i=0;i<sizeof(s->resource);++i)if(p[i])return -1;
        return 0;
    }
    if(s->identity_mask!=15 || !s->resource_count ||
        s->resource[0].address==s->resource[1].address)return -1;
    for(unsigned r=0;r<2;++r) {
        const struct ceph_rpmh_resource_snapshot *v=&s->resource[r];
        if((v->address>>16)!=3 || (v->address&3) || !v->count ||
            v->count>CE_RPMH_DRV_LIMIT || v->pdc_present>1 || v->reserved)return -1;
        for(unsigned i=0;i<CE_RPMH_DRV_LIMIT;++i) {
            const struct ceph_rpmh_drv_snapshot *d=&v->drv[i];
            if(i>=v->count) {
                const uint8_t *p=(const uint8_t *)d;
                for(unsigned k=0;k<sizeof(*d);++k)if(p[k])return -1;
                continue;
            }
            if(d->state>1 || d->priority>1 || d->shadow_valid>1)return -1;
            for(unsigned j=0;j<i;++j)if(d->id==v->drv[j].id)return -1;
            for(unsigned k=0;k<3;++k)if(d->local[k].valid>1 || d->local[k].dirty>4)return -1;
        }
    }
    return 0;
}
#endif
